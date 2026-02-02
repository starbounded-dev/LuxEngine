#include "sepch.h"
#include "StarEngine/Core/Window.h"

#ifdef SE_PLATFORM_WINDOWS
#include "Platform/Windows/WindowsWindow.h"
#endif

namespace StarEngine
{
	Scope<Window> Window::Create(const WindowProps& props)
	{
#ifdef SE_PLATFORM_WINDOWS
		return CreateScope<WindowsWindow>(props);
#else
		SE_CORE_ASSERT(false, "Unknown platform!");
		return nullptr;
#endif
	}
}