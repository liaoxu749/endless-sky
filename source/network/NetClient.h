/* NetClient.h — ESMP LAN multiplayer.
Client-side session logic: connects to a host over TCP, sends input over
UDP at 30Hz, receives authoritative snapshots, and discovers LAN hosts.
Transport only — the game layer (Engine) maps slots to ghost ships.
See DESIGN.md.
*/
#pragma once

#include "NetSocket.h"
#include "Protocol.h"

#include <chrono>
#include <string>
#include <utility>
#include <vector>


class NetClient {
public:
	// UDP datagram learned during discovery.
	struct DiscoveredHost {
		std::string ip;
		std::string hostName;
		uint8_t players = 0;
		uint8_t slots = 0;
	};

	bool Connect(const std::string &hostIp, const std::string &playerName);
	void Disconnect();
	bool IsConnected() const { return connected; }
	bool IsWelcomed() const { return welcomed; }
	uint8_t Slot() const { return slot; }
	const std::string &HostName() const { return hostName; }
	float RttMs() const { return rttMs; }

	// Pump sockets; call once per game frame (60Hz).
	void Tick();

	// The game layer provides the local player's latest input every frame;
	// it goes out over UDP at SNAPSHOT_HZ.
	void SetInput(uint32_t commands, uint32_t fire, double mouseX, double mouseY);

	// One remote entity from the latest authoritative snapshot.
	struct RemoteEntity {
		uint8_t slot;
		uint16_t systemId;
		double x, y, vx, vy, angleDeg;
	};
	const std::vector<RemoteEntity> &LatestSnapshot() const { return snapshot; }
	uint32_t SnapshotSeq() const { return snapshotSeq; }

	// Host-assigned mapping: system name -> host system id.
	const std::vector<std::pair<uint16_t, std::string>> &SystemTable() const
	{
		return systemTable;
	}

	// Listen for LAN host beacons for the given time budget (seconds) and
	// return whatever was heard. Call repeatedly from the lobby panel and
	// merge results.
	static std::vector<DiscoveredHost> Discover(float budgetSeconds = 0.25f);


private:
	void PollTcp();
	void PollUdp();
	void SendFrame(ESMP::Msg type, const void *payload, uint16_t length);

	NetSocket tcp;
	NetSocket udp;
	ESMPAddress serverUdp;

	bool connected = false;
	bool welcomed = false;
	uint8_t slot = 0;
	std::string hostName;
	std::vector<uint8_t> tcpBuf;

	std::vector<std::pair<uint16_t, std::string>> systemTable;

	std::vector<RemoteEntity> snapshot;
	uint32_t snapshotSeq = 0;

	ESMP::InputPacket pendingInput{};

	std::chrono::steady_clock::time_point startedAt;
	std::chrono::steady_clock::time_point lastPing;
	std::chrono::steady_clock::time_point lastInputSent;
	float rttMs = -1.f;
};
