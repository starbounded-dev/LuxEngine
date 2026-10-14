// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "VulkanGPUProfiler.h"

#if LUX_ENABLE_PROFILING

#include <cstring>

namespace Lux {

	void VulkanGPUProfiler::Init(vk::PhysicalDevice physicalDevice, vk::Device device, vk::Queue graphicsQueue, uint32_t graphicsQueueFamily)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Device = device;

		auto poolInfo = vk::CommandPoolCreateInfo()
			.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
			.setQueueFamilyIndex(graphicsQueueFamily);

		if (m_Device.createCommandPool(&poolInfo, nullptr, &m_CommandPool) != vk::Result::eSuccess)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Failed to create the Tracy GPU profiler command pool; GPU zones will be unavailable.");
			return;
		}

		auto allocInfo = vk::CommandBufferAllocateInfo()
			.setCommandPool(m_CommandPool)
			.setLevel(vk::CommandBufferLevel::ePrimary)
			.setCommandBufferCount(1);

		vk::CommandBuffer initCommandBuffer;
		if (m_Device.allocateCommandBuffers(&allocInfo, &initCommandBuffer) != vk::Result::eSuccess)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Failed to allocate the Tracy GPU profiler command buffer; GPU zones will be unavailable.");
			m_Device.destroyCommandPool(m_CommandPool);
			m_CommandPool = nullptr;
			return;
		}

		m_Context = TracyVkContext(physicalDevice, m_Device, graphicsQueue, initCommandBuffer);
		if (!m_Context)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Tracy GPU profiler context creation returned null; GPU zones will be unavailable.");
			return;
		}

		constexpr const char* contextName = "Graphics Queue";
		TracyVkContextName(m_Context, contextName, uint16_t(std::strlen(contextName)));
		LUX_CORE_INFO_TAG("Renderer", "Tracy GPU profiler context created on the graphics queue.");
	}

	void VulkanGPUProfiler::Shutdown()
	{
		LUX_PROFILE_FUNCTION_AUTO;

		if (m_Context)
		{
			TracyVkDestroy(m_Context);
			m_Context = nullptr;
		}

		if (m_CommandPool)
		{
			m_Device.destroyCommandPool(m_CommandPool);
			m_CommandPool = nullptr;
		}
	}

}

#endif
