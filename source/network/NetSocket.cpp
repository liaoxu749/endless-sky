/* NetSocket.cpp — ESMP LAN multiplayer.
Implementation of the thin socket wrapper declared in NetSocket.h.
*/
#include "NetSocket.h"

#include <cerrno>

using namespace std;

#ifdef _WIN32
namespace {
	// Note: reference count our WSAStartup so multiple sockets are safe.
	int wsaRefs = 0;
}
#endif

bool NetInit()
{
#ifdef _WIN32
	if(wsaRefs++ == 0)
	{
		WSADATA data;
		if(WSAStartup(MAKEWORD(2, 2), &data) != 0)
		{
			--wsaRefs;
			return false;
		}
	}
#endif
	return true;
}

void NetShutdown()
{
#ifdef _WIN32
	if(wsaRefs > 0 && --wsaRefs == 0)
		WSACleanup();
#endif
}


ESMPAddress ESMPAddress::FromIPv4(const std::string &ip, uint16_t port)
{
	ESMPAddress a;
	a.addr.sin_family = AF_INET;
	a.addr.sin_port = htons(port);
	inet_pton(AF_INET, ip.c_str(), &a.addr.sin_addr);
	return a;
}

ESMPAddress ESMPAddress::Broadcast(uint16_t port)
{
	ESMPAddress a;
	a.addr.sin_family = AF_INET;
	a.addr.sin_port = htons(port);
	a.addr.sin_addr.s_addr = INADDR_BROADCAST;
	return a;
}

std::string ESMPAddress::IP() const
{
	char buf[INET_ADDRSTRLEN] = "";
	inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
	return buf;
}

uint16_t ESMPAddress::Port() const
{
	return ntohs(addr.sin_port);
}


NetSocket::~NetSocket()
{
	Close();
}

NetSocket::NetSocket(NetSocket &&other) noexcept
	: fd(other.fd), isUdp(other.isUdp)
{
	other.fd = ESMP_INVALID_SOCKET;
}

NetSocket &NetSocket::operator=(NetSocket &&other) noexcept
{
	if(this != &other)
	{
		Close();
		fd = other.fd;
		isUdp = other.isUdp;
		other.fd = ESMP_INVALID_SOCKET;
	}
	return *this;
}


bool NetSocket::Open(int type)
{
	Close();
	if(!NetInit())
		return false;
	fd = socket(AF_INET, type, 0);
	if(fd == ESMP_INVALID_SOCKET)
		return false;
	isUdp = (type == SOCK_DGRAM);
	MakeNonBlocking();
	return true;
}

void NetSocket::MakeNonBlocking()
{
#ifdef _WIN32
	u_long mode = 1;
	ioctlsocket(fd, FIONBIO, &mode);
#else
	int flags = fcntl(fd, F_GETFL, 0);
	if(flags != -1)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

bool NetSocket::ListenTcp(uint16_t port)
{
	if(!Open(SOCK_STREAM))
		return false;
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = INADDR_ANY;
	if(bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0
		|| listen(fd, 4) != 0)
	{
		Close();
		return false;
	}
	return true;
}

bool NetSocket::ConnectTcp(const std::string &ip, uint16_t port)
{
	if(!Open(SOCK_STREAM))
		return false;
	sockaddr_in address = ESMPAddress::FromIPv4(ip, port).addr;
	// Non-blocking connect: a later send/recv failure or a zero-length
	// receive with errno != EAGAIN tells us the outcome. For LAN use we
	// simply treat the first successful exchange as "connected".
	return connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0
#ifdef _WIN32
		|| WSAGetLastError() == WSAEWOULDBLOCK;
#else
		|| errno == EINPROGRESS;
#endif
}

std::optional<NetSocket> NetSocket::AcceptTcp()
{
	sockaddr_in peer{};
#ifdef _WIN32
	int len = sizeof(peer);
#else
	socklen_t len = sizeof(peer);
#endif
	ESMPNativeSocket client = accept(fd, reinterpret_cast<sockaddr *>(&peer), &len);
	if(client == ESMP_INVALID_SOCKET)
		return std::nullopt;
	NetSocket result;
	result.fd = client;
	result.isUdp = false;
	result.MakeNonBlocking();
	return result;
}

bool NetSocket::BindUdp(uint16_t port)
{
	if(!Open(SOCK_DGRAM))
		return false;
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = INADDR_ANY;
	// Allow two processes on one machine (host + client testing) to bind.
	int reuse = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
		reinterpret_cast<const char *>(&reuse), sizeof(reuse));
	if(bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
	{
		Close();
		return false;
	}
	return true;
}

int NetSocket::Send(const void *data, size_t size)
{
	return static_cast<int>(send(fd, static_cast<const char *>(data), size, 0));
}

int NetSocket::SendTo(const void *data, size_t size, const ESMPAddress &to)
{
	return static_cast<int>(sendto(fd, static_cast<const char *>(data), size, 0,
		reinterpret_cast<const sockaddr *>(&to.addr), sizeof(to.addr)));
}

int NetSocket::Receive(void *buffer, size_t size)
{
#ifdef _WIN32
	int result = static_cast<int>(recv(fd, static_cast<char *>(buffer),
		static_cast<int>(size), 0));
	int error = WSAGetLastError();
#else
	ssize_t result = recv(fd, buffer, size, 0);
	int error = errno;
#endif
	if(result > 0)
		return static_cast<int>(result);
	if(result == 0)
		return -1; // orderly shutdown
	// Would block => nothing to read right now; that is not an error.
#ifdef _WIN32
	if(error == WSAEWOULDBLOCK || error == WSAECONNRESET)
		return 0;
#else
	if(error == EAGAIN || error == EWOULDBLOCK)
		return 0;
#endif
	return -1;
}

int NetSocket::ReceiveFrom(void *buffer, size_t size, ESMPAddress *from)
{
	sockaddr_in peer{};
#ifdef _WIN32
	int len = sizeof(peer);
	int result = static_cast<int>(recvfrom(fd, static_cast<char *>(buffer),
		static_cast<int>(size), 0, reinterpret_cast<sockaddr *>(&peer), &len));
	int error = WSAGetLastError();
#else
	socklen_t len = sizeof(peer);
	ssize_t result = recvfrom(fd, buffer, size, 0,
		reinterpret_cast<sockaddr *>(&peer), &len);
	int error = errno;
#endif
	if(result > 0)
	{
		if(from)
			from->addr = peer;
		return static_cast<int>(result);
	}
	if(result == 0)
		return -1;
#ifdef _WIN32
	if(error == WSAEWOULDBLOCK || error == WSAECONNRESET)
		return 0;
#else
	if(error == EAGAIN || error == EWOULDBLOCK)
		return 0;
#endif
	return -1;
}

void NetSocket::Close()
{
	if(fd == ESMP_INVALID_SOCKET)
		return;
#ifdef _WIN32
	closesocket(fd);
#else
	close(fd);
#endif
	fd = ESMP_INVALID_SOCKET;
}

uint16_t NetSocket::BoundPort() const
{
	if(fd == ESMP_INVALID_SOCKET)
		return 0;
	sockaddr_in address{};
#ifdef _WIN32
	int len = sizeof(address);
#else
	socklen_t len = sizeof(address);
#endif
	if(getsockname(fd, reinterpret_cast<sockaddr *>(&address), &len) != 0)
		return 0;
	return ntohs(address.sin_port);
}
