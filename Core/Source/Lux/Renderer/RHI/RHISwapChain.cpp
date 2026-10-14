// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "RHISwapChain.h"

#include "Lux/Core/Application.h"
#include "Lux/Debug/Profiler.h"
#include "Lux/Platform/Vulkan/VulkanDevice.h"
#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Renderer/RHI/RHIQueueTimeline.h"
#include "Lux/Renderer/RHI/UploadContext.h"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace Lux {

	namespace {

		// The present mode (Rendering.md § Present mode). NRI picks it from these flags:
		// VSYNC is FIFO; without it, ALLOW_TEARING is IMMEDIATE and no flag is MAILBOX, each falling
		// back to FIFO when the surface lacks the mode. VSYNC with ALLOW_TEARING would be
		// FIFO_RELAXED, which Lux does not use.
		nri::SwapChainBits GetPresentFlags(const DeviceCreationParameters& params)
		{
			if (params.vsyncEnabled)
				return nri::SwapChainBits::VSYNC;
			return params.preferImmediatePresentMode ? nri::SwapChainBits::ALLOW_TEARING : nri::SwapChainBits::NONE;
		}

		const char* GetPresentModeName(nri::SwapChainBits flags)
		{
			if (flags & nri::SwapChainBits::VSYNC)
				return "FIFO";
			return (flags & nri::SwapChainBits::ALLOW_TEARING) ? "IMMEDIATE" : "MAILBOX";
		}

		const DeviceCreationParameters& GetDeviceParams()
		{
			return Application::Get().GetGraphicsDeviceManager()->GetDeviceParams();
		}

	}

	RHISwapChain::RHISwapChain(GLFWwindow* window)
		: m_Window(window)
	{
	}

	RHISwapChain::~RHISwapChain()
	{
		Destroy();
	}

	bool RHISwapChain::Create()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Destroy();

		// The framebuffer size, in pixels: the window size differs from it under display scaling.
		int width = 0, height = 0;
		glfwGetFramebufferSize(m_Window, &width, &height);
		if (width <= 0 || height <= 0)
			return false;

		const DeviceCreationParameters& params = GetDeviceParams();
		const NRIInterface& api = RHIDevice::API();

		nri::SwapChainDesc desc = {};
		desc.window = GetNRIWindow(m_Window);
		desc.queue = RHIDevice::GetQueue(GPUQueue::Graphics);
		desc.width = static_cast<nri::Dim_t>(width);
		desc.height = static_cast<nri::Dim_t>(height);
		// NRI clamps the count to the surface's range (logged as a warning).
		desc.textureNum = static_cast<uint8_t>(params.swapChainBufferCount);
		// 8-bit UNORM in the sRGB color space, as before: the renderer writes display-encoded color.
		desc.format = nri::SwapChainFormat::BT709_G22_8BIT;
		desc.flags = GetPresentFlags(params);
		desc.queuedFrameNum = static_cast<uint8_t>(params.maxFramesInFlight);
		if (api.CreateSwapChain(RHIDevice::Get(), desc, m_SwapChain) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "NRI failed to create a {}x{} swap chain", width, height);
			m_SwapChain = nullptr;
			return false;
		}

		uint32_t textureNum = 0;
		nri::Texture* const* textures = api.GetSwapChainTextures(*m_SwapChain, textureNum);
		LUX_CORE_VERIFY(textureNum > 0, "The NRI swap chain has no textures");

		// NRI may have snapped the extent to the surface's.
		const nri::TextureDesc& textureDesc = api.GetTextureDesc(*textures[0]);
		m_Width = textureDesc.width;
		m_Height = textureDesc.height;
		m_Format = textureDesc.format;

		m_BackBuffers.resize(textureNum);
		for (uint32_t i = 0; i < textureNum; i++)
		{
			BackBuffer& backBuffer = m_BackBuffers[i];
			backBuffer.Texture = textures[i];
			backBuffer.Tracked.Handle = textures[i];
			backBuffer.Tracked.RestingState = ResourceState::Present;
			// The image starts undefined; the upload batch moves it to Present before any submission.
			Renderer::RecordResourceUpload([&](UploadContext& upload)
				{
					upload.InitializeTexture(backBuffer.Tracked);
				});

			nri::TextureViewDesc viewDesc = {};
			viewDesc.texture = textures[i];
			viewDesc.type = nri::TextureView::COLOR_ATTACHMENT;
			viewDesc.format = m_Format;
			viewDesc.mipNum = 1;
			viewDesc.layerNum = 1;
			LUX_CORE_VERIFY(api.CreateTextureView(viewDesc, backBuffer.ColorAttachment) == nri::Result::SUCCESS, "Failed to create a swap chain color-attachment view");
		}

		const auto createSemaphore = [&]()
			{
				nri::Fence* semaphore = nullptr;
				LUX_CORE_VERIFY(api.CreateFence(RHIDevice::Get(), nri::SWAPCHAIN_SEMAPHORE, semaphore) == nri::Result::SUCCESS, "Failed to create a swap chain semaphore");
				return semaphore;
			};
		// An acquire semaphore is reused only after the frame that waited for it has completed:
		// Present keeps at most maxFramesInFlight frames on the GPU, so one more than that suffices.
		const uint32_t acquireNum = std::max(textureNum, params.maxFramesInFlight + 1);
		m_AcquireSemaphores.resize(acquireNum);
		std::generate(m_AcquireSemaphores.begin(), m_AcquireSemaphores.end(), createSemaphore);
		m_PresentSemaphores.resize(textureNum);
		std::generate(m_PresentSemaphores.begin(), m_PresentSemaphores.end(), createSemaphore);
		m_AcquireSemaphoreIndex = 0;
		m_BackBufferIndex = 0;

		// The mode NRI uses is the requested one when the surface has it, else FIFO.
		LUX_CORE_INFO_TAG("Renderer", "Swap chain: {}x{}, {} images, {} requested (vsync={})",
			m_Width, m_Height, textureNum, GetPresentModeName(desc.flags), params.vsyncEnabled);

		m_NeedsRecreate = false;
		return true;
	}

	void RHISwapChain::Destroy()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_SwapChain)
			return;

		// The open upload batch may hold the first transitions of these images (Create records them).
		// Submitted now, the wait below covers it; destroying the images under a batch still being
		// recorded would invalidate it, and every upload in it with it.
		Renderer::FlushResourceUploads();

		// Both threads are idle here; with the GPU idle too, nothing still uses the images or the
		// semaphores, so they go now rather than through the deletion queue.
		const NRIInterface& api = RHIDevice::API();
		api.DeviceWaitIdle(&RHIDevice::Get());

		for (BackBuffer& backBuffer : m_BackBuffers)
		{
			if (backBuffer.ColorAttachment)
				api.DestroyDescriptor(backBuffer.ColorAttachment);
		}
		m_BackBuffers.clear();

		// Destroys the back-buffer textures and the surface with it.
		api.DestroySwapChain(m_SwapChain);
		m_SwapChain = nullptr;

		for (nri::Fence* semaphore : m_AcquireSemaphores)
			api.DestroyFence(semaphore);
		m_AcquireSemaphores.clear();
		for (nri::Fence* semaphore : m_PresentSemaphores)
			api.DestroyFence(semaphore);
		m_PresentSemaphores.clear();

		m_AcquiredSemaphore = nullptr;
		m_ImageAcquired = false;
	}

	void RHISwapChain::OnResize()
	{
		LUX_PROFILE_FUNCTION_AUTO;

		// A minimized window has a 0x0 framebuffer, for which no swapchain can be built. Keep the
		// current one: the render thread still has a frame queued from before the minimize was seen,
		// and that frame acquires, renders to and presents THIS swapchain (its acquire then fails,
		// which BeginFrame handles). Window::ProcessEvents retries until the window is restored.
		int width = 0, height = 0;
		glfwGetFramebufferSize(m_Window, &width, &height);
		if (m_SwapChain && (width <= 0 || height <= 0))
		{
			m_NeedsRecreate = true;
			return;
		}

		Create();
	}

	bool RHISwapChain::BeginFrame()
	{
		LUX_PROFILE_FUNCTION("RHISwapChain::BeginFrame");
		m_ImageAcquired = false;

		// No swapchain exists while the framebuffer is 0x0 (e.g. the window started minimized).
		if (!m_SwapChain)
		{
			m_NeedsRecreate = true;
			return false;
		}

		nri::Fence* semaphore = m_AcquireSemaphores[m_AcquireSemaphoreIndex];
		uint32_t index = 0;
		const nri::Result result = RHIDevice::API().AcquireNextTexture(*m_SwapChain, *semaphore, index);
		if (result == nri::Result::DEVICE_LOST)
			VulkanDevice::ReportDeviceLost("AcquireNextTexture");

		if (result != nri::Result::SUCCESS)
		{
			// Nothing was acquired and the semaphore has no signal pending, so the frame is skipped.
			// An out-of-date swapchain is rebuilt before the next acquire; a timed-out acquire
			// (logged by NRI) is retried.
			if (result == nri::Result::OUT_OF_DATE)
				m_NeedsRecreate = true;
			return false;
		}

		m_AcquiredSemaphore = semaphore;
		m_AcquireSemaphoreIndex = (m_AcquireSemaphoreIndex + 1) % static_cast<uint32_t>(m_AcquireSemaphores.size());
		m_BackBufferIndex = index;
		m_ImageAcquired = true;
		return true;
	}

	void RHISwapChain::Present()
	{
		LUX_PROFILE_FUNCTION("RHISwapChain::Present");

		// Presenting an image that was never acquired (failed/skipped acquire) is invalid usage.
		if (!m_ImageAcquired)
			return;
		m_ImageAcquired = false;

		// Uploads first: a back buffer nothing rendered to still needs its first transition.
		Renderer::FlushResourceUploads();

		// A submission with no work signals the image's present semaphore after everything before
		// it, and waits for the acquire if nothing rendered this frame.
		nri::Fence* presentSemaphore = m_PresentSemaphores[m_BackBufferIndex];
		RHIQueueTimeline::Submission submission;
		submission.WaitSemaphore = TakeAcquireSemaphore();
		submission.SignalSemaphore = presentSemaphore;
		const uint64_t frameValue = RHIQueueTimeline::Submit(GPUQueue::Graphics, submission);
		if (!frameValue)
		{
			// Nothing will signal the present semaphore (logged by the submission).
			m_NeedsRecreate = true;
			return;
		}

		const NRIInterface& api = RHIDevice::API();
		nri::Result result = nri::Result::SUCCESS;
		RenderCommandBuffer::LockQueue();
		{
			LUX_PROFILE_SCOPE("QueuePresent");
			result = api.QueuePresent(*m_SwapChain, *presentSemaphore, 0);
		}
		RenderCommandBuffer::UnlockQueue();

		// Out of date just means the surface changed (a resize): flag a deferred recreate rather
		// than tearing the swapchain down from inside Present.
		if (result == nri::Result::OUT_OF_DATE)
			m_NeedsRecreate = true;
		else if (result == nri::Result::DEVICE_LOST)
			VulkanDevice::ReportDeviceLost("QueuePresent");
		else
			LUX_CORE_VERIFY(result == nri::Result::SUCCESS, "NRI failed to present (see the [NRI] error above)");

		const DeviceCreationParameters& params = GetDeviceParams();
#ifndef _WIN32
		// Preserved from the raw-Vulkan swapchain, not endorsed: with VSync on, Linux waits for the
		// graphics queue to go idle after every present.
		if (params.vsyncEnabled)
			api.QueueWaitIdle(RHIDevice::GetQueue(GPUQueue::Graphics));
#endif

		// Frame pacing: at most maxFramesInFlight presented frames on the GPU.
		{
			LUX_PROFILE_SCOPE("WaitForFramesInFlight");
			while (m_FramesInFlight.size() >= params.maxFramesInFlight)
			{
				RHIQueueTimeline::Wait(GPUQueue::Graphics, m_FramesInFlight.front());
				m_FramesInFlight.pop();
			}
		}
		m_FramesInFlight.push(frameValue);
	}

	nri::Descriptor* RHISwapChain::GetCurrentNRIColorAttachment() const
	{
		return m_BackBufferIndex < m_BackBuffers.size() ? m_BackBuffers[m_BackBufferIndex].ColorAttachment : nullptr;
	}

	TrackedTexture RHISwapChain::GetCurrentTrackedBackBuffer() const
	{
		return m_BackBufferIndex < m_BackBuffers.size() ? m_BackBuffers[m_BackBufferIndex].Tracked : TrackedTexture{};
	}

}
