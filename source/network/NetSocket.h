/* NetSocket.h — ESMP LAN multiplayer.
Thin RAII wrapper around non-blocking BSD sockets (WinSock2 on Windows).
See DESIGN.md for the multiplayer architecture.
*/
#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>

#ifdef _WIN32
	#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
	#endif
	#include <winsock2.h>
	#include <ws2tcpip.h>
	using ESMPNativeSocket = SOCKET;
	#define ESMP_INVALID_SOCKET INVALID_SOCKET
#else
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <unistd.h>
	#include <fcntl.h>
	using ESMPNativeSocket = int;
	#define ESMP_INVALID_SOCKET (-1)
#endif


// A resolved IPv4 endpoint.
struct ESMPAddress {
	sockaddr_in addr{};

	static ESMPAddress FromIPv4(const std::string &ip, uint16_t port);
	static ESMPAddress Broadcast(uint16_t port);
	std::string IP() const;
	uint16_t Port() const;
};


// One-time per-process initialization (WSAStartup on Windows).
bool NetInit();
void NetShutdown();


// Non-blocking socket wrapper: TCP listener/connection or UDP.
class NetSocket {
public:
	NetSocket() = default;
	~NetSocket();
	NetSocket(const NetSocket &) = delete;
	NetSocket &operator=(const NetSocket &) = delete;
	NetSocket(NetSocket &&other) noexcept;
	NetSocket &operator=(NetSocket &&other) noexcept;

	// TCP server: bind + listen. TCP client: connect.
	bool ListenTcp(uint16_t port);
	bool ConnectTcp(const std::string &ip, uint16_t port);
	std::optional<NetSocket> AcceptTcp();

	// UDP: bind for receiving (broadcasts / snapshots).
	bool BindUdp(uint16_t port);

	// Bytes sent, or -1. UDP sends go to the given address.
	int Send(const void *data, size_t size);
	int SendTo(const void *data, size_t size, const ESMPAddress &to);

	// Bytes received; 0 means "no data right now"; -1 means error or the
	// connection was closed by the peer.
	int Receive(void *buffer, size_t size);
	int ReceiveFrom(void *buffer, size_t size, ESMPAddress *from);

	void Close();
	bool IsOpen() const { return fd != ESMP_INVALID_SOCKET; }
	// Actual bound port (useful after binding to port 0).
	uint16_t BoundPort() const;


private:
	bool Open(int type);
	void MakeNonBlocking();

	ESMPNativeSocket fd = ESMP_INVALID_SOCKET;
	bool isUdp = false;
};
