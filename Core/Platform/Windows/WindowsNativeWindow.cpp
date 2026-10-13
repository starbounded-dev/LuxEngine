// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Renderer/RHI/RHISwapChain.h"

#include "Lux/Renderer/RHI/RHIDevice.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace Lux {

	nri::Window GetNRIWindow(GLFWwindow* window)
	{
		nri::Window nriWindow = {};
		nriWindow.windows.hwnd = glfwGetWin32Window(window);
		return nriWindow;
	}

}
