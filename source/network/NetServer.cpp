/* NetServer.cpp — ESMP LAN multiplayer. Host-side session logic. */
#include "NetServer.h"

#include <algorithm>

using namespace std;
using namespace std::chrono;


NetServer::~NetServer()
{
	Stop();
}

bool NetServer::Start(const std::string &name, uint8_t slots)
{
	Stop();
	if(!tcpListener.ListenTcp(ESMP::PORT))
		return false;
	if(!udp.BindUdp(ESMP::PORT))
	{
		tcpListener.Close();
		return false;
	}
	hostName = name;
	maxSlots = slots;
	clients.clear();
	states.assign(maxSlots + 1, EntityState());
	running = true;
	lastSnapshot = lastBeacon = steady_clock::now();
	return true;
}

void NetServer::Stop()
{
	if(!running)
		return;
	// Politely tell everyone we are going away.
	for(auto &session : clients)
		if(session.alive && session.helloDone)
			SendFrame(session, ESMP::Msg::BYE, nullptr, 0);
	clients.clear();
	tcpListener.Close();
	udp.Close();
	running = false;
}

void NetServer::Tick()
{
	if(!running)
		return;
	auto now = steady_clock::now();

	AcceptNewClients();
	for(auto &session : clients)
		if(session.alive)
			PollClientTcp(session);
	clients.erase(remove_if(clients.begin(), clients.end(),
			[](const NetSession &s) { return !s.alive; }),
		clients.end());

	PollUdp();

	const auto snapshotInterval = duration_cast<steady_clock::duration>(
		chrono::duration<double>(1.0 / ESMP::SNAPSHOT_HZ));
	if(now - lastSnapshot >= snapshotInterval)
	{
		lastSnapshot = now;
		SendSnapshots();
	}
	if(now - lastBeacon >= seconds(1))
	{
		lastBeacon = now;
		SendBeacon();
	}
}

void NetServer::SetEntity(uint8_t slot, uint16_t systemId, double x, double y,
	double vx, double vy, double angleDeg)
{
	if(slot >= states.size())
		return;
	EntityState &state = states[slot];
	state.systemId = systemId;
	state.x = static_cast<float>(x);
	state.y = static_cast<float>(y);
	state.vx = static_cast<float>(vx);
	state.vy = static_cast<float>(vy);
	state.angleDeg = static_cast<float>(angleDeg);
	state.valid = true;
}

bool NetServer::GetInput(uint8_t slot, ESMP::InputPacket *out) const
{
	for(const NetSession &session : clients)
		if(session.alive && session.helloDone && session.slot == slot)
		{
			if(out)
				*out = session.lastInput;
			return true;
		}
	return false;
}

size_t NetServer::PlayerCount() const
{
	size_t count = 1; // the host
	for(const NetSession &session : clients)
		if(session.alive && session.helloDone)
			++count;
	return count;
}

std::string NetServer::PlayerName(uint8_t slot) const
{
	if(slot == 0)
		return hostName;
	for(const NetSession &session : clients)
		if(session.alive && session.helloDone && session.slot == slot)
			return session.name;
	return "";
}

void NetServer::SetSystemTable(std::vector<std::pair<uint16_t, std::string>> table)
{
	systemTable = std::move(table);
}

void NetServer::AnnounceHostModel(const std::string &model)
{
	hostModel = model;
	for(auto &session : clients)
		if(session.alive && session.helloDone)
			SendPlayerInfo(session, 0, model);
}

std::string NetServer::Model(uint8_t slot) const
{
	if(slot == 0)
		return hostModel;
	for(const NetSession &session : clients)
		if(session.alive && session.helloDone && session.slot == slot)
			return session.model;
	return "";
}

bool NetServer::HasClient(uint8_t slot) const
{
	for(const NetSession &session : clients)
		if(session.alive && session.helloDone && session.slot == slot)
			return true;
	return false;
}


void NetServer::AcceptNewClients()
{
	while(true)
	{
		auto client = tcpListener.AcceptTcp();
		if(!client)
			break;
		// Find a free slot.
		uint8_t slot = 0;
		for(uint8_t candidate = 1; candidate <= maxSlots; ++candidate)
		{
			bool taken = false;
			for(const NetSession &session : clients)
				if(session.alive && session.slot == candidate)
					taken = true;
			if(!taken)
			{
				slot = candidate;
				break;
			}
		}
		if(slot == 0)
			continue; // lobby is full; the socket closes on destruction

		NetSession session;
		session.slot = slot;
		session.tcp = std::move(*client);
		session.tcpBuf.reserve(4096);
		clients.push_back(std::move(session));
	}
}

void NetServer::PollClientTcp(NetSession &session)
{
	uint8_t chunk[4096];
	int received = session.tcp.Receive(chunk, sizeof(chunk));
	if(received < 0)
	{
		session.alive = false;
		return;
	}
	if(received > 0)
		session.tcpBuf.insert(session.tcpBuf.end(), chunk, chunk + received);

	size_t offset = 0;
	while(true)
	{
		const uint8_t *payload = nullptr;
		uint16_t length = 0;
		int type = ESMP::ParseFrame(session.tcpBuf.data(),
			session.tcpBuf.size(), &offset, &payload, &length);
		if(type <= 0)
		{
			// Need more data (0) or malformed frame (-1).
			if(type < 0)
				session.alive = false;
			break;
		}
		switch(static_cast<ESMP::Msg>(type))
		{
			case ESMP::Msg::HELLO:
			{
				if(length < static_cast<uint16_t>(sizeof(ESMP::HelloPacket)))
				{
					session.alive = false;
					break;
				}
				ESMP::HelloPacket hello;
				memcpy(&hello, payload, sizeof(hello));
				if(hello.version != ESMP::PROTOCOL_VERSION)
				{
					SendFrame(session, ESMP::Msg::BYE, nullptr, 0);
					session.alive = false;
					break;
				}
				hello.name[ESMP::NAME_LENGTH - 1] = '\0';
				session.name = hello.name;
				session.helloDone = true;
				// Greet with our slot assignment and the system id table.
				ESMP::WelcomePacket welcome{};
				welcome.slot = session.slot;
				strncpy(welcome.hostName, hostName.c_str(), ESMP::NAME_LENGTH - 1);
				welcome.hostSystemId = states[0].systemId;
				SendFrame(session, ESMP::Msg::WELCOME, &welcome, sizeof(welcome));
				for(const auto &entry : systemTable)
				{
					ESMP::SystemEntry packet{};
					packet.id = entry.first;
					strncpy(packet.name, entry.second.c_str(), ESMP::NAME_LENGTH - 1);
					SendFrame(session, ESMP::Msg::SYS_TABLE, &packet, sizeof(packet));
				}
				break;
			}
			case ESMP::Msg::PLAYER_INFO:
			{
				if(!payload || length < sizeof(ESMP::PlayerInfoPacket))
					break;
				ESMP::PlayerInfoPacket packet;
				memcpy(&packet, payload, sizeof(packet));
				packet.model[ESMP::MODEL_LENGTH - 1] = '\0';
				session.model = packet.model;
				// Relay to every other client so peers can build the ship.
				for(auto &other : clients)
					if(other.alive && other.helloDone && other.slot != session.slot)
						SendPlayerInfo(other, session.slot, session.model);
				break;
			}
			case ESMP::Msg::PING:
			{
				if(payload && length >= sizeof(ESMP::PingPacket))
					SendFrame(session, ESMP::Msg::PONG, payload,
						sizeof(ESMP::PingPacket));
				break;
			}
			case ESMP::Msg::CHAT:
			{
				if(payload && length >= sizeof(ESMP::ChatPacket))
					for(auto &other : clients)
						if(other.alive && other.helloDone)
							SendFrame(other, ESMP::Msg::CHAT, payload,
								sizeof(ESMP::ChatPacket));
				break;
			}
			case ESMP::Msg::BYE:
				session.alive = false;
				break;
			default:
				break;
		}
	}
	// Trim consumed bytes.
	if(offset)
		session.tcpBuf.erase(session.tcpBuf.begin(),
			session.tcpBuf.begin() + offset);
}

void NetServer::PollUdp()
{
	uint8_t buffer[1500];
	while(true)
	{
		ESMPAddress from;
		int received = udp.ReceiveFrom(buffer, sizeof(buffer), &from);
		if(received <= 0)
			break;
		if(static_cast<size_t>(received) < sizeof(ESMP::InputPacket))
			continue;
		ESMP::InputPacket input;
		memcpy(&input, buffer, sizeof(input));
		if(input.slot == 0 || input.slot > maxSlots)
			continue;
		for(auto &session : clients)
			if(session.alive && session.helloDone && session.slot == input.slot)
			{
				session.udp = from; // learn the client's UDP endpoint
				session.lastInput = input;
				break;
			}
	}
}

void NetServer::SendSnapshots()
{
	ESMP::UdpHeader header{};
	header.type = static_cast<uint8_t>(ESMP::Msg::SNAPSHOT);
	header.seq = ++snapshotSeq;

	uint8_t buffer[sizeof(ESMP::UdpHeader)
		+ ESMP::MAX_SNAPSHOT_ENTITIES * sizeof(ESMP::EntitySnapshot)];
	memcpy(buffer, &header, sizeof(header));

	uint16_t count = 0;
	auto *entities = reinterpret_cast<ESMP::EntitySnapshot *>(
		buffer + sizeof(ESMP::UdpHeader));
	for(size_t slot = 0; slot < states.size() && count < ESMP::MAX_SNAPSHOT_ENTITIES; ++slot)
	{
		const EntityState &state = states[slot];
		if(!state.valid)
			continue;
		ESMP::EntitySnapshot &snapshot = entities[count++];
		snapshot.slot = static_cast<uint8_t>(slot);
		snapshot.systemId = state.systemId;
		snapshot.x = state.x;
		snapshot.y = state.y;
		snapshot.vx = state.vx;
		snapshot.vy = state.vy;
		snapshot.angleDeg = static_cast<uint16_t>(state.angleDeg);
	}

	// Only send when at least one client is out there.
	bool anyClient = false;
	for(const NetSession &session : clients)
		if(session.alive && session.helloDone)
			anyClient = true;
	if(!anyClient)
		return;

	header.count = count;
	memcpy(buffer, &header, sizeof(header));
	size_t bytes = sizeof(ESMP::UdpHeader) + count * sizeof(ESMP::EntitySnapshot);
	for(const NetSession &session : clients)
		if(session.alive && session.helloDone && session.udp.addr.sin_port)
			udp.SendTo(buffer, bytes, session.udp);
}

void NetServer::SendBeacon()
{
	ESMP::BeaconPacket beacon{};
	memcpy(beacon.magic, ESMP::MAGIC, 4);
	beacon.version = ESMP::PROTOCOL_VERSION;
	strncpy(beacon.hostName, hostName.c_str(), ESMP::NAME_LENGTH - 1);
	beacon.players = static_cast<uint8_t>(PlayerCount());
	beacon.slots = static_cast<uint8_t>(maxSlots + 1);
	udp.SendTo(&beacon, sizeof(beacon), ESMPAddress::Broadcast(ESMP::PORT));
}

void NetServer::SendFrame(NetSession &session, ESMP::Msg type,
	const void *payload, uint16_t length)
{
	uint8_t frame[3 + sizeof(ESMP::ChatPacket)];
	int bytes = ESMP::FrameMessage(frame, sizeof(frame), type, payload, length);
	if(bytes > 0)
		session.tcp.Send(frame, static_cast<size_t>(bytes));
}

void NetServer::SendPlayerInfo(NetSession &session, uint8_t slot,
	const std::string &model)
{
	ESMP::PlayerInfoPacket packet{};
	packet.slot = slot;
	strncpy(packet.model, model.c_str(), ESMP::MODEL_LENGTH - 1);
	SendFrame(session, ESMP::Msg::PLAYER_INFO, &packet, sizeof(packet));
}
