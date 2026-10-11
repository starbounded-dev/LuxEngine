// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Renderer/RHI/ResourceStateTracker.h"

#include "nvrhi/nvrhi.h"

#include <vulkan/vulkan.hpp>

#include <array>
#include <vector>
#include <queue>

namespace nri {
	struct Descriptor;
	struct Fence;
	struct Texture;
	enum class Format : uint8_t;
}

struct GLFWwindow;

namespace Lux {

	class VulkanSwapChain
	{
	public:
		bool Create(uint32_t width, uint32_t height);
		void Destroy();

		void OnResize(uint32_t width, uint32_t height);
		void Resize();

		bool BeginFrame();
		void Present();

		nvrhi::ITexture* GetCurrentBackBuffer();
		nvrhi::ITexture* GetBackBuffer(uint32_t index);
		uint32_t GetCurrentBackBufferIndex();
		uint32_t GetBackBufferCount();
		// The NRI wrapper of the acquired image's binary semaphore, handed out once per frame: the
		// first submission that touches the back buffer waits for it (RenderCommandBuffer::
		// RT_Submit), and Present waits for it when nothing did. Null after that. Render thread,
		// after BeginFrame.
		nri::Fence* TakeAcquireSemaphore() { return std::exchange(m_AcquiredSemaphore, nullptr); }

		// The NRI color-attachment view of the acquired back buffer (NRI migration Phase 10), and the
		// back buffer as the resource state tracker sees it (resting in Present). Render thread,
		// after BeginFrame; null / empty before the first acquire.
		nri::Descriptor* GetCurrentNRIColorAttachment() const;
		TrackedTexture GetCurrentTrackedBackBuffer() const;
		// The NRI format of the back buffers (and of their color-attachment views).
		nri::Format GetNRIColorFormat() const;

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		// True when the surface reported eSuboptimal/eOutOfDate (typically after a resize) and the
		// swapchain must be recreated. Recreation is deferred to a point where both threads are idle
		// (Window::ProcessEvents) — never mid-BeginFrame, which would orphan a signaled acquire
		// semaphore and crash. Cleared by Create().
		bool NeedsRecreate() const { return m_NeedsRecreate; }

	private:
		bool IsSurfaceZeroSized() const;
	public:
		VulkanSwapChain(vk::SurfaceKHR surface);
	private:
		vk::SurfaceKHR m_Surface = nullptr;
		uint32_t m_Width = 0, m_Height = 0;

		std::array<vk::Semaphore, 3> m_AcquireSemaphores;
		std::vector<vk::Semaphore> m_PresentSemaphores; // One per swapchain image
		// Their NRI wrappers, for NRI submissions (RHIQueueTimeline); created and destroyed with them.
		std::array<nri::Fence*, 3> m_AcquireFences = {};
		std::vector<nri::Fence*> m_PresentFences;
		nri::Fence* m_AcquiredSemaphore = nullptr;

		vk::SurfaceFormatKHR m_SwapChainFormat;
		vk::SwapchainKHR m_SwapChain;

		uint32_t m_AcquireSemaphoreIndex = 0;
		bool m_NeedsRecreate = false;
		bool m_ImageAcquired = false; // Render thread only: set by a successful BeginFrame, consumed by Present

		struct SwapChainImage
		{
			vk::Image image;
			nvrhi::TextureHandle rhiHandle;
			// Non-owning NRI wrapper of `image` and its color-attachment view; nothing renders
			// through them until NRI draws (migration P10/P14).
			nri::Texture* RHITexture = nullptr;
			nri::Descriptor* RHIColorAttachment = nullptr;
			TrackedTexture Tracked;
		};
		std::vector<SwapChainImage> m_SwapChainImages;
		uint32_t m_SwapChainIndex = uint32_t(-1);

		// Graphics timeline values of the presented frames still on the GPU.
		std::queue<uint64_t> m_FramesInFlight;
	};
}
