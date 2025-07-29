#pragma once

#include <ostream>
#include <cstdint>

namespace StarEngine
{
	typedef enum class KeyCode : uint16_t
	{
		// From glfw3.h
		Space = 32,
		Apostrophe = 39, /* ' */
		Comma = 44, /* , */
		Minus = 45, /* - */
		Period = 46, /* . */
		Slash = 47, /* / */

		D0 = 48, /* 0 */
		D1 = 49, /* 1 */
		D2 = 50, /* 2 */
		D3 = 51, /* 3 */
		D4 = 52, /* 4 */
		D5 = 53, /* 5 */
		D6 = 54, /* 6 */
		D7 = 55, /* 7 */
		D8 = 56, /* 8 */
		D9 = 57, /* 9 */

		Semicolon = 59, /* ; */
		Equal = 61, /* = */

		A = 65,
		B = 66,
		C = 67,
		D = 68,
		E = 69,
		F = 70,
		G = 71,
		H = 72,
		I = 73,
		J = 74,
		K = 75,
		L = 76,
		M = 77,
		N = 78,
		O = 79,
		P = 80,
		Q = 81,
		R = 82,
		S = 83,
		T = 84,
		U = 85,
		V = 86,
		W = 87,
		X = 88,
		Y = 89,
		Z = 90,

		LeftBracket = 91,  /* [ */
		Backslash = 92,  /* \ */
		RightBracket = 93,  /* ] */
		GraveAccent = 96,  /* ` */

		World1 = 161, /* non-US #1 */
		World2 = 162, /* non-US #2 */

		/* Function keys */
		Escape = 256,
		Enter = 257,
		Tab = 258,
		Backspace = 259,
		Insert = 260,
		Delete = 261,
		Right = 262,
		Left = 263,
		Down = 264,
		Up = 265,
		PageUp = 266,
		PageDown = 267,
		Home = 268,
		End = 269,
		CapsLock = 280,
		ScrollLock = 281,
		NumLock = 282,
		PrintScreen = 283,
		Pause = 284,
		F1 = 290,
		F2 = 291,
		F3 = 292,
		F4 = 293,
		F5 = 294,
		F6 = 295,
		F7 = 296,
		F8 = 297,
		F9 = 298,
		F10 = 299,
		F11 = 300,
		F12 = 301,
		F13 = 302,
		F14 = 303,
		F15 = 304,
		F16 = 305,
		F17 = 306,
		F18 = 307,
		F19 = 308,
		F20 = 309,
		F21 = 310,
		F22 = 311,
		F23 = 312,
		F24 = 313,
		F25 = 314,

		/* Keypad */
		KP0 = 320,
		KP1 = 321,
		KP2 = 322,
		KP3 = 323,
		KP4 = 324,
		KP5 = 325,
		KP6 = 326,
		KP7 = 327,
		KP8 = 328,
		KP9 = 329,
		KPDecimal = 330,
		KPDivide = 331,
		KPMultiply = 332,
		KPSubtract = 333,
		KPAdd = 334,
		KPEnter = 335,
		KPEqual = 336,

		LeftShift = 340,
		LeftControl = 341,
		LeftAlt = 342,
		LeftSuper = 343,
		RightShift = 344,
		RightControl = 345,
		RightAlt = 346,
		RightSuper = 347,
		Menu = 348
	} Key;

	enum class KeyState
	{
		None = -1,
		Pressed,
		Held,
		Released
	};

	enum class CursorMode
	{
		Normal = 0,
		Hidden = 1,
		Locked = 2
	};

	typedef enum class MouseButton : uint16_t
	{
		Button0 = 0,
		Button1 = 1,
		Button2 = 2,
		Button3 = 3,
		Button4 = 4,
		Button5 = 5,
		Left = Button0,
		Right = Button1,
		Middle = Button2
	} Button;


	inline std::ostream& operator<<(std::ostream& os, KeyCode keyCode)
	{
		os << static_cast<int32_t>(keyCode);
		return os;
	}

	inline std::ostream& operator<<(std::ostream& os, MouseButton button)
	{
		os << static_cast<int32_t>(button);
		return os;
	}
}

// From glfw3.h
#define SE_KEY_SPACE           ::StarEngine::Key::Space
#define SE_KEY_APOSTROPHE      ::StarEngine::Key::Apostrophe    /* ' */
#define SE_KEY_COMMA           ::StarEngine::Key::Comma         /* , */
#define SE_KEY_MINUS           ::StarEngine::Key::Minus         /* - */
#define SE_KEY_PERIOD          ::StarEngine::Key::Period        /* . */
#define SE_KEY_SLASH           ::StarEngine::Key::Slash         /* / */
#define SE_KEY_0               ::StarEngine::Key::D0
#define SE_KEY_1               ::StarEngine::Key::D1
#define SE_KEY_2               ::StarEngine::Key::D2
#define SE_KEY_3               ::StarEngine::Key::D3
#define SE_KEY_4               ::StarEngine::Key::D4
#define SE_KEY_5               ::StarEngine::Key::D5
#define SE_KEY_6               ::StarEngine::Key::D6
#define SE_KEY_7               ::StarEngine::Key::D7
#define SE_KEY_8               ::StarEngine::Key::D8
#define SE_KEY_9               ::StarEngine::Key::D9
#define SE_KEY_SEMICOLON       ::StarEngine::Key::Semicolon     /* ; */
#define SE_KEY_EQUAL           ::StarEngine::Key::Equal         /* = */
#define SE_KEY_A               ::StarEngine::Key::A
#define SE_KEY_B               ::StarEngine::Key::B
#define SE_KEY_C               ::StarEngine::Key::C
#define SE_KEY_D               ::StarEngine::Key::D
#define SE_KEY_E               ::StarEngine::Key::E
#define SE_KEY_F               ::StarEngine::Key::F
#define SE_KEY_G               ::StarEngine::Key::G
#define SE_KEY_H               ::StarEngine::Key::H
#define SE_KEY_I               ::StarEngine::Key::I
#define SE_KEY_J               ::StarEngine::Key::J
#define SE_KEY_K               ::StarEngine::Key::K
#define SE_KEY_L               ::StarEngine::Key::L
#define SE_KEY_M               ::StarEngine::Key::M
#define SE_KEY_N               ::StarEngine::Key::N
#define SE_KEY_O               ::StarEngine::Key::O
#define SE_KEY_P               ::StarEngine::Key::P
#define SE_KEY_Q               ::StarEngine::Key::Q
#define SE_KEY_R               ::StarEngine::Key::R
#define SE_KEY_S               ::StarEngine::Key::S
#define SE_KEY_T               ::StarEngine::Key::T
#define SE_KEY_U               ::StarEngine::Key::U
#define SE_KEY_V               ::StarEngine::Key::V
#define SE_KEY_W               ::StarEngine::Key::W
#define SE_KEY_X               ::StarEngine::Key::X
#define SE_KEY_Y               ::StarEngine::Key::Y
#define SE_KEY_Z               ::StarEngine::Key::Z
#define SE_KEY_LEFT_BRACKET    ::StarEngine::Key::LeftBracket   /* [ */
#define SE_KEY_BACKSLASH       ::StarEngine::Key::Backslash     /* \ */
#define SE_KEY_RIGHT_BRACKET   ::StarEngine::Key::RightBracket  /* ] */
#define SE_KEY_GRAVE_ACCENT    ::StarEngine::Key::GraveAccent   /* ` */
#define SE_KEY_WORLD_1         ::StarEngine::Key::World1        /* non-US #1 */
#define SE_KEY_WORLD_2         ::StarEngine::Key::World2        /* non-US #2 */

/* Function keys */
#define SE_KEY_ESCAPE          ::StarEngine::Key::Escape
#define SE_KEY_ENTER           ::StarEngine::Key::Enter
#define SE_KEY_TAB             ::StarEngine::Key::Tab
#define SE_KEY_BACKSPACE       ::StarEngine::Key::Backspace
#define SE_KEY_INSERT          ::StarEngine::Key::Insert
#define SE_KEY_DELETE          ::StarEngine::Key::Delete
#define SE_KEY_RIGHT           ::StarEngine::Key::Right
#define SE_KEY_LEFT            ::StarEngine::Key::Left
#define SE_KEY_DOWN            ::StarEngine::Key::Down
#define SE_KEY_UP              ::StarEngine::Key::Up
#define SE_KEY_PAGE_UP         ::StarEngine::Key::PageUp
#define SE_KEY_PAGE_DOWN       ::StarEngine::Key::PageDown
#define SE_KEY_HOME            ::StarEngine::Key::Home
#define SE_KEY_END             ::StarEngine::Key::End
#define SE_KEY_CAPS_LOCK       ::StarEngine::Key::CapsLock
#define SE_KEY_SCROLL_LOCK     ::StarEngine::Key::ScrollLock
#define SE_KEY_NUM_LOCK        ::StarEngine::Key::NumLock
#define SE_KEY_PRINT_SCREEN    ::StarEngine::Key::PrintScreen
#define SE_KEY_PAUSE           ::StarEngine::Key::Pause
#define SE_KEY_F1              ::StarEngine::Key::F1
#define SE_KEY_F2              ::StarEngine::Key::F2
#define SE_KEY_F3              ::StarEngine::Key::F3
#define SE_KEY_F4              ::StarEngine::Key::F4
#define SE_KEY_F5              ::StarEngine::Key::F5
#define SE_KEY_F6              ::StarEngine::Key::F6
#define SE_KEY_F7              ::StarEngine::Key::F7
#define SE_KEY_F8              ::StarEngine::Key::F8
#define SE_KEY_F9              ::StarEngine::Key::F9
#define SE_KEY_F10             ::StarEngine::Key::F10
#define SE_KEY_F11             ::StarEngine::Key::F11
#define SE_KEY_F12             ::StarEngine::Key::F12
#define SE_KEY_F13             ::StarEngine::Key::F13
#define SE_KEY_F14             ::StarEngine::Key::F14
#define SE_KEY_F15             ::StarEngine::Key::F15
#define SE_KEY_F16             ::StarEngine::Key::F16
#define SE_KEY_F17             ::StarEngine::Key::F17
#define SE_KEY_F18             ::StarEngine::Key::F18
#define SE_KEY_F19             ::StarEngine::Key::F19
#define SE_KEY_F20             ::StarEngine::Key::F20
#define SE_KEY_F21             ::StarEngine::Key::F21
#define SE_KEY_F22             ::StarEngine::Key::F22
#define SE_KEY_F23             ::StarEngine::Key::F23
#define SE_KEY_F24             ::StarEngine::Key::F24
#define SE_KEY_F25             ::StarEngine::Key::F25

/* Keypad */
#define SE_KEY_KP_0            ::StarEngine::Key::KP0
#define SE_KEY_KP_1            ::StarEngine::Key::KP1
#define SE_KEY_KP_2            ::StarEngine::Key::KP2
#define SE_KEY_KP_3            ::StarEngine::Key::KP3
#define SE_KEY_KP_4            ::StarEngine::Key::KP4
#define SE_KEY_KP_5            ::StarEngine::Key::KP5
#define SE_KEY_KP_6            ::StarEngine::Key::KP6
#define SE_KEY_KP_7            ::StarEngine::Key::KP7
#define SE_KEY_KP_8            ::StarEngine::Key::KP8
#define SE_KEY_KP_9            ::StarEngine::Key::KP9
#define SE_KEY_KP_DECIMAL      ::StarEngine::Key::KPDecimal
#define SE_KEY_KP_DIVIDE       ::StarEngine::Key::KPDivide
#define SE_KEY_KP_MULTIPLY     ::StarEngine::Key::KPMultiply
#define SE_KEY_KP_SUBTRACT     ::StarEngine::Key::KPSubtract
#define SE_KEY_KP_ADD          ::StarEngine::Key::KPAdd
#define SE_KEY_KP_ENTER        ::StarEngine::Key::KPEnter
#define SE_KEY_KP_EQUAL        ::StarEngine::Key::KPEqual

#define SE_KEY_LEFT_SHIFT      ::StarEngine::Key::LeftShift
#define SE_KEY_LEFT_CONTROL    ::StarEngine::Key::LeftControl
#define SE_KEY_LEFT_ALT        ::StarEngine::Key::LeftAlt
#define SE_KEY_LEFT_SUPER      ::StarEngine::Key::LeftSuper
#define SE_KEY_RIGHT_SHIFT     ::StarEngine::Key::RightShift
#define SE_KEY_RIGHT_CONTROL   ::StarEngine::Key::RightControl
#define SE_KEY_RIGHT_ALT       ::StarEngine::Key::RightAlt
#define SE_KEY_RIGHT_SUPER     ::StarEngine::Key::RightSuper
#define SE_KEY_MENU            ::StarEngine::Key::Menu

// Mouse
#define SE_MOUSE_BUTTON_LEFT    ::StarEngine::Button::Left
#define SE_MOUSE_BUTTON_RIGHT   ::StarEngine::Button::Right
#define SE_MOUSE_BUTTON_MIDDLE  ::StarEngine::Button::Middle
