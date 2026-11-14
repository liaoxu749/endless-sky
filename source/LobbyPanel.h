/* LobbyPanel.h — ESMP LAN multiplayer.
LAN lobby panel: lists discovered hosts, starts a host session or joins
one. Keyboard driven: B = host, J or 1-8 = join, R = refresh, X = back.
See DESIGN.md.
*/
#pragma once

#include "Panel.h"

#include <string>
#include <vector>

class PlayerInfo;
class UI;


class LobbyPanel : public Panel {
public:
	LobbyPanel(PlayerInfo &player, UI &gamePanels);

	// Poll LAN discovery in short bursts.
	void Step() override;
	void Draw() override;


protected:
	bool KeyDown(SDL_Keycode key, Uint16 mod, const Command &command,
		bool isNewPress) override;


private:
	struct HostEntry {
		std::string ip;
		std::string name;
		int players = 0;
		int slots = 0;
	};

	// Returns the MainPanel backing the game (created by MenuPanel), or
	// nullptr if it is missing.
	class MainPanel *Main() const;

	PlayerInfo &player;
	UI &gamePanels;

	std::vector<HostEntry> hosts;
	int selected = 0;
	int discoveryCooldown = 0;
	std::string status;
};
