/* Edit.cpp
Copyright (c) 2026 by thewierdnut

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "Edit.h"

#include "Color.h"
#include "Command.h"
#include "shader/FillShader.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "GameData.h"
#include "Rectangle.h"
#include "Screen.h"
#include "UI.h"

#include "SDL.h"

#include <cassert>
#include <cctype>

using namespace std;


namespace {
	// ESMP: self-drawn touch keyboard (no dependency on the Android IME).
	bool vkVisible = false;
	bool vkShift = false;
	bool vkDigits = false;

	struct VkKey {
		std::string label;
		int kind; // 0 char, 1 shift, 2 back, 3 page, 4 hide, 5 space, -1 spacer
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

		const std::string shiftLabel = vkShift ? "SHIFT" : "shift";
		if(!vkDigits)
		{
			addRow({{"123", 3, ' ', 2.}, {"space", 5, ' ', 6.}, {"hide", 4, ' ', 2.}},
				bottom - kh);
			addRow({{shiftLabel, 1, ' ', 2.}, {"z", 0, 'z', 1.}, {"x", 0, 'x', 1.},
				{"c", 0, 'c', 1.}, {"v", 0, 'v', 1.}, {"b", 0, 'b', 1.},
				{"n", 0, 'n', 1.}, {"m", 0, 'm', 1.}, {"back", 2, ' ', 2.}},
				bottom - 2 * kh - gap);
			addRow({{"", -1, ' ', .5}, {"a", 0, 'a', 1.}, {"s", 0, 's', 1.},
				{"d", 0, 'd', 1.}, {"f", 0, 'f', 1.}, {"g", 0, 'g', 1.},
				{"h", 0, 'h', 1.}, {"j", 0, 'j', 1.}, {"k", 0, 'k', 1.},
				{"l", 0, 'l', 1.}, {"", -1, ' ', .5}},
				bottom - 3 * kh - 2 * gap);
			addRow({{"q", 0, 'q', 1.}, {"w", 0, 'w', 1.}, {"e", 0, 'e', 1.},
				{"r", 0, 'r', 1.}, {"t", 0, 't', 1.}, {"y", 0, 'y', 1.},
				{"u", 0, 'u', 1.}, {"i", 0, 'i', 1.}, {"o", 0, 'o', 1.},
				{"p", 0, 'p', 1.}},
				bottom - 4 * kh - 3 * gap);
		}
		else
		{
			addRow({{"abc", 3, ' ', 2.}, {"space", 5, ' ', 6.}, {"hide", 4, ' ', 2.}},
				bottom - kh);
			addRow({{".", 0, '.', 1.}, {",", 0, ',', 1.}, {"-", 0, '-', 1.},
				{"'", 0, 0x27, 1.}, {"!", 0, '!', 1.}, {"?", 0, '?', 1.},
				{"(", 0, '(', 1.}, {")", 0, ')', 1.}, {"@", 0, '@', 1.}},
				bottom - 2 * kh - gap);
			addRow({{"1", 0, '1', 1.}, {"2", 0, '2', 1.}, {"3", 0, '3', 1.},
				{"4", 0, '4', 1.}, {"5", 0, '5', 1.}, {"6", 0, '6', 1.},
				{"7", 0, '7', 1.}, {"8", 0, '8', 1.}, {"9", 0, '9', 1.},
				{"0", 0, '0', 1.}},
				bottom - 3 * kh - 2 * gap);
		}
		return keys;
	}

	void DrawVirtualKeyboard()
	{
		const Font &font = FontSet::Get(16);
		for(const VkKey &key : VkKeys())
		{
			if(key.kind < 0)
				continue;
			const bool active = (key.kind == 1 && vkShift) || (key.kind == 3 && vkDigits);
			FillShader::Fill(key.rect, *GameData::Colors().Get(active ? "drag select" : "dark"));
			const double labelWidth = font.Width(key.label);
			font.Draw(key.label, key.rect.Center() - Point(labelWidth / 2., 9.),
				*GameData::Colors().Get("medium"));
		}
	}
}

namespace {
	constexpr size_t INVALID_POS = -1;
}



Edit::Edit()
{
	Clear();
	caret.SetHeight(fontSize);
}



void Edit::SetFontSize(int f)
{
	if(f != fontSize)
	{
		fontSize = f;
		caret.SetHeight(fontSize);
		ComputeTextBounds();
	}
}



void Edit::SetPosition(const Rectangle &p)
{
	if(p != position)
	{
		position = p;
		ComputeTextBounds();
	}
}



const string &Edit::Text() const
{
	assert(historyPos > 0);
	return textHistory[historyPos - 1].first;
}



void Edit::SetText(const string &s)
{
	Clear();
	UpdateText(s, s.size());
}



void Edit::Clear()
{
	historyPos = 0;
	textHistory.clear();
	UpdateText("", 0); // Make sure the history is not empty.
	caretPos = 0;
	highlightPos = INVALID_POS;
}



void Edit::Draw()
{
	if(!visible)
		return;
#ifdef __ANDROID__
	if(HasFocus() && vkVisible)
		DrawVirtualKeyboard();
#endif

#ifdef __ANDROID__
	// ESMP: manual soft-keyboard button, shown while the field is focused.
	if(HasFocus())
	{
		const Rectangle &button = KbButtonRect();
		FillShader::Fill(button, *GameData::Colors().Get("dark"));
		const Font &font = FontSet::Get(fontSize);
		font.Draw("KB", button.Center() - Point(font.Width("KB") / 2., 8.),
			*GameData::Colors().Get("medium"));
	}
#endif

	// const Font &font = FontSet::Get(14);
	// const Color &bright = *GameData::Colors().Get("bright");
	// const Color &dim = *GameData::Colors().Get("medium");
	if(bgColor == Color())
	{
		bgColor = *GameData::Colors().Get("panel background");
	}
	const Font &font = FontSet::Get(fontSize);
	const Color &hover = *GameData::Colors().Get("hover");
	const Color &active = *GameData::Colors().Get("active");
	const Color &inactive = *GameData::Colors().Get("inactive");

	FillShader::Fill(position.Center(), position.Dimensions(), bgColor);

	if(HasFocus())
	{
		caret.Draw();

		if(highlightPos != INVALID_POS)
		{
			Color highlightColor = active.Transparent(.25);

			FillShader::Fill(highlight, highlightColor);
		}
	}

	font.Draw(Text(),
		AlignedOffset(0),
		isActive ? (isHover ? hover : active) : inactive);
}



void Edit::SetPadding(int p)
{
	leftPadding = p;
	rightPadding = p;
	topPadding = p;
	bottomPadding = p;
	ComputeTextBounds();
}



void Edit::SetLeftPadding(int p)
{
	leftPadding = p;
	ComputeTextBounds();
}



void Edit::SetRightPadding(int p)
{
	rightPadding = p;
	ComputeTextBounds();
}



void Edit::SetTopPadding(int p)
{
	topPadding = p;
	ComputeTextBounds();
}



void Edit::SetBottomPadding(int p)
{
	bottomPadding = p;
	ComputeTextBounds();
}



bool Edit::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress)
{
	if(!isEditable)
		return false;

	// Force the caret blink cycle to on after any keypress.
	caret.BlinkOn();

	bool shift = (mod & KMOD_SHIFT) != 0;
	bool ctrl = (mod & KMOD_CTRL) != 0;

	switch(key)
	{
	case SDLK_TAB:
		if(shift)
			FocusPrev();
		else
			FocusNext();
		break;
	case SDLK_HOME:
		MoveCaret(0);
		break;
	case SDLK_END:
		MoveCaret(Text().size());
		break;
	case SDLK_LEFT:
		if(caretPos > 0)
			MoveCaret(caretPos - 1);
		break;
	case SDLK_RIGHT:
		if(caretPos < Text().size())
			MoveCaret(caretPos + 1);
		break;
	case SDLK_BACKSPACE:
		if(highlightPos != INVALID_POS)
		{
			int left = highlightPos;
			int right = caretPos;
			if(left > right)
				swap(left, right);
			UpdateText(Text().substr(0, left) + Text().substr(right), left);
			highlightPos = INVALID_POS;
		}
		else if(caretPos > 0)
		{
			UpdateText(Text().substr(0, caretPos - 1) + Text().substr(caretPos), caretPos - 1);
		}
		break;
	case SDLK_DELETE:
		if(highlightPos != INVALID_POS)
		{
			int left = highlightPos;
			int right = caretPos;
			if(left > right)
				swap(left, right);
			if(shift)
			{
				// shift-del, treat as cut
				SDL_SetClipboardText(Text().substr(left, right - left).c_str());
			}
			UpdateText(Text().substr(0, left) + Text().substr(right), left);
			highlightPos = INVALID_POS;
		}
		else if(caretPos < Text().size())
		{
			UpdateText(Text().substr(0, caretPos) + Text().substr(caretPos + 1), caretPos);
		}
		break;
	case SDLK_c:
		if(ctrl)
		{
			// ctrl-c copy
			Copy();
			break;
		}
		return false;
		break;
	case SDLK_x:
		if(ctrl)
		{
			// ctrl-x cut
			Cut();
			break;
		}
		return false;
		break;
	case SDLK_v:
		if(ctrl)
		{
			// ctrl-v paste
			Paste();
			break;
		}
		return false;
		break;
	case SDLK_z:
		if(ctrl)
		{
			if(shift)
			{
					// ctrl-shift-z Redo
					Redo();
			}
			else
			{
					// ctrl-z undo
					Undo();
			}
			break;
		}
		return false;
		break;
	case SDLK_INSERT:
		if(shift)
		{
			// shift-insert, paste
			Paste();
			break;
		}
		else if(ctrl)
		{
			// ctrl-insert, copy
			Copy();
			break;
		}
		return false;
		break;
	case SDLK_AC_BACK:
	case SDLK_ESCAPE:
		SetFocus(false);
		break;
	default:
		return false;
	}

	return true;
}



bool Edit::Click(int x, int y, MouseButton button, int clicks)
{
	if(button != MouseButton::LEFT)
		return false;
#ifdef __ANDROID__
	if(HasFocus())
	{
		if(HandleVirtualKeyboard(Point(x, y)))
			return true;
		if(KbButtonRect().Contains(Point(x, y)))
		{
			ToggleVirtualKeyboard();
			return true;
		}
	}
#endif
	if(!position.Contains(Point(x, y)))
	{
		SetFocus(false);
		vkVisible = false;
		return false;
	}

	dragPos = Point(x, y);

	SetFocus(true);

	const string &s = Text();
	size_t newPos = 0;
	for( ; newPos < s.size(); ++newPos)
	{
		if(AlignedOffset(newPos + 1).X() > x)
			break;
	}

	MoveCaret(newPos);

	return true;
}



bool Edit::Drag(double dx, double dy)
{
	if(dragPos != Point())
	{
		if(highlightPos == INVALID_POS)
			highlightPos = caretPos;
		dragPos.X() += dx;
		dragPos.Y() += dy;

		const string &s = Text();
		size_t newPos = 0;
		for( ; newPos < s.size(); ++newPos)
		{
			if(AlignedOffset(newPos + 1).X() > dragPos.X())
				break;
		}

		UpdateHighlightRect(newPos, highlightPos);

		UpdateCaret(newPos);
		return true;
	}
	return false;
}



bool Edit::Release(int x, int y, MouseButton button)
{
	dragPos = Point();
	return false;
}



bool Edit::OnFocus(bool f)
{
	if(f)
	{
		if(!isEditable)
			return false;
#ifdef ES_USE_SDL3
		SDL_StartTextInput(SDL_GetKeyboardFocus());
#else
		// ESMP/android: SDL's text input is ACTIVE by default, so a plain
		// SDL_StartTextInput() is a no-op there and the soft keyboard never
		// shows. Force an inactive -> active transition (that transition is
		// what makes SDL's Android backend pop the keyboard).
		const Point topLeft = Screen::TopLeft();
		const Rectangle &fieldRect = position;
		SDL_Rect inputRect{
			static_cast<int>(fieldRect.Left() + topLeft.X()),
			static_cast<int>(fieldRect.Top() + topLeft.Y()),
			static_cast<int>(fieldRect.Width()),
			static_cast<int>(fieldRect.Height())};
		SDL_StopTextInput();
		SDL_SetTextInputRect(&inputRect);
		SDL_StartTextInput();
#endif
		highlightPos = INVALID_POS;
		UpdateCaret(textHistory.back().second);
	}
	else
	{
#ifdef ES_USE_SDL3
		SDL_StopTextInput(SDL_GetKeyboardFocus());
#else
		SDL_StopTextInput();
		vkVisible = false;
#endif
	}
	return true;
}



#ifdef __ANDROID__
Rectangle Edit::KbButtonRect() const
{
	// A small strip centered under the text field.
	return Rectangle(Point(position.Left() + position.Width() / 2.,
		position.Bottom() + 26.), Point(120., 44.));
}

bool Edit::VirtualKeyboardVisible()
{
	return vkVisible;
}

void Edit::ToggleVirtualKeyboard()
{
	vkVisible = !vkVisible;
}

bool Edit::HandleVirtualKeyboard(const Point &p)
{
	if(!vkVisible)
		return false;
	for(const VkKey &key : VkKeys())
	{
		if(!key.rect.Contains(p))
			continue;
		switch(key.kind)
		{
			case 0:
			{
				char c = key.ch;
				if(vkShift && !vkDigits)
				{
					c = static_cast<char>(toupper(c));
					vkShift = false;
				}
				TextInput(std::string(1, c));
				break;
			}
			case 1:
				vkShift = !vkShift;
				break;
			case 2:
				KeyDown(SDLK_BACKSPACE, 0, Command(), true);
				break;
			case 3:
				vkDigits = !vkDigits;
				break;
			case 4:
				vkVisible = false;
				break;
			case 5:
				TextInput(" ");
				break;
			default:
				break;
		}
		return true;
	}
	return false;
}
#endif



bool Edit::TextInput(const string &s)
{
	if(!HasFocus())
		return false;

	string newText;
	size_t newPos;
	if(highlightPos != INVALID_POS)
	{
		int left = highlightPos;
		int right = caretPos;
		if(left > right)
			swap(left, right);

		newText = Text().substr(0, left) + s + Text().substr(right);
		newPos = left + s.size();

		highlightPos = INVALID_POS;
	}
	else
	{
		newText = Text().substr(0, caretPos) + s + Text().substr(caretPos);
		newPos = caretPos + s.size();
	}
	// Someday, we should make the text scroll and clip if we type something too
	// long, but for now, just ignore input if it is too big to fit.
	auto &font = FontSet::Get(fontSize);
	if(font.Width(newText) > textBounds.Width())
		return true;

	UpdateText(newText, newPos);
	return true;
}



void Edit::MoveCaret(size_t pos)
{
	assert(pos <= Text().size());
	if(pos > Text().size())
		pos = Text().size();

	auto mod = SDL_GetModState();
	bool shift = (mod & KMOD_SHIFT) != 0;

	if(highlightPos == INVALID_POS && shift)
		highlightPos = caretPos;
	else if(highlightPos != INVALID_POS && !shift)
		highlightPos = INVALID_POS;
	if(highlightPos != INVALID_POS)
		UpdateHighlightRect(pos, highlightPos);

	UpdateCaret(pos);
}



void Edit::UpdateCaret(size_t pos)
{
	caret.SetX(AlignedOffset(pos).X());
	caretPos = pos;
}



void Edit::UpdateText(const string &text, size_t caretPos)
{
	string mutableText = text;
	if(changedCallback)
	{
		if(changedCallback(mutableText))
		{
			// The callback accepted the change, but may have modified it.
			if(caretPos > text.size())
				caretPos = text.size();
		}
		else
		{
			// Callback rejected the change.
			return;
		}
	}


	textHistory.resize(historyPos++);
	textHistory.emplace_back(mutableText, caretPos);
	UpdateCaret(caretPos);
}



void Edit::Cut()
{
	if(highlightPos == INVALID_POS)
	{
		SDL_SetClipboardText(Text().c_str());
		UpdateText("", 0);
	}
	else
	{
		int left = highlightPos;
		int right = caretPos;
		if(left > right)
			swap(left, right);
		SDL_SetClipboardText(Text().substr(left, right - left).c_str());
		UpdateText(Text().substr(0, left) + Text().substr(right), left);
	}
	highlightPos = INVALID_POS;
}



void Edit::Copy() const
{
	if(highlightPos == INVALID_POS)
		SDL_SetClipboardText(Text().c_str());
	else
	{
		int left = highlightPos;
		int right = caretPos;
		if(left > right)
			swap(left, right);
		SDL_SetClipboardText(Text().substr(left, right - left).c_str());
	}
}



void Edit::Paste()
{
	shared_ptr<const char> text(SDL_GetClipboardText(), SDL_free);
	if(highlightPos == INVALID_POS)
	{
		// insert text at caret position
		UpdateText(
			Text().substr(0, caretPos) + text.get() + Text().substr(caretPos),
			caretPos + strlen(text.get())
		);
	}
	else
	{
		// replace highlighted text
		int left = highlightPos;
		int right = caretPos;
		if(left > right)
			swap(left, right);
		UpdateText(
			Text().substr(0, left) + text.get() + Text().substr(right),
			left + strlen(text.get())
		);
	}

	highlightPos = INVALID_POS;
}



void Edit::Undo()
{
	// The first entry is the initial string set by the
	// constructor. Don't go past it.
	if(historyPos > 1)
	{
		--historyPos;
		UpdateCaret(textHistory[historyPos - 1].second);
	}
}



void Edit::Redo()
{
	if(historyPos < textHistory.size())
	{
		++historyPos;
		UpdateCaret(textHistory[historyPos - 1].second);
	}
}


Point Edit::AlignedOffset(size_t offset)
{
	const Font &font = FontSet::Get(fontSize);
	const string &s = Text();
	Point ret{};
	switch(alignment)
	{
	case Edit::LEFT:
		ret = Point(textBounds.Left(), textBounds.Center().Y() - font.Height() / 2.0);
		break;
	case Edit::RIGHT:
		ret = Point(textBounds.Right() - font.Width(s), textBounds.Center().Y() - font.Height() / 2.0);
		break;
	default: // CENTER
		ret = textBounds.Center() - Point(font.Width(s) / 2.0, font.Height() / 2.0);
		break;
	}

	if(offset)
		ret.X() += font.Width(s.substr(0, offset));
	return ret;
}



void Edit::UpdateHighlightRect(size_t o1, size_t o2)
{
	assert(o1 != INVALID_POS);
	assert(o2 != INVALID_POS);
	int highlightLeft = AlignedOffset(o1).X();
	int highlightRight = AlignedOffset(o2).X();
	if(highlightLeft > highlightRight)
		swap(highlightLeft, highlightRight);
	Point tl(highlightLeft, textBounds.Center().Y() - fontSize / 2.0);
	Point br = tl;
	br.X() = highlightRight;
	br.Y() += fontSize;
	highlight = Rectangle::WithCorners(tl, br);
}



void Edit::ComputeTextBounds()
{
	textBounds = Rectangle::WithCorners(
		position.TopLeft() + Point(leftPadding, topPadding),
		position.BottomRight() - Point(rightPadding, bottomPadding)
	);
	caret.SetY(textBounds.Center().Y() - fontSize / 2.0);
}
