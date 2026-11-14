/* NetServer.h — ESMP LAN multiplayer.
Host-side session logic: accepts clients over TCP, receives their input
over UDP, and broadcasts 30Hz authoritative snapshots. Transport only —
the game layer (Engine) maps slots to Ship objects and feeds states.
See DESIGN.md.
*/
#pragma once

#include "NetSocket.h"
#include "Protocol.h"

#include <chrono>
#include <string>
#include <vector>


// Transport-level state for one connected client.
struct NetSession {
	uint8_t slot = 0;          // 1..N; 0 is reserved for the host
	bool helloDone = false;
	std::string name;
	std::string model;         // ship model the client's flagship flies
	NetSocket tcp;
	ESMPAddress udp;           // learned from the first INPUT datagram
	std::vector<uint8_t> tcpBuf;
	ESMP::InputPacket lastInput{};
	bool alive = true;
};


class NetServer {
public:
	~NetServer();

	// Opens the TCP listener and the UDP socket. The host name goes into
	// beacons and WELCOME packets.
	bool Start(const std::string &hostName, uint8_t slots = 4);
	void Stop();
	bool IsRunning() const { return running; }

	// The host provides the system-name table so clients can translate
	// their local system names into the host's session ids.
	void SetSystemTable(std::vector<std::pair<uint16_t, std::string>> table);
	// Announces the host's flagship model to every client (PLAYER_INFO).
	void AnnounceHostModel(const std::string &model);
	// Ship model announced by the given slot (empty if unknown yet).
	std::string Model(uint8_t slot) const;
	// Whether a welcomed client with this slot is connected.
	bool HasClient(uint8_t slot) const;

	// Pump sockets; call once per game frame (60Hz).
	void Tick();

	// The game layer feeds authoritative entity states every frame.
	// Slot 0 is the host's flagship, 1..N are simulated remote flagships.
	void SetEntity(uint8_t slot, uint16_t systemId, double x, double y,
		double vx, double vy, double angleDeg);

	// Latest input from the given client slot (1..N).
	bool GetInput(uint8_t slot, ESMP::InputPacket *out) const;

	// Number of connected players including the host.
	size_t PlayerCount() const;
	// Name of the player in the given slot (for lobby / HUD).
	std::string PlayerName(uint8_t slot) const;


private:
	void AcceptNewClients();
	void PollClientTcp(NetSession &session);
	void PollUdp();
	void SendSnapshots();
	void SendBeacon();
	void SendFrame(NetSession &session, ESMP::Msg type, const void *payload,
		uint16_t length);
	void SendPlayerInfo(NetSession &session, uint8_t slot, const std::string &model);

	NetSocket tcpListener;
	NetSocket udp;
	bool running = false;
	uint8_t maxSlots = 4;
	std::string hostName;

	uint32_t snapshotSeq = 0;
	std::chrono::steady_clock::time_point lastSnapshot;
	std::chrono::steady_clock::time_point lastBeacon;

	std::vector<NetSession> clients;

	// System name -> session id, sent to clients after WELCOME.
	std::vector<std::pair<uint16_t, std::string>> systemTable;
	std::string hostModel;

	struct EntityState {
		uint16_t systemId = 0;
		float x = 0.f;
		float y = 0.f;
		float vx = 0.f;
		float vy = 0.f;
		float angleDeg = 0.f;
		bool valid = false;
	};
	std::vector<EntityState> states; // index = slot, size maxSlots + 1
};
