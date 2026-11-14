/* NetClient.cpp — ESMP LAN multiplayer. Client-side session logic. */
#include "NetClient.h"

#include <algorithm>
#include <thread>

using namespace std;
using namespace std::chrono;


bool NetClient::Connect(const std::string &hostIp, const std::string &playerName)
{
	Disconnect();
	if(!tcp.ConnectTcp(hostIp, ESMP::PORT))
	{
		tcp.Close();
		return false;
	}
	// Bind an ephemeral UDP port for the game session: the host learns our
	// endpoint from the first INPUT datagram (Protocol.h InputPacket.slot),
	// so no fixed client port is needed and several clients can share one
	// machine. Port 4777 stays reserved for the host and LAN discovery.
	if(!udp.BindUdp(0))
	{
		tcp.Close();
		return false;
	}
	serverUdp = ESMPAddress::FromIPv4(hostIp, ESMP::PORT);
	systemTable.clear();
	snapshot.clear();
	snapshotSeq = 0;
	welcomed = false;
	connected = true;
	startedAt = steady_clock::now();
	lastPing = lastInputSent = startedAt - seconds(2);

	ESMP::HelloPacket hello{};
	hello.version = ESMP::PROTOCOL_VERSION;
	strncpy(hello.name, playerName.c_str(), ESMP::NAME_LENGTH - 1);
	SendFrame(ESMP::Msg::HELLO, &hello, sizeof(hello));
	return true;
}

void NetClient::Disconnect()
{
	if(connected)
		SendFrame(ESMP::Msg::BYE, nullptr, 0);
	tcp.Close();
	udp.Close();
	connected = false;
	welcomed = false;
	systemTable.clear();
	snapshot.clear();
	rttMs = -1.f;
}

void NetClient::SetInput(uint64_t commands, uint64_t fire, double turn,
	double mouseX, double mouseY)
{
	pendingInput.slot = slot;
	pendingInput.commands = commands;
	pendingInput.fire = fire;
	pendingInput.turn = static_cast<float>(turn);
	pendingInput.mouse[0] = static_cast<float>(mouseX);
	pendingInput.mouse[1] = static_cast<float>(mouseY);
}

void NetClient::SendPlayerInfo(const std::string &model)
{
	ESMP::PlayerInfoPacket packet{};
	packet.slot = slot;
	strncpy(packet.model, model.c_str(), ESMP::MODEL_LENGTH - 1);
	SendFrame(ESMP::Msg::PLAYER_INFO, &packet, sizeof(packet));
}

void NetClient::Tick()
{
	if(!connected)
		return;
	auto now = steady_clock::now();

	PollTcp();
	PollUdp();
	if(!connected)
		return;

	// Keep-alive pings once a second.
	if(now - lastPing >= seconds(1))
	{
		lastPing = now;
		ESMP::PingPacket ping{};
		ping.sentAt = duration_cast<duration<float>>(now - startedAt).count();
		SendFrame(ESMP::Msg::PING, &ping, sizeof(ping));
	}

	// Ship the latest input at SNAPSHOT_HZ once we have a slot.
	if(welcomed && now - lastInputSent >=
		duration_cast<steady_clock::duration>(duration<double>(1.0 / ESMP::SNAPSHOT_HZ)))
	{
		lastInputSent = now;
		udp.SendTo(&pendingInput, sizeof(pendingInput), serverUdp);
	}
}


void NetClient::PollTcp()
{
	uint8_t chunk[4096];
	int received = tcp.Receive(chunk, sizeof(chunk));
	if(received < 0)
	{
		Disconnect();
		return;
	}
	if(received > 0)
		tcpBuf.insert(tcpBuf.end(), chunk, chunk + received);

	size_t offset = 0;
	while(connected)
	{
		const uint8_t *payload = nullptr;
		uint16_t length = 0;
		int type = ESMP::ParseFrame(tcpBuf.data(), tcpBuf.size(), &offset,
			&payload, &length);
		if(type <= 0)
		{
			if(type < 0)
				Disconnect();
			break;
		}
		switch(static_cast<ESMP::Msg>(type))
		{
			case ESMP::Msg::WELCOME:
			{
				if(length < static_cast<uint16_t>(sizeof(ESMP::WelcomePacket)))
				{
					Disconnect();
					break;
				}
				ESMP::WelcomePacket welcome;
				memcpy(&welcome, payload, sizeof(welcome));
				slot = welcome.slot;
				welcome.hostName[ESMP::NAME_LENGTH - 1] = '\0';
				hostName = welcome.hostName;
				welcomed = true;
				break;
			}
			case ESMP::Msg::SYS_TABLE:
			{
				if(payload && length >= sizeof(ESMP::SystemEntry))
				{
					ESMP::SystemEntry entry;
					memcpy(&entry, payload, sizeof(entry));
					entry.name[ESMP::NAME_LENGTH - 1] = '\0';
					systemTable.emplace_back(entry.id, entry.name);
				}
				break;
			}
			case ESMP::Msg::PLAYER_INFO:
			{
				if(payload && length >= sizeof(ESMP::PlayerInfoPacket))
				{
					ESMP::PlayerInfoPacket packet;
					memcpy(&packet, payload, sizeof(packet));
					packet.model[ESMP::MODEL_LENGTH - 1] = '\0';
					playerModels[packet.slot] = packet.model;
				}
				break;
			}
			case ESMP::Msg::PONG:
			{
				if(payload && length >= sizeof(ESMP::PingPacket))
				{
					ESMP::PingPacket ping;
					memcpy(&ping, payload, sizeof(ping));
					float nowSec = duration_cast<duration<float>>(
						steady_clock::now() - startedAt).count();
					rttMs = max(0.f, (nowSec - ping.sentAt) * 1000.f);
				}
				break;
			}
			case ESMP::Msg::BYE:
				Disconnect();
				break;
			default:
				break;
		}
	}
	if(offset)
		tcpBuf.erase(tcpBuf.begin(), tcpBuf.begin() + offset);
}

void NetClient::PollUdp()
{
	uint8_t buffer[1500];
	while(true)
	{
		ESMPAddress from;
		int received = udp.ReceiveFrom(buffer, sizeof(buffer), &from);
		if(received <= 0)
			break;
		if(static_cast<size_t>(received) < sizeof(ESMP::UdpHeader))
			continue;
		ESMP::UdpHeader header;
		memcpy(&header, buffer, sizeof(header));
		if(header.type != static_cast<uint8_t>(ESMP::Msg::SNAPSHOT))
			continue; // ignore beacons from other hosts
		if(header.count > ESMP::MAX_SNAPSHOT_ENTITIES)
			continue;
		size_t expected = sizeof(ESMP::UdpHeader)
			+ header.count * sizeof(ESMP::EntitySnapshot);
		if(static_cast<size_t>(received) < expected)
			continue;
		// Only accept snapshots sent by the server we connected to.
		if(from.IP() != serverUdp.IP())
			continue;

		snapshot.clear();
		snapshot.reserve(header.count);
		auto *entities = reinterpret_cast<const ESMP::EntitySnapshot *>(
			buffer + sizeof(ESMP::UdpHeader));
		for(uint16_t i = 0; i < header.count; ++i)
		{
			const ESMP::EntitySnapshot &entity = entities[i];
			RemoteEntity remote;
			remote.slot = entity.slot;
			remote.systemId = entity.systemId;
			remote.x = entity.x;
			remote.y = entity.y;
			remote.vx = entity.vx;
			remote.vy = entity.vy;
			remote.angleDeg = entity.angleDeg;
			snapshot.push_back(remote);
		}
		snapshotSeq = header.seq;
	}
}

vector<NetClient::DiscoveredHost> NetClient::Discover(float budgetSeconds)
{
	NetSocket listener;
	if(!listener.BindUdp(ESMP::PORT))
		return {};
	vector<DiscoveredHost> hosts;
	auto deadline = steady_clock::now()
		+ duration_cast<steady_clock::duration>(duration<float>(budgetSeconds));
	uint8_t buffer[512];
	while(steady_clock::now() < deadline)
	{
		ESMPAddress from;
		int received = listener.ReceiveFrom(buffer, sizeof(buffer), &from);
		if(received >= static_cast<int>(sizeof(ESMP::BeaconPacket)))
		{
			ESMP::BeaconPacket beacon;
			memcpy(&beacon, buffer, sizeof(beacon));
			if(memcmp(beacon.magic, ESMP::MAGIC, 4) == 0
				&& beacon.version == ESMP::PROTOCOL_VERSION)
			{
				beacon.hostName[ESMP::NAME_LENGTH - 1] = '\0';
				bool known = false;
				for(const DiscoveredHost &host : hosts)
					if(host.ip == from.IP())
						known = true;
				if(!known)
				{
					DiscoveredHost host;
					host.ip = from.IP();
					host.hostName = beacon.hostName;
					host.players = beacon.players;
					host.slots = beacon.slots;
					hosts.push_back(std::move(host));
				}
			}
		}
		this_thread::sleep_for(milliseconds(10));
	}
	return hosts;
}

void NetClient::SendFrame(ESMP::Msg type, const void *payload, uint16_t length)
{
	uint8_t frame[3 + sizeof(ESMP::ChatPacket)];
	int bytes = ESMP::FrameMessage(frame, sizeof(frame), type, payload, length);
	if(bytes > 0)
		tcp.Send(frame, static_cast<size_t>(bytes));
}
