/* LobbyPanel.cpp — ESMP LAN multiplayer. */
#include "LobbyPanel.h"

#include "Color.h"
#include "Command.h"
#include "Engine.h"
#include "GameData.h"
#include "MainPanel.h"
#include "PlayerInfo.h"
#include "SDL.h"
#include "network/NetClient.h"
#include "network/NetSync.h"
#include "shader/FillShader.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "UI.h"

#include <utility>

using namespace std;

namespace {
	const double MARGIN = 60.;
	const double TOP = 40.;
	const double LINE_HEIGHT = 24.;
}


LobbyPanel::LobbyPanel(PlayerInfo &player, UI &gamePanels)
	: player(player), gamePanels(gamePanels)
{
	SetIsFullScreen(true);
}



MainPanel *LobbyPanel::Main() const
{
	return static_cast<MainPanel *>(gamePanels.Root().get());
}



void LobbyPanel::Step()
{
	if(discoveryCooldown-- <= 0)
	{
		discoveryCooldown = 30; // discover in short bursts, twice a second
		for(auto &found : NetClient::Discover(0.15f))
		{
			bool known = false;
			for(const HostEntry &host : hosts)
				if(host.ip == found.ip)
					known = true;
			if(known)
				continue;
			HostEntry host;
			host.ip = found.ip;
			host.name = found.hostName;
			host.players = found.players;
			host.slots = found.slots;
			hosts.push_back(std::move(host));
		}
	}
	if(hosts.empty())
		selected = 0;
	else if(selected >= static_cast<int>(hosts.size()))
		selected = static_cast<int>(hosts.size()) - 1;
}



bool LobbyPanel::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command,
	bool isNewPress)
{
	MainPanel *main = Main();
	if(!main)
		return false;

	if(key == 'b' || key == 'B')
	{
		if(!player.IsLoaded())
		{
			status = "Load a pilot first, then host a game.";
			return true;
		}
		main->GetEngine().GetNet().StartHost(player);
		GetUI().Pop(this);
		return true;
	}
	else if(key == 'j' || key == 'J')
	{
		if(selected < 0 || selected >= static_cast<int>(hosts.size()))
			return true;
		if(!player.IsLoaded())
		{
			status = "Load a pilot first, then join a game.";
			return true;
		}
		const HostEntry &host = hosts[selected];
		if(!main->GetEngine().GetNet().Join(player, host.ip))
		{
			status = "Could not connect to " + host.ip;
			return true;
		}
		status.clear();
		GetUI().Pop(this);
		return true;
	}
	else if(key >= '1' && key <= '8')
	{
		int index = key - '1';
		if(index < static_cast<int>(hosts.size()))
			selected = index;
		return true;
	}
	else if(key == SDLK_UP && selected > 0)
	{
		--selected;
		return true;
	}
	else if(key == SDLK_DOWN && selected + 1 < static_cast<int>(hosts.size()))
	{
		++selected;
		return true;
	}
	else if(key == 'r' || key == 'R')
	{
		hosts.clear();
		discoveryCooldown = 0;
		return true;
	}
	else if(key == 'x' || key == SDLK_ESCAPE)
	{
		GetUI().Pop(this);
		return true;
	}

	return false;
}



void LobbyPanel::Draw()
{
	DrawBackdrop();

	const Font &font = FontSet::Get(14);
	const Color &bright = *GameData::Colors().Get("bright");
	const Color &medium = *GameData::Colors().Get("medium");
	const Color &dark = *GameData::Colors().Get("dark");

	Point pos(MARGIN, TOP);
	font.Draw("ESMP LAN lobby", pos, bright);
	pos.Y() += LINE_HEIGHT * 2.;

	font.Draw("Hosts on this network:", pos, medium);
	pos.Y() += LINE_HEIGHT;
	int index = 0;
	for(const HostEntry &host : hosts)
	{
		const Color &color = (index == selected) ? bright : medium;
		string line = (index == selected) ? "> " : "   ";
		line += to_string(index + 1) + ".  " + host.name
			+ "  [" + host.ip + "]  "
			+ to_string(host.players) + "/" + to_string(host.slots) + " players";
		font.Draw(line, pos, color);
		pos.Y() += LINE_HEIGHT;
		++index;
	}
	if(hosts.empty())
	{
		font.Draw("(no hosts found yet - waiting for beacons, R to refresh)", pos, dark);
		pos.Y() += LINE_HEIGHT;
	}
	pos.Y() += LINE_HEIGHT;

	font.Draw("B = host a game    J = join selected    up/down or 1-8 = select", pos, medium);
	pos.Y() += LINE_HEIGHT;
	font.Draw("R = refresh    X = back", pos, medium);

	if(!status.empty())
	{
		pos.Y() += LINE_HEIGHT * 1.5;
		font.Draw(status, pos, bright);
	}
}
