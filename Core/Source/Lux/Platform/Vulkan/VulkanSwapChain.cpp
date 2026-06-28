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

		m_SwapChainFormat = {
			vk::Format(nvrhi::vulkan::convertFormat(deviceParams.swapChainFormat)),
			vk::ColorSpaceKHR::eSrgbNonlinear
		};

		vk::Extent2D extent = vk::Extent2D(m_Width, m_Height);

		std::unordered_set<uint32_t> uniqueQueues = {
			uint32_t(vulkanDeviceManager->m_QueueFamilyIndices.Graphics),
			uint32_t(vulkanDeviceManager->m_QueueFamilyIndices.Present) };

		std::vector<uint32_t> queues = Utils::SetToVector(uniqueQueues);

		const bool enableSwapChainSharing = queues.size() > 1;

		auto desc = vk::SwapchainCreateInfoKHR()
			.setSurface(m_Surface)
			.setMinImageCount(deviceParams.swapChainBufferCount)
			.setImageFormat(m_SwapChainFormat.format)
			.setImageColorSpace(m_SwapChainFormat.colorSpace)
			.setImageExtent(extent)
			.setImageArrayLayers(1)
			.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled)
			.setImageSharingMode(enableSwapChainSharing ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive)
			.setFlags(vulkanDeviceManager->m_SwapChainMutableFormatSupported ? vk::SwapchainCreateFlagBitsKHR::eMutableFormat : vk::SwapchainCreateFlagBitsKHR(0))
			.setQueueFamilyIndexCount(enableSwapChainSharing ? uint32_t(queues.size()) : 0)
			.setPQueueFamilyIndices(enableSwapChainSharing ? queues.data() : nullptr)
			.setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
			.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
			.setPresentMode(deviceParams.vsyncEnabled ? vk::PresentModeKHR::eFifo : vk::PresentModeKHR::eImmediate)
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
			LUX_CORE_ERROR("Failed to create a Vulkan swap chain, error code = {}", nvrhi::vulkan::resultToString(VkResult(res)));
			return false;
		}

		// retrieve swap chain images
		auto images = vulkanDeviceManager->m_VulkanDevice.getSwapchainImagesKHR(m_SwapChain);
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
		for (uint32_t i = 0; i < 3; ++i)
		{
			m_AcquireSemaphores[i] = vulkanDeviceManager->m_VulkanDevice.createSemaphore(vk::SemaphoreCreateInfo());
		}

		// Create one present semaphore per swapchain image to avoid reuse conflicts
		// (see: https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)
		m_PresentSemaphores.resize(m_SwapChainImages.size());
		for (size_t i = 0; i < m_SwapChainImages.size(); ++i)
		{
			m_PresentSemaphores[i] = vulkanDeviceManager->m_VulkanDevice.createSemaphore(vk::SemaphoreCreateInfo());
		}

		BackBufferResized();
	}

	void VulkanSwapChain::Destroy()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		VulkanDeviceManager* vulkanDeviceManager = (VulkanDeviceManager*)Application::Get().GetGraphicsDeviceManager();
		if (vulkanDeviceManager->m_VulkanDevice)
		{
			vulkanDeviceManager->m_VulkanDevice.waitIdle();
		}

		if (m_SwapChain)
		{
			vulkanDeviceManager->m_VulkanDevice.destroySwapchainKHR(m_SwapChain);
			m_SwapChain = nullptr;
		}

		m_SwapChainImages.clear();

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

		vk::Result res;

		int const maxAttempts = 3;
		for (int attempt = 0; attempt < maxAttempts; ++attempt)
		{
			res = vulkanDeviceManager->m_VulkanDevice.acquireNextImageKHR(
				m_SwapChain,
				std::numeric_limits<uint64_t>::max(), // timeout
				semaphore,
				vk::Fence(),
				&m_SwapChainIndex);

			m_AcquiredSemaphore = semaphore;

			if (res == vk::Result::eErrorOutOfDateKHR && attempt < maxAttempts)
			{
				BackBufferResizing();
				auto surfaceCaps = vulkanDeviceManager->m_VulkanPhysicalDevice.getSurfaceCapabilitiesKHR(m_Surface);

				m_Width = surfaceCaps.currentExtent.width;
				m_Height = surfaceCaps.currentExtent.height;

				Resize();
				BackBufferResized();
			}
			else
			{
				break;
			}
		}

		m_AcquireSemaphoreIndex = (m_AcquireSemaphoreIndex + 1) % m_AcquireSemaphores.size();

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
			LUX_CORE_VERIFY(res == vk::Result::eSuccess || res == vk::Result::eErrorOutOfDateKHR);
		}

		RenderCommandBuffer::UnlockQueue();

#ifndef _WIN32
		if (deviceParams.vsyncEnabled)
		{
			m_PresentQueue.waitIdle();
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
		LUX_PROFILE_FUNCTION_AUTO;
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


