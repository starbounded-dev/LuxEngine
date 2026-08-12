#include "lpch.h"
#include "VulkanSwapChain.h"

#include "Lux/Core/Application.h"
#include "Lux/Debug/Profiler.h"

#include <GLFW/glfw3.h>

#include "VulkanDeviceManager.h"

namespace Lux {

	namespace Utils {
		template <typename T>
		static std::vector<T> SetToVector(const std::unordered_set<T>& set)
		{
			std::vector<T> ret;
			for (const auto& s : set)
			{
				ret.push_back(s);
			}

			return ret;
		}
	}

	VulkanSwapChain::VulkanSwapChain(vk::SurfaceKHR surface)
		: m_Surface(surface)
	{
	}

	bool VulkanSwapChain::Create(uint32_t width, uint32_t height)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Width = width;
		m_Height = height;

		Destroy();

		const auto& deviceParams = Application::Get().GetGraphicsDeviceManager()->GetDeviceParams();
		auto device = Application::Get().GetGraphicsDevice();

		VulkanDeviceManager* vulkanDeviceManager = (VulkanDeviceManager*)Application::Get().GetGraphicsDeviceManager();

		vk::SurfaceCapabilitiesKHR surfaceCaps;
		vk::Result capsRes = vulkanDeviceManager->m_VulkanPhysicalDevice.getSurfaceCapabilitiesKHR(m_Surface, &surfaceCaps);
		if (capsRes != vk::Result::eSuccess)
		{
			LUX_CORE_ERROR("VulkanSwapChain::Create - getSurfaceCapabilitiesKHR failed: {}", nvrhi::vulkan::resultToString(VkResult(capsRes)));
			return false;
		}

		// currentExtent == 0xFFFFFFFF means "the surface size is whatever the swap chain
		// asks for" (typical on Wayland). Otherwise the surface dictates the size and the
		// requested one must be ignored.
		if (surfaceCaps.currentExtent.width != 0xFFFFFFFF)
		{
			m_Width = surfaceCaps.currentExtent.width;
			m_Height = surfaceCaps.currentExtent.height;
		}

		// imageExtent must lie within [minImageExtent, maxImageExtent] (VUID-01274).
		// Violating this is reported as VK_ERROR_OUT_OF_DEVICE_MEMORY by some drivers.
		m_Width = std::clamp(m_Width, surfaceCaps.minImageExtent.width, surfaceCaps.maxImageExtent.width);
		m_Height = std::clamp(m_Height, surfaceCaps.minImageExtent.height, surfaceCaps.maxImageExtent.height);

		if (m_Width == 0 || m_Height == 0)
		{
			LUX_CORE_WARN("VulkanSwapChain::Create - surface extent is 0x0, deferring swap chain creation.");
			return false;
		}

		LUX_CORE_INFO("VulkanSwapChain::Create - extent={}x{}, surfaceCaps: minImages={}, maxImages={}, supportedTransforms={:#x}, supportedCompositeAlpha={:#x}, supportedUsageFlags={:#x}",
			m_Width, m_Height,
			surfaceCaps.minImageCount, surfaceCaps.maxImageCount,
			(uint32_t)surfaceCaps.supportedTransforms,
			(uint32_t)surfaceCaps.supportedCompositeAlpha,
			(uint32_t)surfaceCaps.supportedUsageFlags);

		m_SwapChainFormat = {
			vk::Format(nvrhi::vulkan::convertFormat(deviceParams.swapChainFormat)),
			vk::ColorSpaceKHR::eSrgbNonlinear
		};

		vk::Extent2D extent = vk::Extent2D(m_Width, m_Height);

		uint32_t minImages = deviceParams.swapChainBufferCount;
		if (capsRes == vk::Result::eSuccess)
		{
			minImages = std::max(minImages, surfaceCaps.minImageCount);
			if (surfaceCaps.maxImageCount > 0)
				minImages = std::min(minImages, surfaceCaps.maxImageCount);
		}

		std::unordered_set<uint32_t> uniqueQueues = {
			uint32_t(vulkanDeviceManager->m_QueueFamilyIndices.Graphics),
			uint32_t(vulkanDeviceManager->m_QueueFamilyIndices.Present) };

		std::vector<uint32_t> queues = Utils::SetToVector(uniqueQueues);

		const bool enableSwapChainSharing = queues.size() > 1;

		vk::SurfaceTransformFlagBitsKHR preTransform = surfaceCaps.currentTransform;

		vk::CompositeAlphaFlagBitsKHR compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
		if (!(surfaceCaps.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eOpaque))
		{
			if (surfaceCaps.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit)
				compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eInherit;
			else if (surfaceCaps.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
				compositeAlpha = vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
			else if (surfaceCaps.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied)
				compositeAlpha = vk::CompositeAlphaFlagBitsKHR::ePostMultiplied;
		}

		vk::ImageUsageFlags imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
		imageUsage &= surfaceCaps.supportedUsageFlags;

		// Present mode:
		//  - VSync on  -> FIFO: blocks on vblank, no tearing (guaranteed available by the spec).
		//  - VSync off -> prefer MAILBOX (uncapped AND tear-free), then IMMEDIATE (uncapped, may
		//                 tear), falling back to FIFO if neither is exposed. Immediate-only used to
		//                 mean the choice was tear-or-vsync; Mailbox gives uncapped fps without tearing.
		vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
		{
			const auto availableModes = vulkanDeviceManager->m_VulkanPhysicalDevice.getSurfacePresentModesKHR(m_Surface);
			const auto isSupported = [&](vk::PresentModeKHR mode)
			{
				return std::find(availableModes.begin(), availableModes.end(), mode) != availableModes.end();
			};

			if (!deviceParams.vsyncEnabled)
			{
				if (isSupported(vk::PresentModeKHR::eMailbox))
					presentMode = vk::PresentModeKHR::eMailbox;
				else if (isSupported(vk::PresentModeKHR::eImmediate))
					presentMode = vk::PresentModeKHR::eImmediate;
				// else: neither uncapped mode available — stay on FIFO.
			}
		}

		// Diagnostic: log the chosen present mode and everything the surface exposes. A hard 120fps
		// (or refresh-rate) cap means FIFO — either vsync is on, or Immediate/Mailbox aren't exposed
		// for this windowed surface and it fell back to FIFO.
		{
			const auto modes = vulkanDeviceManager->m_VulkanPhysicalDevice.getSurfacePresentModesKHR(m_Surface);
			std::string supported;
			for (auto m : modes)
				supported += vk::to_string(m) + " ";
			LUX_CORE_INFO("VulkanSwapChain::Create - present mode = {} (vsync={}); surface supports: {}",
				vk::to_string(presentMode), deviceParams.vsyncEnabled, supported);
		}

		auto desc = vk::SwapchainCreateInfoKHR()
			.setSurface(m_Surface)
			.setMinImageCount(minImages)
			.setImageFormat(m_SwapChainFormat.format)
			.setImageColorSpace(m_SwapChainFormat.colorSpace)
			.setImageExtent(extent)
			.setImageArrayLayers(1)
			.setImageUsage(imageUsage)
			.setImageSharingMode(enableSwapChainSharing ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive)
			.setFlags(vulkanDeviceManager->m_SwapChainMutableFormatSupported ? vk::SwapchainCreateFlagBitsKHR::eMutableFormat : vk::SwapchainCreateFlagBitsKHR(0))
			.setQueueFamilyIndexCount(enableSwapChainSharing ? uint32_t(queues.size()) : 0)
			.setPQueueFamilyIndices(enableSwapChainSharing ? queues.data() : nullptr)
			.setPreTransform(preTransform)
			.setCompositeAlpha(compositeAlpha)
			.setPresentMode(presentMode)
			.setClipped(true)
			.setOldSwapchain(nullptr);

		std::vector<vk::Format> imageFormats = { m_SwapChainFormat.format };
		switch (m_SwapChainFormat.format)
		{
			case vk::Format::eR8G8B8A8Unorm:
				imageFormats.push_back(vk::Format::eR8G8B8A8Srgb);
				break;
			case vk::Format::eR8G8B8A8Srgb:
				imageFormats.push_back(vk::Format::eR8G8B8A8Unorm);
				break;
			case vk::Format::eB8G8R8A8Unorm:
				imageFormats.push_back(vk::Format::eB8G8R8A8Srgb);
				break;
			case vk::Format::eB8G8R8A8Srgb:
				imageFormats.push_back(vk::Format::eB8G8R8A8Unorm);
				break;
		}

		auto imageFormatListCreateInfo = vk::ImageFormatListCreateInfo()
			.setViewFormats(imageFormats);

		if (vulkanDeviceManager->m_SwapChainMutableFormatSupported)
			desc.pNext = &imageFormatListCreateInfo;

		const vk::Result res = vulkanDeviceManager->m_VulkanDevice.createSwapchainKHR(&desc, nullptr, &m_SwapChain);
		if (res != vk::Result::eSuccess)
		{
			LUX_CORE_ERROR("Failed to create a Vulkan swap chain, error code = {}. extent={}x{}, minImages={}, format={}, preTransform={:#x}, compositeAlpha={:#x}, presentMode={}, mutableFormat={}",
				nvrhi::vulkan::resultToString(VkResult(res)),
				m_Width, m_Height, minImages, (int)m_SwapChainFormat.format,
				(uint32_t)preTransform, (uint32_t)compositeAlpha,
				(int)desc.presentMode, vulkanDeviceManager->m_SwapChainMutableFormatSupported);
			return false;
		}
		// retrieve swap chain images
		uint32_t imageCount = 0;
		vulkanDeviceManager->m_VulkanDevice.getSwapchainImagesKHR(m_SwapChain, &imageCount, nullptr);
		std::vector<vk::Image> images(imageCount);
		vulkanDeviceManager->m_VulkanDevice.getSwapchainImagesKHR(m_SwapChain, &imageCount, images.data());
		for (auto image : images)
		{
			SwapChainImage sci;
			sci.image = image;

			nvrhi::TextureDesc textureDesc;
			textureDesc.width = m_Width;
			textureDesc.height = m_Height;
			textureDesc.format = deviceParams.swapChainFormat;
			textureDesc.debugName = "Swap chain image";
			textureDesc.initialState = nvrhi::ResourceStates::Present;
			textureDesc.keepInitialState = true;
			textureDesc.isRenderTarget = true;

			sci.rhiHandle = device->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(sci.image), textureDesc);
			m_SwapChainImages.push_back(sci);
		}

		m_SwapChainIndex = 0;

		// Create acquire semaphores (for frame synchronization)
		{
			vk::SemaphoreCreateInfo semCI;
			for (uint32_t i = 0; i < 3; ++i)
			{
				const vk::Result semRes = vulkanDeviceManager->m_VulkanDevice.createSemaphore(&semCI, nullptr, &m_AcquireSemaphores[i]);
				LUX_CORE_VERIFY(semRes == vk::Result::eSuccess);
			}

			// Create one present semaphore per swapchain image to avoid reuse conflicts
			// (see: https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)
			m_PresentSemaphores.resize(m_SwapChainImages.size());
			for (size_t i = 0; i < m_SwapChainImages.size(); ++i)
			{
				const vk::Result semRes = vulkanDeviceManager->m_VulkanDevice.createSemaphore(&semCI, nullptr, &m_PresentSemaphores[i]);
				LUX_CORE_VERIFY(semRes == vk::Result::eSuccess);
			}
		}

		BackBufferResized();

		m_NeedsRecreate = false;
		return true;
	}

	void VulkanSwapChain::Destroy()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		VulkanDeviceManager* vulkanDeviceManager = (VulkanDeviceManager*)Application::Get().GetGraphicsDeviceManager();
		if (vulkanDeviceManager->m_VulkanDevice)
		{
			vulkanDeviceManager->m_VulkanDevice.waitIdle();
		}

		// The nvrhi framebuffers and texture handles own image views onto the swap chain's images,
		// so they have to be released - and actually collected, since nvrhi defers destruction -
		// before vkDestroySwapchainKHR takes those images away.
		// NOTE: BackBufferResizing() also clears the framebuffers on the OnResize() path; doing it
		//       here as well keeps Destroy() correct on every path (shutdown, Resize(), re-entry).
		m_SwapChainFramebuffers.clear();
		m_SwapChainImages.clear();

		if (nvrhi::IDevice* device = vulkanDeviceManager->GetDevice())
			device->runGarbageCollection();

		if (m_SwapChain)
		{
			vulkanDeviceManager->m_VulkanDevice.destroySwapchainKHR(m_SwapChain);
			m_SwapChain = nullptr;
		}

		for (auto& semaphore : m_PresentSemaphores)
		{
			if (semaphore)
			{
				vulkanDeviceManager->m_VulkanDevice.destroySemaphore(semaphore);
			}
		}
		m_PresentSemaphores.clear();

		for (auto& semaphore : m_AcquireSemaphores)
		{
			if (semaphore)
			{
				vulkanDeviceManager->m_VulkanDevice.destroySemaphore(semaphore);
				semaphore = vk::Semaphore();
			}
		}

		// Aliases one of m_AcquireSemaphores, so it dangles once those are destroyed
		m_AcquiredSemaphore = vk::Semaphore();
	}

	void VulkanSwapChain::OnResize(uint32_t width, uint32_t height)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Width = width;
		m_Height = height;
		BackBufferResizing();
		Resize();
	}

	void VulkanSwapChain::Resize()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Destroy();
		Create(m_Width, m_Height);
	}

	bool VulkanSwapChain::BeginFrame()
	{
		LUX_PROFILE_FUNCTION("VulkanSwapChain::BeginFrame");

		auto device = (nvrhi::vulkan::IDevice*)Application::Get().GetGraphicsDevice().Get();
		VulkanDeviceManager* vulkanDeviceManager = (VulkanDeviceManager*)Application::Get().GetGraphicsDeviceManager();

		const auto& semaphore = m_AcquireSemaphores[m_AcquireSemaphoreIndex];

		const vk::Result res = vulkanDeviceManager->m_VulkanDevice.acquireNextImageKHR(
			m_SwapChain,
			std::numeric_limits<uint64_t>::max(), // timeout
			semaphore,
			vk::Fence(),
			&m_SwapChainIndex);

		m_AcquiredSemaphore = semaphore;
		m_AcquireSemaphoreIndex = (m_AcquireSemaphoreIndex + 1) % m_AcquireSemaphores.size();

		// Recreation is NEVER done here: destroying the swapchain would waitIdle and then destroy
		// the acquire semaphores, but vkAcquireNextImageKHR signals a binary semaphore from the
		// presentation engine and waitIdle does not clear that pending signal — destroying it is
		// undefined behaviour and crashes the driver (the resize crash). Instead flag it and let
		// Window::ProcessEvents recreate at the next point where both threads are idle.
		if (res == vk::Result::eErrorOutOfDateKHR)
		{
			// The acquire failed: no image, and 'semaphore' was NOT signaled. We cannot render this
			// frame — skip it. The swapchain is rebuilt before the next acquire.
			m_NeedsRecreate = true;
			return false;
		}

		if (res == vk::Result::eSuboptimalKHR)
		{
			// The acquire SUCCEEDED and signaled 'semaphore'; the image is still presentable. Render
			// and present this frame as usual, and rebuild afterwards at the safe boundary.
			m_NeedsRecreate = true;
			return true;
		}

		return res == vk::Result::eSuccess;
	}

	void VulkanSwapChain::Present()
	{
		LUX_PROFILE_FUNCTION("VulkanSwapChain::Present");

		VulkanDeviceManager* vulkanDeviceManager = (VulkanDeviceManager*)Application::Get().GetGraphicsDeviceManager();
		auto device = (nvrhi::vulkan::IDevice*)Application::Get().GetGraphicsDevice().Get();

		// Use the semaphore corresponding to the current swapchain image index
		// This ensures proper synchronization without semaphore reuse conflicts
		const auto& semaphore = m_PresentSemaphores[m_SwapChainIndex];

		RenderCommandBuffer::LockQueue();
		device->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);
		device->executeCommandLists(nullptr, 0);

		{
			LUX_PROFILE_SCOPE("PresentKHR");
			vk::PresentInfoKHR info = vk::PresentInfoKHR()
				.setWaitSemaphoreCount(1)
				.setPWaitSemaphores(&semaphore)
				.setSwapchainCount(1)
				.setPSwapchains(&m_SwapChain)
				.setPImageIndices(&m_SwapChainIndex);

			const vk::Result res = vulkanDeviceManager->m_PresentQueue.presentKHR(&info);
			// eSuboptimal/eOutOfDate here just mean the surface changed size (resize); flag for a
			// deferred recreate rather than tearing down the swapchain from inside Present.
			if (res == vk::Result::eErrorOutOfDateKHR || res == vk::Result::eSuboptimalKHR)
				m_NeedsRecreate = true;
			else
				LUX_CORE_VERIFY(res == vk::Result::eSuccess);
		}

		RenderCommandBuffer::UnlockQueue();

#ifndef _WIN32
		if (vulkanDeviceManager->m_DeviceParams.vsyncEnabled)
		{
			vulkanDeviceManager->m_PresentQueue.waitIdle();
		}
#endif

		{
			LUX_PROFILE_SCOPE("WaitEventQuery");

			while (m_FramesInFlight.size() >= vulkanDeviceManager->m_DeviceParams.maxFramesInFlight)
			{
				auto query = m_FramesInFlight.front();
				m_FramesInFlight.pop();

				device->waitEventQuery(query);

				m_QueryPool.push_back(query);
			}
		}

		{
			LUX_PROFILE_SCOPE("OtherEventQuery");
			nvrhi::EventQueryHandle query;
			if (!m_QueryPool.empty())
			{
				query = m_QueryPool.back();
				m_QueryPool.pop_back();
			}
			else
			{
				query = device->createEventQuery();
			}

			device->resetEventQuery(query);
			device->setEventQuery(query, nvrhi::CommandQueue::Graphics);
			m_FramesInFlight.push(query);
		}
	}

	void VulkanSwapChain::BackBufferResizing()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_SwapChainFramebuffers.clear();
	}

	void VulkanSwapChain::BackBufferResized()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		auto device = Application::Get().GetGraphicsDevice();
		uint32_t backBufferCount = GetBackBufferCount();
		m_SwapChainFramebuffers.resize(backBufferCount);
		for (uint32_t index = 0; index < backBufferCount; index++)
		{
			m_SwapChainFramebuffers[index] = device->createFramebuffer(
				nvrhi::FramebufferDesc().addColorAttachment(GetBackBuffer(index)));
		}
	}

	nvrhi::ITexture* VulkanSwapChain::GetCurrentBackBuffer()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_SwapChainImages[m_SwapChainIndex].rhiHandle;
	}

	nvrhi::ITexture* VulkanSwapChain::GetBackBuffer(uint32_t index)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (index < m_SwapChainImages.size())
			return m_SwapChainImages[index].rhiHandle;
		return nullptr;
	}

	uint32_t VulkanSwapChain::GetCurrentBackBufferIndex()
	{
		return m_SwapChainIndex;
	}

	uint32_t VulkanSwapChain::GetBackBufferCount()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return uint32_t(m_SwapChainImages.size());
	}

	nvrhi::IFramebuffer* VulkanSwapChain::GetCurrentFramebuffer()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return GetFramebuffer(GetCurrentBackBufferIndex());
	}

	nvrhi::IFramebuffer* VulkanSwapChain::GetFramebuffer(uint32_t index)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (index < m_SwapChainFramebuffers.size())
			return m_SwapChainFramebuffers[index];

		return nullptr;
	}


}


