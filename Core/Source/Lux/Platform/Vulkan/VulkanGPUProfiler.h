// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Debug/Profiler.h"

#if LUX_ENABLE_PROFILING

#include <vulkan/vulkan.hpp>

// Only pulled in here, not in Profiler.h: TracyVulkan.hpp needs the Vulkan headers, and
// Profiler.h reaches nearly every translation unit through the PCH.
#include <tracy/TracyVulkan.hpp>

namespace Lux {

	// Tracy's GPU context on the graphics queue, owned by VulkanDevice: it binds to the physical
	// device, device and queue that class owns, so its lifetime matches theirs. Zones are emitted on
	// the render thread by RenderCommandBuffer's timer-query pair.
	class VulkanGPUProfiler
	{
	public:
		// Blocking: Tracy submits and waits on the graphics queue to correlate the CPU and GPU
		// clocks, so this runs before the render thread exists. Logs and leaves the context null on
		// failure, which callers must tolerate.
		void Init(vk::PhysicalDevice physicalDevice, vk::Device device, vk::Queue graphicsQueue, uint32_t graphicsQueueFamily);
		// Before the device is destroyed: the context owns a query pool on it.
		void Shutdown();

		[[nodiscard]] TracyVkCtx GetContext() const { return m_Context; }

	private:
		vk::Device m_Device;
		TracyVkCtx m_Context = nullptr;
		// Tracy drives its init command buffer itself - recording, submitting, and calling
		// vkQueueWaitIdle several times - so it needs a pool of its own.
		vk::CommandPool m_CommandPool;
	};

}

#endif
