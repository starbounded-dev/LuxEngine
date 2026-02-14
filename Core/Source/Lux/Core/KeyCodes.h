#pragma once

#include <ostream>
#include <cstdint>

namespace Lux
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
#define LUX_KEY_SPACE           ::Lux::Key::Space
#define LUX_KEY_APOSTROPHE      ::Lux::Key::Apostrophe    /* ' */
#define LUX_KEY_COMMA           ::Lux::Key::Comma         /* , */
#define LUX_KEY_MINUS           ::Lux::Key::Minus         /* - */
#define LUX_KEY_PERIOD          ::Lux::Key::Period        /* . */
#define LUX_KEY_SLASH           ::Lux::Key::Slash         /* / */
#define LUX_KEY_0               ::Lux::Key::D0
#define LUX_KEY_1               ::Lux::Key::D1
#define LUX_KEY_2               ::Lux::Key::D2
#define LUX_KEY_3               ::Lux::Key::D3
#define LUX_KEY_4               ::Lux::Key::D4
#define LUX_KEY_5               ::Lux::Key::D5
#define LUX_KEY_6               ::Lux::Key::D6
#define LUX_KEY_7               ::Lux::Key::D7
#define LUX_KEY_8               ::Lux::Key::D8
#define LUX_KEY_9               ::Lux::Key::D9
#define LUX_KEY_SEMICOLON       ::Lux::Key::Semicolon     /* ; */
#define LUX_KEY_EQUAL           ::Lux::Key::Equal         /* = */
#define LUX_KEY_A               ::Lux::Key::A
#define LUX_KEY_B               ::Lux::Key::B
#define LUX_KEY_C               ::Lux::Key::C
#define LUX_KEY_D               ::Lux::Key::D
#define LUX_KEY_E               ::Lux::Key::E
#define LUX_KEY_F               ::Lux::Key::F
#define LUX_KEY_G               ::Lux::Key::G
#define LUX_KEY_H               ::Lux::Key::H
#define LUX_KEY_I               ::Lux::Key::I
#define LUX_KEY_J               ::Lux::Key::J
#define LUX_KEY_K               ::Lux::Key::K
#define LUX_KEY_L               ::Lux::Key::L
#define LUX_KEY_M               ::Lux::Key::M
#define LUX_KEY_N               ::Lux::Key::N
#define LUX_KEY_O               ::Lux::Key::O
#define LUX_KEY_P               ::Lux::Key::P
#define LUX_KEY_Q               ::Lux::Key::Q
#define LUX_KEY_R               ::Lux::Key::R
#define LUX_KEY_S               ::Lux::Key::S
#define LUX_KEY_T               ::Lux::Key::T
#define LUX_KEY_U               ::Lux::Key::U
#define LUX_KEY_V               ::Lux::Key::V
#define LUX_KEY_W               ::Lux::Key::W
#define LUX_KEY_X               ::Lux::Key::X
#define LUX_KEY_Y               ::Lux::Key::Y
#define LUX_KEY_Z               ::Lux::Key::Z
#define LUX_KEY_LEFT_BRACKET    ::Lux::Key::LeftBracket   /* [ */
#define LUX_KEY_BACKSLASH       ::Lux::Key::Backslash     /* \ */
#define LUX_KEY_RIGHT_BRACKET   ::Lux::Key::RightBracket  /* ] */
#define LUX_KEY_GRAVE_ACCENT    ::Lux::Key::GraveAccent   /* ` */
#define LUX_KEY_WORLD_1         ::Lux::Key::World1        /* non-US #1 */
#define LUX_KEY_WORLD_2         ::Lux::Key::World2        /* non-US #2 */

/* Function keys */
#define LUX_KEY_ESCAPE          ::Lux::Key::Escape
#define LUX_KEY_ENTER           ::Lux::Key::Enter
#define LUX_KEY_TAB             ::Lux::Key::Tab
#define LUX_KEY_BACKSPACE       ::Lux::Key::Backspace
#define LUX_KEY_INSERT          ::Lux::Key::Insert
#define LUX_KEY_DELETE          ::Lux::Key::Delete
#define LUX_KEY_RIGHT           ::Lux::Key::Right
#define LUX_KEY_LEFT            ::Lux::Key::Left
#define LUX_KEY_DOWN            ::Lux::Key::Down
#define LUX_KEY_UP              ::Lux::Key::Up
#define LUX_KEY_PAGE_UP         ::Lux::Key::PageUp
#define LUX_KEY_PAGE_DOWN       ::Lux::Key::PageDown
#define LUX_KEY_HOME            ::Lux::Key::Home
#define LUX_KEY_END             ::Lux::Key::End
#define LUX_KEY_CAPS_LOCK       ::Lux::Key::CapsLock
#define LUX_KEY_SCROLL_LOCK     ::Lux::Key::ScrollLock
#define LUX_KEY_NUM_LOCK        ::Lux::Key::NumLock
#define LUX_KEY_PRINT_SCREEN    ::Lux::Key::PrintScreen
#define LUX_KEY_PAUSE           ::Lux::Key::Pause
#define LUX_KEY_F1              ::Lux::Key::F1
#define LUX_KEY_F2              ::Lux::Key::F2
#define LUX_KEY_F3              ::Lux::Key::F3
#define LUX_KEY_F4              ::Lux::Key::F4
#define LUX_KEY_F5              ::Lux::Key::F5
#define LUX_KEY_F6              ::Lux::Key::F6
#define LUX_KEY_F7              ::Lux::Key::F7
#define LUX_KEY_F8              ::Lux::Key::F8
#define LUX_KEY_F9              ::Lux::Key::F9
#define LUX_KEY_F10             ::Lux::Key::F10
#define LUX_KEY_F11             ::Lux::Key::F11
#define LUX_KEY_F12             ::Lux::Key::F12
#define LUX_KEY_F13             ::Lux::Key::F13
#define LUX_KEY_F14             ::Lux::Key::F14
#define LUX_KEY_F15             ::Lux::Key::F15
#define LUX_KEY_F16             ::Lux::Key::F16
#define LUX_KEY_F17             ::Lux::Key::F17
#define LUX_KEY_F18             ::Lux::Key::F18
#define LUX_KEY_F19             ::Lux::Key::F19
#define LUX_KEY_F20             ::Lux::Key::F20
#define LUX_KEY_F21             ::Lux::Key::F21
#define LUX_KEY_F22             ::Lux::Key::F22
#define LUX_KEY_F23             ::Lux::Key::F23
#define LUX_KEY_F24             ::Lux::Key::F24
#define LUX_KEY_F25             ::Lux::Key::F25

/* Keypad */
#define LUX_KEY_KP_0            ::Lux::Key::KP0
#define LUX_KEY_KP_1            ::Lux::Key::KP1
#define LUX_KEY_KP_2            ::Lux::Key::KP2
#define LUX_KEY_KP_3            ::Lux::Key::KP3
#define LUX_KEY_KP_4            ::Lux::Key::KP4
#define LUX_KEY_KP_5            ::Lux::Key::KP5
#define LUX_KEY_KP_6            ::Lux::Key::KP6
#define LUX_KEY_KP_7            ::Lux::Key::KP7
#define LUX_KEY_KP_8            ::Lux::Key::KP8
#define LUX_KEY_KP_9            ::Lux::Key::KP9
#define LUX_KEY_KP_DECIMAL      ::Lux::Key::KPDecimal
#define LUX_KEY_KP_DIVIDE       ::Lux::Key::KPDivide
#define LUX_KEY_KP_MULTIPLY     ::Lux::Key::KPMultiply
#define LUX_KEY_KP_SUBTRACT     ::Lux::Key::KPSubtract
#define LUX_KEY_KP_ADD          ::Lux::Key::KPAdd
#define LUX_KEY_KP_ENTER        ::Lux::Key::KPEnter
#define LUX_KEY_KP_EQUAL        ::Lux::Key::KPEqual

#define LUX_KEY_LEFT_SHIFT      ::Lux::Key::LeftShift
#define LUX_KEY_LEFT_CONTROL    ::Lux::Key::LeftControl
#define LUX_KEY_LEFT_ALT        ::Lux::Key::LeftAlt
#define LUX_KEY_LEFT_SUPER      ::Lux::Key::LeftSuper
#define LUX_KEY_RIGHT_SHIFT     ::Lux::Key::RightShift
#define LUX_KEY_RIGHT_CONTROL   ::Lux::Key::RightControl
#define LUX_KEY_RIGHT_ALT       ::Lux::Key::RightAlt
#define LUX_KEY_RIGHT_SUPER     ::Lux::Key::RightSuper
#define LUX_KEY_MENU            ::Lux::Key::Menu

// Mouse
#define LUX_MOUSE_BUTTON_LEFT    ::Lux::Button::Left
#define LUX_MOUSE_BUTTON_RIGHT   ::Lux::Button::Right
#define LUX_MOUSE_BUTTON_MIDDLE  ::Lux::Button::Middle
