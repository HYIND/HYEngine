#include "EditorGlfwInput.h"

#include <GLFW/glfw3.h>

EngineKey EngineKeyFromGlfwKey(int glfwKey)
{
	switch (glfwKey)
	{
	// 字母：GLFW_KEY_A 等就是 ASCII 大写字母
	case GLFW_KEY_A: return EngineKey::A;
	case GLFW_KEY_B: return EngineKey::B;
	case GLFW_KEY_C: return EngineKey::C;
	case GLFW_KEY_D: return EngineKey::D;
	case GLFW_KEY_E: return EngineKey::E;
	case GLFW_KEY_F: return EngineKey::F;
	case GLFW_KEY_G: return EngineKey::G;
	case GLFW_KEY_H: return EngineKey::H;
	case GLFW_KEY_I: return EngineKey::I;
	case GLFW_KEY_J: return EngineKey::J;
	case GLFW_KEY_K: return EngineKey::K;
	case GLFW_KEY_L: return EngineKey::L;
	case GLFW_KEY_M: return EngineKey::M;
	case GLFW_KEY_N: return EngineKey::N;
	case GLFW_KEY_O: return EngineKey::O;
	case GLFW_KEY_P: return EngineKey::P;
	case GLFW_KEY_Q: return EngineKey::Q;
	case GLFW_KEY_R: return EngineKey::R;
	case GLFW_KEY_S: return EngineKey::S;
	case GLFW_KEY_T: return EngineKey::T;
	case GLFW_KEY_U: return EngineKey::U;
	case GLFW_KEY_V: return EngineKey::V;
	case GLFW_KEY_W: return EngineKey::W;
	case GLFW_KEY_X: return EngineKey::X;
	case GLFW_KEY_Y: return EngineKey::Y;
	case GLFW_KEY_Z: return EngineKey::Z;

	case GLFW_KEY_0: return EngineKey::Num0;
	case GLFW_KEY_1: return EngineKey::Num1;
	case GLFW_KEY_2: return EngineKey::Num2;
	case GLFW_KEY_3: return EngineKey::Num3;
	case GLFW_KEY_4: return EngineKey::Num4;
	case GLFW_KEY_5: return EngineKey::Num5;
	case GLFW_KEY_6: return EngineKey::Num6;
	case GLFW_KEY_7: return EngineKey::Num7;
	case GLFW_KEY_8: return EngineKey::Num8;
	case GLFW_KEY_9: return EngineKey::Num9;

	case GLFW_KEY_F1: return EngineKey::F1;
	case GLFW_KEY_F2: return EngineKey::F2;
	case GLFW_KEY_F3: return EngineKey::F3;
	case GLFW_KEY_F4: return EngineKey::F4;
	case GLFW_KEY_F5: return EngineKey::F5;
	case GLFW_KEY_F6: return EngineKey::F6;
	case GLFW_KEY_F7: return EngineKey::F7;
	case GLFW_KEY_F8: return EngineKey::F8;
	case GLFW_KEY_F9: return EngineKey::F9;
	case GLFW_KEY_F10: return EngineKey::F10;
	case GLFW_KEY_F11: return EngineKey::F11;
	case GLFW_KEY_F12: return EngineKey::F12;

	case GLFW_KEY_ESCAPE: return EngineKey::Escape;
	case GLFW_KEY_TAB: return EngineKey::Tab;
	case GLFW_KEY_CAPS_LOCK: return EngineKey::CapsLock;
	case GLFW_KEY_SPACE: return EngineKey::Space;
	case GLFW_KEY_ENTER: return EngineKey::Enter;
	case GLFW_KEY_BACKSPACE: return EngineKey::Backspace;
	case GLFW_KEY_DELETE: return EngineKey::Delete;
	case GLFW_KEY_INSERT: return EngineKey::Insert;

	case GLFW_KEY_HOME: return EngineKey::Home;
	case GLFW_KEY_END: return EngineKey::End;
	case GLFW_KEY_PAGE_UP: return EngineKey::PageUp;
	case GLFW_KEY_PAGE_DOWN: return EngineKey::PageDown;

	case GLFW_KEY_UP: return EngineKey::ArrowUp;
	case GLFW_KEY_DOWN: return EngineKey::ArrowDown;
	case GLFW_KEY_LEFT: return EngineKey::ArrowLeft;
	case GLFW_KEY_RIGHT: return EngineKey::ArrowRight;

	case GLFW_KEY_LEFT_SHIFT: return EngineKey::LeftShift;
	case GLFW_KEY_RIGHT_SHIFT: return EngineKey::RightShift;
	case GLFW_KEY_LEFT_CONTROL: return EngineKey::LeftCtrl;
	case GLFW_KEY_RIGHT_CONTROL: return EngineKey::RightCtrl;
	case GLFW_KEY_LEFT_ALT: return EngineKey::LeftAlt;
	case GLFW_KEY_RIGHT_ALT: return EngineKey::RightAlt;

	case GLFW_KEY_MINUS: return EngineKey::Minus;
	case GLFW_KEY_EQUAL: return EngineKey::Equal;
	case GLFW_KEY_LEFT_BRACKET: return EngineKey::LeftBracket;
	case GLFW_KEY_RIGHT_BRACKET: return EngineKey::RightBracket;
	case GLFW_KEY_BACKSLASH: return EngineKey::Backslash;
	case GLFW_KEY_SEMICOLON: return EngineKey::Semicolon;
	case GLFW_KEY_APOSTROPHE: return EngineKey::Quote;
	case GLFW_KEY_COMMA: return EngineKey::Comma;
	case GLFW_KEY_PERIOD: return EngineKey::Period;
	case GLFW_KEY_SLASH: return EngineKey::Slash;
	case GLFW_KEY_GRAVE_ACCENT: return EngineKey::Grave;

	default: return EngineKey::Unknown;
	}
}

EngineKey EngineKeyFromGlfwMouseButton(int glfwButton)
{
	switch (glfwButton)
	{
	case GLFW_MOUSE_BUTTON_LEFT: return EngineKey::MouseLeft;
	case GLFW_MOUSE_BUTTON_RIGHT: return EngineKey::MouseRight;
	case GLFW_MOUSE_BUTTON_MIDDLE: return EngineKey::MouseMiddle;
	default: return EngineKey::Unknown;
	}
}
