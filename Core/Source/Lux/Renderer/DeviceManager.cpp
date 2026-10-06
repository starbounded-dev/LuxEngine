/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

/*
License for glfw

Copyright (c) 2002-2006 Marcus Geelnard

Copyright (c) 2006-2019 Camilla Lowy

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would
   be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not
   be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
   distribution.
*/
#include "lpch.h"
#include "DeviceManager.h"

#include "Lux/Platform/Vulkan/VulkanDeviceManager.h"

#if LUX_HAS_DX11
#include <d3d11.h>
#endif

#if LUX_HAS_DX12
#include <d3d12.h>
#endif

#ifdef _WIN64
#include <ShellScalingApi.h>
#pragma comment(lib, "shcore.lib")
#endif

using namespace Lux;

bool DeviceManager::CreateDevice(const DeviceCreationParameters& params, const char *windowTitle)
{
	m_DeviceParams = params;

	if (!CreateInstanceInternal())
		return false;

    if (!CreateDevice())
        return false;

    return true;
}

void DeviceManager::Shutdown()
{
    DestroyDevice();
}

Lux::DeviceManager* Lux::DeviceManager::Create(nvrhi::GraphicsAPI api, GLFWwindow* windowHandle)
{
    switch (api)
    {
#if LUX_HAS_DX11
    case nvrhi::GraphicsAPI::D3D11:
        return CreateD3D11();
#endif
#if LUX_HAS_DX12
    case nvrhi::GraphicsAPI::D3D12:
        return CreateD3D12();
#endif
#if LUX_HAS_VULKAN
    case nvrhi::GraphicsAPI::VULKAN:
        return CreateVK(windowHandle);
#endif
    default:
        LUX_CORE_ERROR("DeviceManager::Create: Unsupported Graphics API {0}", (uint8_t)api);
        return nullptr;
    }
}


DefaultMessageCallback& DefaultMessageCallback::GetInstance()
{
    static DefaultMessageCallback Instance;
    return Instance;
}

void DefaultMessageCallback::message(nvrhi::MessageSeverity severity, const char* messageText)
{
    switch (severity)
    {
    case nvrhi::MessageSeverity::Info:
		LUX_CORE_INFO("{0}", messageText);
        break;
    case nvrhi::MessageSeverity::Warning:
		LUX_CORE_WARN("{0}", messageText);
        break;
    case nvrhi::MessageSeverity::Error:
		// NVRHI reports a lost device on submit only through this message (vulkan-queue.cpp).
		if (std::string_view(messageText) == "Device Removed!")
			VulkanDeviceManager::ReportDeviceLost("NVRHI queue submit");
		LUX_CORE_ERROR("{0}", messageText);
        break;
    case nvrhi::MessageSeverity::Fatal:
		LUX_CORE_FATAL("{0}", messageText);
        break;
    }
}
