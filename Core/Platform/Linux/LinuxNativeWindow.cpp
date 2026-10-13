// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Renderer/RHI/RHISwapChain.h"

#include "Lux/Renderer/RHI/RHIDevice.h"

// Last: the X11 headers define macros (None, Bool, Window, Always, ...) that would break the headers
// above.
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>

namespace Lux {

	nri::Window GetNRIWindow(GLFWwindow* window)
	{
		nri::Window nriWindow = {};
		if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
		{
			nriWindow.wayland.display = glfwGetWaylandDisplay();
			nriWindow.wayland.surface = glfwGetWaylandWindow(window);
		}
		else
		{
			// X11 (and XWayland). NRI creates an Xlib surface, so VK_KHR_xlib_surface must be enabled
			// (VulkanDeviceManager does when the platform is X11).
			nriWindow.x11.dpy = glfwGetX11Display();
			nriWindow.x11.window = static_cast<uint64_t>(glfwGetX11Window(window));
		}
		return nriWindow;
	}

}
