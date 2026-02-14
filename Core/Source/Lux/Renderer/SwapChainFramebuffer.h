#pragma once

#include "Lux/Renderer/Framebuffer.h"
#include "Lux/Platform/Vulkan/VulkanSwapChain.h"

namespace Lux {

	/// Wraps VulkanSwapChain to provide Framebuffer interface for Renderer2D and similar.
	/// GetHandle() returns the current swapchain framebuffer (changes each frame).
	class SwapChainFramebuffer : public Framebuffer
	{
	public:
		static Ref<SwapChainFramebuffer> Create(VulkanSwapChain* swapchain);
		SwapChainFramebuffer(VulkanSwapChain* swapchain);

		virtual nvrhi::FramebufferHandle GetHandle() const override;
		virtual uint32_t GetWidth() const override;
		virtual uint32_t GetHeight() const override;

		VulkanSwapChain* GetSwapChain() const { return m_SwapChain; }

	private:
		VulkanSwapChain* m_SwapChain = nullptr;
	};

}
