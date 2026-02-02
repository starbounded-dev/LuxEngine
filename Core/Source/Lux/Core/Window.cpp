#include "lpch.h"
#include "Lux/Core/Window.h"

#ifdef LUX_PLATFORM_WINDOWS
#include "Platform/Windows/WindowsWindow.h"
#endif

namespace Lux
{
	Scope<Window> Window::Create(const WindowProps& props)
	{
#ifdef LUX_PLATFORM_WINDOWS
		return CreateScope<WindowsWindow>(props);
#else
		LUX_CORE_ASSERT(false, "Unknown platform!");
		return nullptr;
#endif
	}
}