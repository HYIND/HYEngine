#include "Input/EngineKey.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <string>

EngineKey EngineKeyFromVirtualKey(int virtualKey)
{
	switch (virtualKey)
	{
	case 'A': return EngineKey::A;
	case 'B': return EngineKey::B;
	case 'C': return EngineKey::C;
	case 'D': return EngineKey::D;
	case 'E': return EngineKey::E;
	case 'F': return EngineKey::F;
	case 'G': return EngineKey::G;
	case 'H': return EngineKey::H;
	case 'I': return EngineKey::I;
	case 'J': return EngineKey::J;
	case 'K': return EngineKey::K;
	case 'L': return EngineKey::L;
	case 'M': return EngineKey::M;
	case 'N': return EngineKey::N;
	case 'O': return EngineKey::O;
	case 'P': return EngineKey::P;
	case 'Q': return EngineKey::Q;
	case 'R': return EngineKey::R;
	case 'S': return EngineKey::S;
	case 'T': return EngineKey::T;
	case 'U': return EngineKey::U;
	case 'V': return EngineKey::V;
	case 'W': return EngineKey::W;
	case 'X': return EngineKey::X;
	case 'Y': return EngineKey::Y;
	case 'Z': return EngineKey::Z;

	case '0': return EngineKey::Num0;
	case '1': return EngineKey::Num1;
	case '2': return EngineKey::Num2;
	case '3': return EngineKey::Num3;
	case '4': return EngineKey::Num4;
	case '5': return EngineKey::Num5;
	case '6': return EngineKey::Num6;
	case '7': return EngineKey::Num7;
	case '8': return EngineKey::Num8;
	case '9': return EngineKey::Num9;

	case VK_F1: return EngineKey::F1;
	case VK_F2: return EngineKey::F2;
	case VK_F3: return EngineKey::F3;
	case VK_F4: return EngineKey::F4;
	case VK_F5: return EngineKey::F5;
	case VK_F6: return EngineKey::F6;
	case VK_F7: return EngineKey::F7;
	case VK_F8: return EngineKey::F8;
	case VK_F9: return EngineKey::F9;
	case VK_F10: return EngineKey::F10;
	case VK_F11: return EngineKey::F11;
	case VK_F12: return EngineKey::F12;

	case VK_ESCAPE: return EngineKey::Escape;
	case VK_TAB: return EngineKey::Tab;
	case VK_CAPITAL: return EngineKey::CapsLock;
	case VK_SPACE: return EngineKey::Space;
	case VK_RETURN: return EngineKey::Enter;
	case VK_BACK: return EngineKey::Backspace;
	case VK_DELETE: return EngineKey::Delete;
	case VK_INSERT: return EngineKey::Insert;

	case VK_HOME: return EngineKey::Home;
	case VK_END: return EngineKey::End;
	case VK_PRIOR: return EngineKey::PageUp;
	case VK_NEXT: return EngineKey::PageDown;

	case VK_UP: return EngineKey::ArrowUp;
	case VK_DOWN: return EngineKey::ArrowDown;
	case VK_LEFT: return EngineKey::ArrowLeft;
	case VK_RIGHT: return EngineKey::ArrowRight;

	case VK_LSHIFT: return EngineKey::LeftShift;
	case VK_RSHIFT: return EngineKey::RightShift;
	case VK_LCONTROL: return EngineKey::LeftCtrl;
	case VK_RCONTROL: return EngineKey::RightCtrl;
	case VK_LMENU: return EngineKey::LeftAlt;
	case VK_RMENU: return EngineKey::RightAlt;

	case VK_LBUTTON: return EngineKey::MouseLeft;
	case VK_RBUTTON: return EngineKey::MouseRight;
	case VK_MBUTTON: return EngineKey::MouseMiddle;
	case VK_XBUTTON1: return EngineKey::MouseX1;
	case VK_XBUTTON2: return EngineKey::MouseX2;

	case VK_OEM_MINUS: return EngineKey::Minus;
	case VK_OEM_PLUS: return EngineKey::Equal;
	case VK_OEM_4: return EngineKey::LeftBracket;
	case VK_OEM_6: return EngineKey::RightBracket;
	case VK_OEM_5: return EngineKey::Backslash;
	case VK_OEM_1: return EngineKey::Semicolon;
	case VK_OEM_7: return EngineKey::Quote;
	case VK_OEM_COMMA: return EngineKey::Comma;
	case VK_OEM_PERIOD: return EngineKey::Period;
	case VK_OEM_2: return EngineKey::Slash;
	case VK_OEM_3: return EngineKey::Grave;

	default: return EngineKey::Unknown;
	}
}

EngineKey EngineKeyFromName(std::string_view name)
{
	if (name.empty())
		return EngineKey::Unknown;

	if (name.size() == 1)
	{
		const char c = name[0];
		if (c >= 'a' && c <= 'z')
			return static_cast<EngineKey>(static_cast<uint32_t>(EngineKey::A) + (c - 'a'));
		if (c >= 'A' && c <= 'Z')
			return static_cast<EngineKey>(static_cast<uint32_t>(EngineKey::A) + (c - 'A'));
		if (c >= '0' && c <= '9')
			return static_cast<EngineKey>(static_cast<uint32_t>(EngineKey::Num0) + (c - '0'));
		return EngineKey::Unknown;
	}

	std::string upper(name);
	std::transform(upper.begin(), upper.end(), upper.begin(),
		[](unsigned char c) { return static_cast<char>(std::toupper(c)); });

	struct NamedKey
	{
		const char* name;
		EngineKey key;
	};

	static constexpr NamedKey kNames[] = {
		{ "SPACE", EngineKey::Space },
		{ "ESCAPE", EngineKey::Escape },
		{ "ESC", EngineKey::Escape },
		{ "TAB", EngineKey::Tab },
		{ "ENTER", EngineKey::Enter },
		{ "RETURN", EngineKey::Enter },
		{ "BACKSPACE", EngineKey::Backspace },
		{ "DELETE", EngineKey::Delete },
		{ "INSERT", EngineKey::Insert },
		{ "HOME", EngineKey::Home },
		{ "END", EngineKey::End },
		{ "PAGEUP", EngineKey::PageUp },
		{ "PAGEDOWN", EngineKey::PageDown },
		{ "UP", EngineKey::ArrowUp },
		{ "DOWN", EngineKey::ArrowDown },
		{ "LEFT", EngineKey::ArrowLeft },
		{ "RIGHT", EngineKey::ArrowRight },
		{ "ARROWUP", EngineKey::ArrowUp },
		{ "ARROWDOWN", EngineKey::ArrowDown },
		{ "ARROWLEFT", EngineKey::ArrowLeft },
		{ "ARROWRIGHT", EngineKey::ArrowRight },
		{ "LEFTSHIFT", EngineKey::LeftShift },
		{ "RIGHTSHIFT", EngineKey::RightShift },
		{ "LEFTCTRL", EngineKey::LeftCtrl },
		{ "RIGHTCTRL", EngineKey::RightCtrl },
		{ "LEFTALT", EngineKey::LeftAlt },
		{ "RIGHTALT", EngineKey::RightAlt },
		{ "SHIFT", EngineKey::LeftShift },
		{ "CTRL", EngineKey::LeftCtrl },
		{ "ALT", EngineKey::LeftAlt },
		{ "MOUSELEFT", EngineKey::MouseLeft },
		{ "MOUSERIGHT", EngineKey::MouseRight },
		{ "MOUSEMIDDLE", EngineKey::MouseMiddle },
		{ "MOUSEX1", EngineKey::MouseX1 },
		{ "MOUSEX2", EngineKey::MouseX2 },
		{ "LMB", EngineKey::MouseLeft },
		{ "RMB", EngineKey::MouseRight },
		{ "MMB", EngineKey::MouseMiddle },
		{ "F1", EngineKey::F1 },
		{ "F2", EngineKey::F2 },
		{ "F3", EngineKey::F3 },
		{ "F4", EngineKey::F4 },
		{ "F5", EngineKey::F5 },
		{ "F6", EngineKey::F6 },
		{ "F7", EngineKey::F7 },
		{ "F8", EngineKey::F8 },
		{ "F9", EngineKey::F9 },
		{ "F10", EngineKey::F10 },
		{ "F11", EngineKey::F11 },
		{ "F12", EngineKey::F12 },
	};

	for (const auto& entry : kNames)
	{
		if (upper == entry.name)
			return entry.key;
	}

	// "NUM0".."NUM9"
	if (upper.size() == 4 && upper[0] == 'N' && upper[1] == 'U' && upper[2] == 'M' &&
		upper[3] >= '0' && upper[3] <= '9')
	{
		return static_cast<EngineKey>(static_cast<uint32_t>(EngineKey::Num0) + (upper[3] - '0'));
	}

	return EngineKey::Unknown;
}
