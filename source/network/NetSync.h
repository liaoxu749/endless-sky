/* NetSync.h — ESMP LAN multiplayer.
Glue between the network transport and the game engine: owns the session
role, creates/updates remote ships ("ghosts" on clients, network-steered
ships on the host), feeds the host-side simulation with remote input, and
publishes authoritative snapshots. See DESIGN.md.
*/
#pragma once

#include "NetClient.h"
#include "NetServer.h"

#include <list>
#include <map>
#include <memory>
#include <string>

class Command;
class PlayerInfo;
class Ship;


class NetSync {
public:
	enum class Role { None, Host, Client };

	NetSync() = default;
	~NetSync();

	void StartHost(PlayerInfo &player);
	bool Join(PlayerInfo &player, const std::string &hostIp);
	void Disconnect();

	Role GetRole() const { return role; }
	bool IsHost() const { return role == Role::Host; }
	bool IsClient() const { return role == Role::Client; }
	bool IsActive() const { return role != Role::None; }

	// Called once per frame from Engine::Step, with the calculation thread
	// paused (so mutating the ships list is safe here).
	void Step(PlayerInfo &player, std::list<std::shared_ptr<Ship>> &ships,
		const Command &activeCommands);


private:
	void TickHost(PlayerInfo &player, std::list<std::shared_ptr<Ship>> &ships,
		const Command &activeCommands);
	void TickClient(PlayerInfo &player, std::list<std::shared_ptr<Ship>> &ships,
		const Command &activeCommands);
	void Reconcile(std::list<std::shared_ptr<Ship>> &ships);
	void RemoveAllRemoteShips(std::list<std::shared_ptr<Ship>> &ships);
	std::shared_ptr<Ship> CreateRemoteShip(PlayerInfo &player, uint8_t slot,
		const std::string &model);
	uint16_t SystemIdOf(const std::string &name) const;

	Role role = Role::None;
	NetServer server;
	NetClient client;

	// Remote ships currently present in Engine::ships, by slot.
	std::map<uint8_t, std::shared_ptr<Ship>> remoteShips;
	// Ship model announced per slot (0 = host flagship).
	std::map<uint8_t, std::string> slotModels;
	// System name -> session id. Host: built at startup; client: rebuilt
	// from the host's table.
	std::map<std::string, uint16_t> systemIdsByName;
	size_t clientTableSize = 0;
	bool sentPlayerInfo = false;
};
