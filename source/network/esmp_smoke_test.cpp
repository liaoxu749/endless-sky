// ESMP transport smoke test: host + client over loopback.
// Verifies: connect, HELLO/WELCOME, system table, input uplink,
// snapshot downlink, RTT ping, and clean disconnect.
#include "network/NetServer.h"
#include "network/NetClient.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using namespace std;

int main()
{
	NetServer server;
	assert(server.Start("TestHost", 4));
	server.SetSystemTable({{1, "Sol"}, {2, "Rigel"}});
	server.SetEntity(0, 1, 100.0, 200.0, 5.0, 0.0, 90.0);

	NetClient client;
	assert(client.Connect("127.0.0.1", "Tester"));

	// Pump both sides for up to ~2 seconds at a fake 60Hz.
	bool haveInput = false;
	for(int i = 0; i < 120 && !(client.IsWelcomed()
		&& client.LatestSnapshot().size() >= 2); ++i)
	{
		server.Tick();
		client.Tick();
		client.SetInput(0x1u, 0x2u, 300.0, 400.0);
		ESMP::InputPacket input;
		if(server.GetInput(1, &input))
		{
			haveInput = true;
			// Host simulates the client's ship from its input, then feeds
			// the authoritative state back into the snapshot.
			server.SetEntity(1, 1, 500.0, 600.0, 0.0, 1.0, 45.0);
		}
		this_thread::sleep_for(chrono::milliseconds(16));
	}

	assert(client.IsWelcomed());
	assert(client.Slot() == 1);
	assert(client.HostName() == "TestHost");
	assert(client.SystemTable().size() == 2);
	assert(haveInput);
	assert(client.LatestSnapshot().size() == 2);

	const auto &snap = client.LatestSnapshot();
	assert(snap[0].slot == 0 && snap[0].systemId == 1);
	assert(snap[0].x == 100.0 && snap[0].y == 200.0);
	assert(snap[1].slot == 1 && snap[1].x == 500.0 && snap[1].angleDeg == 45);

	ESMP::InputPacket input;
	assert(server.GetInput(1, &input));
	assert(input.commands == 0x1u && input.fire == 0x2u);
	assert(input.mouse[0] == 300.0f && input.mouse[1] == 400.0f);
	assert(server.PlayerCount() == 2);

	cout << "RTT: " << client.RttMs() << " ms" << endl;

	// Clean disconnect: the host should drop back to 1 player.
	client.Disconnect();
	for(int i = 0; i < 30; ++i)
	{
		server.Tick();
		this_thread::sleep_for(chrono::milliseconds(10));
	}
	assert(server.PlayerCount() == 1);
	assert(!server.GetInput(1, &input));

	cout << "ESMP transport smoke test PASSED" << endl;
	return 0;
}
