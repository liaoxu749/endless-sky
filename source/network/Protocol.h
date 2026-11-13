/* Protocol.h — ESMP (Endless Sky MultiPlayer) wire protocol, v1.
LAN multiplayer for Endless Sky. Host-authoritative design; see DESIGN.md.

TCP messages are framed as [u8 type][u16le length][payload].
UDP messages consist of a fixed-size header followed by packed structs.
All structs are packed; all floats are IEEE-754 little-endian in practice
(x86/ARM on the LAN), which we accept for a LAN-only protocol.
*/
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace ESMP {
	// LAN discovery and game traffic share this UDP port; TCP uses it too.
	constexpr uint16_t PORT = 4777;
	constexpr uint32_t PROTOCOL_VERSION = 1;
	// Beacon magic bytes; also guards against stray UDP traffic.
	constexpr const char *MAGIC = "ESMP";
	// Snapshot cadence and per-snapshot entity cap.
	constexpr int SNAPSHOT_HZ = 30;
	constexpr int MAX_SNAPSHOT_ENTITIES = 32;
	// Name/text lengths include the NUL terminator.
	constexpr int NAME_LENGTH = 32;
	constexpr int CHAT_LENGTH = 128;

	// Wire message types.
	enum class Msg : uint8_t {
		// TCP, client -> host.
		HELLO = 1,      // payload: HelloPacket
		INPUT,          // payload: InputPacket (also allowed over UDP)
		CHAT,           // payload: ChatPacket
		BYE,            // no payload; polite disconnect
		// TCP, host -> client.
		WELCOME,        // payload: WelcomePacket
		SYS_TABLE,      // payload: SystemEntry, one frame per system
		FIRE_EVENT,     // payload: FireEventPacket
		SHIP_EVENT,     // payload: ShipEventPacket
		SYS_CHANGE,     // payload: SysChangePacket
		LANDED,         // payload: LandedPacket
		// Either direction.
		PING,           // payload: PingPacket
		PONG,           // payload: PingPacket
		// UDP only.
		SNAPSHOT = 64,  // UdpHeader + EntitySnapshot[count]
		BEACON = 65,    // BeaconPacket (LAN discovery broadcast)
	};

	#pragma pack(push, 1)
	struct HelloPacket {
		uint32_t version;
		char name[NAME_LENGTH]; // player name, NUL-terminated
	};

	struct WelcomePacket {
		uint8_t slot;           // player slot assigned by the host (0 = host)
		char hostName[NAME_LENGTH];
		uint16_t hostSystemId;  // system id of the host's flagship right now
	};

	// One frame per system; ids are host-assigned and stable for the session.
	struct SystemEntry {
		uint16_t id;
		char name[NAME_LENGTH]; // system name as it appears in the data files
	};

	// Client input state, sent at SNAPSHOT_HZ. The two bitsets are the raw
	// Command / FireCommand sets (Command.h, FireCommand.h). The slot field
	// lets the host attribute UDP datagrams without a handshake.
	struct InputPacket {
		uint8_t slot;
		uint32_t commands;
		uint32_t fire;
		float mouse[2]; // aim point, engine coordinates
	};

	struct PingPacket { float sentAt; };

	struct ChatPacket { char text[CHAT_LENGTH]; };

	// One entity in a host snapshot. Engine-space doubles are narrowed to
	// float; angle is degrees 0..360.
	struct EntitySnapshot {
		uint8_t slot;        // 0 = host flagship, 1.. = clients
		uint16_t systemId;
		float x, y;
		float vx, vy;
		uint16_t angleDeg;
	};

	struct UdpHeader {
		uint8_t type;        // Msg as uint8_t (SNAPSHOT or BEACON)
		uint32_t seq;
		uint16_t count;      // entities following (SNAPSHOT only)
	};

	struct BeaconPacket {  // broadcast by the host once per second
		char magic[4];       // "ESMP"
		uint32_t version;
		char hostName[NAME_LENGTH];
		uint8_t players;     // current player count
		uint8_t slots;       // capacity
	};

	// M2: weapon fire relayed so clients can simulate the visuals locally.
	struct FireEventPacket {
		uint8_t srcSlot;
		uint8_t weaponIndex;
		uint16_t systemId;
		float x, y;
		float aim[2];
	};

	// M2: authoritative combat events (mirrors Engine's ShipEvent kinds).
	struct ShipEventPacket {
		uint8_t kind;        // ShipEvent::Type as uint8_t
		uint8_t srcSlot;
		uint8_t dstSlot;
	};

	// M4: system transitions.
	struct SysChangePacket { uint8_t slot; uint16_t newSystemId; };
	struct LandedPacket { uint8_t slot; uint16_t planetId; char planetName[NAME_LENGTH]; };
	#pragma pack(pop)

	// Frame helpers for TCP. FrameMessage returns the total bytes written
	// (3 + length), or -1 if the output buffer is too small.
	inline int FrameMessage(uint8_t *out, size_t cap, Msg type,
		const void *payload, uint16_t length)
	{
		if(cap < 3u + length)
			return -1;
		out[0] = static_cast<uint8_t>(type);
		out[1] = static_cast<uint8_t>(length & 0xffu);
		out[2] = static_cast<uint8_t>(length >> 8);
		if(length && payload)
			memcpy(out + 3, payload, length);
		return 3 + length;
	}

	// Parse one complete frame from the front of the buffer. On success sets
	// *payload/*length, adds 3+length to *offset, and returns the type.
	// Returns 0 if more bytes are needed, or -1 on malformed data.
	inline int ParseFrame(const uint8_t *buffer, size_t available, size_t *offset,
		const uint8_t **payload, uint16_t *length)
	{
		if(available - *offset < 3u)
			return 0;
		uint16_t len = static_cast<uint16_t>(buffer[*offset + 1]
			| (buffer[*offset + 2] << 8));
		if(available - *offset < 3u + len)
			return 0;
		*payload = buffer + *offset + 3;
		*length = len;
		int type = buffer[*offset];
		*offset += 3u + len;
		return type;
	}
}
