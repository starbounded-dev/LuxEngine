#include "lpch.h"
#include "SwapChainFramebuffer.h"

#include "Lux/Platform/Vulkan/VulkanSwapChain.h"

namespace Lux {

	SwapChainFramebuffer::SwapChainFramebuffer(VulkanSwapChain* swapchain)
		: Framebuffer(), m_SwapChain(swapchain)
	{
	}

	Ref<SwapChainFramebuffer> SwapChainFramebuffer::Create(VulkanSwapChain* swapchain)
	{
		LUX_CORE_VERIFY(swapchain);
		return Ref<SwapChainFramebuffer>::Create(swapchain);
	}

	nvrhi::FramebufferHandle SwapChainFramebuffer::GetHandle() const
	{
		if (!m_SwapChain)
			return nullptr;
		return nvrhi::FramebufferHandle(m_SwapChain->GetCurrentFramebuffer());
	}

	uint32_t SwapChainFramebuffer::GetWidth() const
	{
		return m_SwapChain ? m_SwapChain->GetWidth() : 0;
	}

	uint32_t SwapChainFramebuffer::GetHeight() const
	{
		return m_SwapChain ? m_SwapChain->GetHeight() : 0;
	}

}
