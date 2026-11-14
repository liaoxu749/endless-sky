/* VirtualKeyboard.h — ESMP Android port.
A self-drawn touch keyboard that does not rely on the Android IME at all:
tapping a key synthesizes SDL key events (KEYDOWN/KEYUP and, for
printable characters, TEXTINPUT), so any panel that already handles
physical-keyboard input receives it unchanged. See DESIGN.md.
*/
#pragma once

#include "Point.h"

class VirtualKeyboard {
public:
	static bool Visible();
	static void Toggle();
	static void Hide();

	// Draw the keyboard (anchor: bottom of the screen).
	static void Draw();

	// Route a click; synthesizes SDL events for the pressed key.
	// Returns true if the click landed on a key.
	static bool Click(const Point &p);
};
