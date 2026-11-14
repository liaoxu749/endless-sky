/* NetSync.cpp — ESMP LAN multiplayer. Engine <-> network glue. */
#include "NetSync.h"

#include "../FireCommand.h"
#include "../GameData.h"
#include "../PlayerInfo.h"
#include "../Ship.h"
#include "../System.h"

#include <algorithm>
#include <set>

using namespace std;


NetSync::~NetSync()
{
	Disconnect();
}

void NetSync::StartHost(PlayerInfo &player)
{
	Disconnect();
	// Assign session ids to every known system; clients translate their
	// local system names into these ids using the table sent after WELCOME.
	vector<pair<uint16_t, string>> table;
	uint16_t id = 1;
	for(const auto &it : GameData::Systems())
	{
		table.emplace_back(id, it.first);
		systemIdsByName[it.first] = id++;
	}
	server.SetSystemTable(std::move(table));
	server.Start(player.FirstName() + " " + player.LastName());
	role = Role::Host;
}

bool NetSync::Join(PlayerInfo &player, const string &hostIp)
{
	Disconnect();
	if(!client.Connect(hostIp, player.FirstName() + " " + player.LastName()))
		return false;
	role = Role::Client;
	sentPlayerInfo = false;
	return true;
}

void NetSync::Disconnect()
{
	if(role == Role::Host)
		server.Stop();
	else if(role == Role::Client)
		client.Disconnect();
	role = Role::None;
	// Remote ships are removed from Engine::ships by the next Step() call.
}


void NetSync::Step(PlayerInfo &player, std::list<std::shared_ptr<Ship>> &ships,
	const Command &activeCommands)
{
	if(role == Role::None)
	{
		if(!remoteShips.empty())
			RemoveAllRemoteShips(ships);
		return;
	}
	if(role == Role::Host)
		TickHost(player, ships, activeCommands);
	else
		TickClient(player, ships, activeCommands);
	Reconcile(ships);
}


void NetSync::TickHost(PlayerInfo &player, std::list<std::shared_ptr<Ship>> &ships,
	const Command &activeCommands)
{
	(void)activeCommands; // the host flies its own ship locally
	server.Tick();

	// Drop ships of clients that disconnected.
	for(auto it = remoteShips.begin(); it != remoteShips.end(); )
	{
		if(it->first != 0 && !server.HasClient(it->first))
		{
			ships.remove(it->second);
			it = remoteShips.erase(it);
		}
		else
			++it;
	}

	const shared_ptr<Ship> &flagship = player.FlagshipPtr();
	const uint16_t systemId = SystemIdOf(
		player.GetSystem() ? player.GetSystem()->TrueName() : "");

	// Publish the host flagship's authoritative state.
	if(flagship)
	{
		if(slotModels[0].empty())
		{
			slotModels[0] = flagship->TrueModelName();
			server.AnnounceHostModel(slotModels[0]);
		}
		server.SetEntity(0, systemId,
			flagship->Position().X(), flagship->Position().Y(),
			flagship->Velocity().X(), flagship->Velocity().Y(),
			flagship->Facing().Degrees());
	}

	// One network-steered ship per connected client.
	for(uint8_t slot = 1; slot <= ESMP::MAX_PLAYERS; ++slot)
	{
		if(!server.HasClient(slot))
			continue;
		const string &model = server.Model(slot);
		if(model.empty())
			continue; // waiting for the client's PLAYER_INFO
		auto found = remoteShips.find(slot);
		if(found == remoteShips.end())
		{
			auto ship = CreateRemoteShip(player, slot, model);
			if(!ship)
				continue;
			ships.push_back(ship);
			remoteShips[slot] = ship;
		}
		shared_ptr<Ship> &ship = remoteShips[slot];

		// Feed the client's input into the host-side simulation.
		ESMP::InputPacket input;
		if(server.GetInput(slot, &input))
		{
			Command commands = Command::FromState(input.commands);
			commands.SetTurn(input.turn);
			ship->SetCommands(commands);
			FireCommand fire;
			const size_t hardpoints = ship->Weapons().size();
			fire.SetHardpoints(hardpoints, 0);
			if(input.fire & 1u)
				for(size_t i = 0; i < hardpoints; ++i)
					fire.SetFire(static_cast<int>(i));
			ship->SetCommands(fire);
		}

		const System *shipSystem = ship->GetSystem();
		const uint16_t shipSystemId = SystemIdOf(
			shipSystem ? shipSystem->TrueName() : "");
		server.SetEntity(slot, shipSystemId,
			ship->Position().X(), ship->Position().Y(),
			ship->Velocity().X(), ship->Velocity().Y(),
			ship->Facing().Degrees());
	}
}


void NetSync::TickClient(PlayerInfo &player, std::list<std::shared_ptr<Ship>> &ships,
	const Command &activeCommands)
{
	client.Tick();
	if(!client.IsConnected())
	{
		role = Role::None; // ship cleanup happens in the next Step()
		return;
	}

	// Translate the host's system ids.
	const auto &table = client.SystemTable();
	if(table.size() != clientTableSize)
	{
		clientTableSize = table.size();
		systemIdsByName.clear();
		for(const auto &entry : table)
			systemIdsByName[entry.second] = entry.first;
	}

	// Announce our flagship model once, after the welcome.
	if(!sentPlayerInfo && client.IsWelcomed() && player.FlagshipPtr())
	{
		client.SendPlayerInfo(player.FlagshipPtr()->TrueModelName());
		sentPlayerInfo = true;
	}
	for(const auto &it : client.PlayerModels())
		slotModels[it.first] = it.second;

	// Input uplink; primary fire is relayed as bit 0 for now.
	if(client.IsWelcomed())
		client.SetInput(activeCommands.State(),
			activeCommands.Has(Command::PRIMARY) ? 1u : 0u,
			activeCommands.Turn(), 0., 0.);

	// Ghost ships driven by the authoritative snapshot.
	const uint16_t mySystemId = SystemIdOf(
		player.GetSystem() ? player.GetSystem()->TrueName() : "");
	set<uint8_t> visibleSlots;
	for(const NetClient::RemoteEntity &entity : client.LatestSnapshot())
	{
		if(entity.slot == client.Slot())
			continue;
		if(entity.systemId != mySystemId)
			continue; // peer is elsewhere; drop the ghost
		visibleSlots.insert(entity.slot);
		auto found = remoteShips.find(entity.slot);
		if(found == remoteShips.end())
		{
			auto modelIt = slotModels.find(entity.slot);
			if(modelIt == slotModels.end() || modelIt->second.empty())
				continue; // model not announced yet
			auto ghost = CreateRemoteShip(player, entity.slot, modelIt->second);
			if(!ghost)
				continue;
			ships.push_back(ghost);
			remoteShips[entity.slot] = ghost;
		}
		// LAN latency is tiny: snap to the authoritative state directly.
		remoteShips[entity.slot]->SetNetworkState(
			Point(entity.x, entity.y), Point(entity.vx, entity.vy),
			Angle(entity.angleDeg));
	}
	// Drop ghosts whose slot vanished or jumped to another system.
	for(auto it = remoteShips.begin(); it != remoteShips.end(); )
	{
		if(!visibleSlots.count(it->first))
		{
			ships.remove(it->second);
			it = remoteShips.erase(it);
		}
		else
			++it;
	}
}


void NetSync::Reconcile(std::list<std::shared_ptr<Ship>> &ships)
{
	// Engine::Place() can rebuild the ships list (e.g. after landing);
	// re-attach our remote ships that survived.
	for(const auto &it : remoteShips)
		if(find(ships.begin(), ships.end(), it.second) == ships.end())
			ships.push_back(it.second);
}

void NetSync::RemoveAllRemoteShips(std::list<std::shared_ptr<Ship>> &ships)
{
	for(const auto &it : remoteShips)
		ships.remove(it.second);
	remoteShips.clear();
	slotModels.clear();
}

shared_ptr<Ship> NetSync::CreateRemoteShip(PlayerInfo &player, uint8_t slot,
	const string &model)
{
	const Ship *modelShip = GameData::Ships().Get(model);
	if(!modelShip)
		return nullptr;
	auto ship = make_shared<Ship>(*modelShip);
	ship->SetGovernment(GameData::PlayerGovernment());
	ship->SetSystem(player.GetSystem());
	ship->SetRemote(true);
	// Spawn near the local flagship, offset by slot to avoid overlap.
	if(player.FlagshipPtr())
		ship->SetNetworkState(player.FlagshipPtr()->Position() + Point(80. * slot, 0.),
			Point(), Angle(0.));
	return ship;
}

uint16_t NetSync::SystemIdOf(const string &name) const
{
	auto found = systemIdsByName.find(name);
	return found != systemIdsByName.end() ? found->second : 0;
}
