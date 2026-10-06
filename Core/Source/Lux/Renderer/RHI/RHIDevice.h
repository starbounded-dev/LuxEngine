// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/RHI/RHITypes.h"

#include <NRI.h>
#include <Extensions/NRIDeviceCreation.h>
#include <Extensions/NRIHelper.h>
#include <Extensions/NRIMeshShader.h>
#include <Extensions/NRISwapChain.h>
#include <Extensions/NRIWrapperVK.h>

#include <array>
#include <string>
#include <vector>

namespace Lux {

	// Every NRI interface Lux uses, filled once by RHIDevice::Init (the layout NRI's samples use).
	// MeshShaderInterface stays zeroed when the device has no mesh shaders.
	struct NRIInterface
		: nri::CoreInterface
		, nri::HelperInterface
		, nri::WrapperVKInterface
		, nri::SwapChainInterface
		, nri::MeshShaderInterface
	{
	};

	nri::QueueType ToNRIQueueType(GPUQueue queue);

	// The Vulkan device RHIDevice wraps, as VulkanDeviceManager created it. Handles are the raw
	// VkInstance, VkPhysicalDevice and VkDevice.
	struct RHIDeviceCreateInfo
	{
		void* Instance = nullptr;
		void* PhysicalDevice = nullptr;
		void* Device = nullptr;
		// The device is used at Vulkan 1.<MinorVersion>; NRI needs 2 or higher.
		uint32_t MinorVersion = 0;
		// Queue family per GPUQueue; -1 when that queue was not created.
		std::array<int32_t, static_cast<size_t>(GPUQueue::Count)> QueueFamilies = { -1, -1, -1 };
		std::vector<std::string> InstanceExtensions;
		std::vector<std::string> DeviceExtensions;
		bool EnableValidation = false;
	};

	// The NRI device. Until NVRHI is removed (NRI migration Phase 15) it wraps the VkDevice that
	// VulkanDeviceManager creates and NVRHI also drives; it never owns the native device, queues or
	// instance. Destruction order: RHIDevice, then NVRHI, then vkDestroyDevice.
	//
	// Threading: Init, RunSelfTest and Shutdown run on the main thread from Window::Init and
	// Window::Shutdown, while the render thread submits nothing. The accessors are read-only after
	// Init. NRI queue submissions hold RenderCommandBuffer::LockQueue, because NVRHI submits to the
	// same VkQueues.
	class RHIDevice
	{
	public:
		static bool Init(const RHIDeviceCreateInfo& createInfo);
		static void Shutdown();

		static bool IsInitialized();
		static nri::Device& Get();
		static const NRIInterface& API();
		// Null when the queue was not created (no dedicated compute or transfer family).
		static nri::Queue* GetQueue(GPUQueue queue);
		static const nri::DeviceDesc& GetDesc();

		// Creates, records and submits a little work through NRI on the shared device (buffer,
		// texture, view, barrier, zero fill, fence wait) and destroys it again. Logs the outcome.
		static bool RunSelfTest();
	};

}
