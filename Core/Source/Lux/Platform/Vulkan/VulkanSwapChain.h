#pragma once

#include "Lux/Core/Base.h"

#include "Vulkan.h"
#include "VulkanDevice.h"

#include "nvrhi/nvrhi.h"

#include <vulkan/vulkan.hpp>

#include <vector>
#include <queue>

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
		vk::Semaphore GetAcquiredImageSemaphore() const { return m_AcquiredSemaphore; }

		nvrhi::IFramebuffer* GetCurrentFramebuffer();
		nvrhi::IFramebuffer* GetFramebuffer(uint32_t index);

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		// True when the surface reported eSuboptimal/eOutOfDate (typically after a resize) and the
		// swapchain must be recreated. Recreation is deferred to a point where both threads are idle
		// (Window::ProcessEvents) — never mid-BeginFrame, which would orphan a signaled acquire
		// semaphore and crash. Cleared by Create().
		bool NeedsRecreate() const { return m_NeedsRecreate; }

		void BackBufferResizing();
		void BackBufferResized();
	public:
		VulkanSwapChain(vk::SurfaceKHR surface);
	private:
		vk::SurfaceKHR m_Surface = nullptr;
		uint32_t m_Width = 0, m_Height = 0;

		std::array<vk::Semaphore, 3> m_AcquireSemaphores;
		std::vector<vk::Semaphore> m_PresentSemaphores; // One per swapchain image
		vk::Semaphore m_AcquiredSemaphore;

		std::vector<nvrhi::FramebufferHandle> m_SwapChainFramebuffers;

		vk::SurfaceFormatKHR m_SwapChainFormat;
		vk::SwapchainKHR m_SwapChain;

		uint32_t m_AcquireSemaphoreIndex = 0;
		bool m_NeedsRecreate = false;

		struct SwapChainImage
		{
			vk::Image image;
			nvrhi::TextureHandle rhiHandle;
		};
		std::vector<SwapChainImage> m_SwapChainImages;
		uint32_t m_SwapChainIndex = uint32_t(-1);

		std::queue<nvrhi::EventQueryHandle> m_FramesInFlight;
		std::vector<nvrhi::EventQueryHandle> m_QueryPool;
	};
}
