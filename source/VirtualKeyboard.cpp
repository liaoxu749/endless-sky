/* VirtualKeyboard.cpp — ESMP Android port.
Self-drawn QWERTY touch keyboard. Key presses are delivered as synthesized
SDL events (KEYDOWN/KEYUP plus TEXTINPUT for printable characters), which
reaches every panel through the normal event loop — Edit fields via
TextInput, ConversationPanel via KeyDown.
*/
#include "VirtualKeyboard.h"

#include "Color.h"
#include "Rectangle.h"
#include "GameData.h"
#include "SDL.h"
#include "Screen.h"
#include "shader/FillShader.h"
#include "text/Font.h"
#include "text/FontSet.h"

#include <array>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

using namespace std;

namespace {
	bool vkVisible = false;
	bool vkShift = false;
	bool vkDigits = false;

	constexpr int KIND_CHAR = 0;
	constexpr int KIND_SHIFT = 1;
	constexpr int KIND_BACK = 2;
	constexpr int KIND_PAGE = 3;
	constexpr int KIND_HIDE = 4;
	constexpr int KIND_SPACE = 5;
	constexpr int KIND_SPACER = -1;

	struct VkKey {
		string label;
		int kind;
		char ch;
		double wf;
		Rectangle rect;
	};

	vector<VkKey> VkKeys()
	{
		vector<VkKey> keys;
		const double kh = min(110., Screen::Height() / 10.);
		const double margin = 6.;
		const double gap = 6.;
		const double bottom = Screen::Bottom() - margin;

		auto addRow = [&](std::initializer_list<VkKey> items, double y)
		{
			double total = 0.;
			for(const VkKey &k : items)
				total += k.wf;
			const double avail = Screen::Width() - 2 * margin - (items.size() - 1) * gap;
			double x = margin;
			for(const VkKey &k : items)
			{
				VkKey key = k;
				const double w = avail * key.wf / total;
				key.rect = Rectangle(Point(x + w / 2., y + kh / 2.), Point(w, kh));
				keys.push_back(std::move(key));
				x += w + gap;
			}
		};

		const string shiftLabel = vkShift ? "SHIFT" : "shift";
		auto lettersRow = [&](const char *chars, double y)
		{
			std::initializer_list<VkKey> items = {
				{"", KIND_SPACER, ' ', .5},
				{std::string(1, chars[0]), KIND_CHAR, chars[0], 1.},
				{std::string(1, chars[1]), KIND_CHAR, chars[1], 1.},
				{std::string(1, chars[2]), KIND_CHAR, chars[2], 1.},
				{std::string(1, chars[3]), KIND_CHAR, chars[3], 1.},
				{std::string(1, chars[4]), KIND_CHAR, chars[4], 1.},
				{std::string(1, chars[5]), KIND_CHAR, chars[5], 1.},
				{std::string(1, chars[6]), KIND_CHAR, chars[6], 1.},
				{std::string(1, chars[7]), KIND_CHAR, chars[7], 1.},
				{chars[8] ? std::string(1, chars[8]) : "", chars[8] ? KIND_CHAR : KIND_SPACER,
					chars[8] ? chars[8] : ' ', chars[8] ? 1. : .5},
				{"", KIND_SPACER, ' ', .5},
			};
			addRow(items, y);
		};

		if(!vkDigits)
		{
			addRow({{"123", KIND_PAGE, ' ', 2.}, {"space", KIND_SPACE, ' ', 6.},
				{"hide", KIND_HIDE, ' ', 2.}}, bottom - kh);
			addRow({{shiftLabel, KIND_SHIFT, ' ', 2.}, {"z", KIND_CHAR, 'z', 1.},
				{"x", KIND_CHAR, 'x', 1.}, {"c", KIND_CHAR, 'c', 1.},
				{"v", KIND_CHAR, 'v', 1.}, {"b", KIND_CHAR, 'b', 1.},
				{"n", KIND_CHAR, 'n', 1.}, {"m", KIND_CHAR, 'm', 1.},
				{"back", KIND_BACK, ' ', 2.}}, bottom - 2 * kh - gap);
			lettersRow("asdfghjkl", bottom - 3 * kh - 2 * gap);
			lettersRow("qwertyuiop", bottom - 4 * kh - 3 * gap);
		}
		else
		{
			addRow({{"abc", KIND_PAGE, ' ', 2.}, {"space", KIND_SPACE, ' ', 6.},
				{"hide", KIND_HIDE, ' ', 2.}}, bottom - kh);
			addRow({{".", KIND_CHAR, '.', 1.}, {",", KIND_CHAR, ',', 1.},
				{"-", KIND_CHAR, '-', 1.}, {"'", KIND_CHAR, 0x27, 1.},
				{"!", KIND_CHAR, '!', 1.}, {"?", KIND_CHAR, '?', 1.},
				{"(", KIND_CHAR, '(', 1.}, {")", KIND_CHAR, ')', 1.},
				{"@", KIND_CHAR, '@', 1.}}, bottom - 2 * kh - gap);
			addRow({{"1", KIND_CHAR, '1', 1.}, {"2", KIND_CHAR, '2', 1.},
				{"3", KIND_CHAR, '3', 1.}, {"4", KIND_CHAR, '4', 1.},
				{"5", KIND_CHAR, '5', 1.}, {"6", KIND_CHAR, '6', 1.},
				{"7", KIND_CHAR, '7', 1.}, {"8", KIND_CHAR, '8', 1.},
				{"9", KIND_CHAR, '9', 1.}, {"0", KIND_CHAR, '0', 1.}},
				bottom - 3 * kh - 2 * gap);
		}
		return keys;
	}

	void PushKey(SDL_Keycode key, Uint16 mod)
	{
		SDL_Event down{};
		down.type = SDL_KEYDOWN;
		down.key.keysym.sym = key;
		down.key.keysym.mod = mod;
		down.key.state = SDL_PRESSED;
		down.key.repeat = 0;
		SDL_PushEvent(&down);
		SDL_Event up = down;
		up.type = SDL_KEYUP;
		up.key.state = SDL_RELEASED;
		SDL_PushEvent(&up);
	}

	void PushText(const char *text)
	{
		SDL_Event ev{};
		ev.type = SDL_TEXTINPUT;
		std::memcpy(ev.text.text, text, std::min(std::strlen(text) + 1, sizeof(ev.text.text)));
		SDL_PushEvent(&ev);
	}
}

bool VirtualKeyboard::Visible()
{
	return vkVisible;
}

void VirtualKeyboard::Toggle()
{
	vkVisible = !vkVisible;
}

void VirtualKeyboard::Hide()
{
	vkVisible = false;
}

void VirtualKeyboard::Draw()
{
	const Font &font = FontSet::Get(16);
	for(const VkKey &key : VkKeys())
	{
		if(key.kind == KIND_SPACER)
			continue;
		const bool active = (key.kind == KIND_SHIFT && vkShift)
			|| (key.kind == KIND_PAGE && vkDigits);
		FillShader::Fill(key.rect, *GameData::Colors().Get(active ? "drag select" : "dark"));
		const double labelWidth = font.Width(key.label);
		font.Draw(key.label, key.rect.Center() - Point(labelWidth / 2., 9.),
			*GameData::Colors().Get("medium"));
	}
}

bool VirtualKeyboard::Click(const Point &p)
{
	for(const VkKey &key : VkKeys())
	{
		if(!key.rect.Contains(p))
			continue;
		switch(key.kind)
		{
			case KIND_CHAR:
			{
				char c = key.ch;
				if(vkShift && !vkDigits)
				{
					c = static_cast<char>(toupper(c));
					vkShift = false;
				}
				PushKey(static_cast<SDL_Keycode>(c),
					vkShift ? KMOD_SHIFT : KMOD_NONE);
				PushText(string(1, c).c_str());
				break;
			}
			case KIND_SHIFT:
				vkShift = !vkShift;
				break;
			case KIND_BACK:
				PushKey(SDLK_BACKSPACE, KMOD_NONE);
				break;
			case KIND_PAGE:
				vkDigits = !vkDigits;
				break;
			case KIND_HIDE:
				vkVisible = false;
				break;
			case KIND_SPACE:
				PushKey(SDLK_SPACE, KMOD_NONE);
				PushText(" ");
				break;
			default:
				break;
		}
		return true;
	}
	return false;
}
