// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/RHI/ResourceStateTracker.h"

#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

struct GLFWwindow;

namespace nri {
	struct Descriptor;
	struct Fence;
	struct SwapChain;
	struct Texture;
	struct Window;
	enum class Format : uint8_t;
}

namespace Lux {

	// The NRI description of a GLFW window: its HWND, or its X11 or Wayland handles. Implemented per
	// platform (Core/Platform/<OS>/<OS>NativeWindow.cpp), the only translation units that include
	// the native window-system headers (X11 defines macros such as None, Bool and Window).
	nri::Window GetNRIWindow(GLFWwindow* window);

	// A window's swapchain, on NRI's SwapChain extension (NRI migration Phase 14). The main window,
	// each ImGui platform window and the runtime player present through one; NRI owns the surface.
	//
	// Threading: Create, OnResize and Destroy run on the main thread while the render thread is idle
	// (Window::ProcessEvents, the ImGui platform-window callbacks). BeginFrame, Present and the
	// back-buffer accessors run on the render thread. Neither BeginFrame nor Present recreates the
	// swapchain: they flag it (NeedsRecreate) for the next idle point, because an acquire semaphore
	// with a pending signal must not be destroyed.
	class RHISwapChain
	{
	public:
		explicit RHISwapChain(GLFWwindow* window);
		~RHISwapChain();

		RHISwapChain(const RHISwapChain&) = delete;
		RHISwapChain& operator=(const RHISwapChain&) = delete;

		// Builds the swapchain at the window's framebuffer size, with the image count and present
		// mode of the device parameters. False while the framebuffer is 0x0 (minimized) or when NRI
		// fails (logged); BeginFrame then flags a retry.
		bool Create();
		void Destroy();
		// Rebuilds the swapchain for the window's current size and present settings. A minimized
		// window keeps its swapchain, flagged for recreation: the render thread may still have a
		// frame queued for it.
		void OnResize();

		// Acquires the next back buffer. False when nothing was acquired; the frame is then skipped.
		bool BeginFrame();
		void Present();

		uint32_t GetCurrentBackBufferIndex() const { return m_BackBufferIndex; }
		uint32_t GetBackBufferCount() const { return static_cast<uint32_t>(m_BackBuffers.size()); }
		// The acquired back buffer's acquire semaphore, handed out once per frame: the first
		// submission that touches the back buffer waits for it (RenderCommandBuffer::RT_Submit), and
		// Present waits for it when nothing did. Null after that. Render thread, after BeginFrame.
		nri::Fence* TakeAcquireSemaphore() { return std::exchange(m_AcquiredSemaphore, nullptr); }

		// The acquired back buffer's color-attachment view, and the back buffer as the resource state
		// tracker sees it (resting in Present). Render thread, after BeginFrame; null / empty before
		// the first acquire.
		nri::Descriptor* GetCurrentNRIColorAttachment() const;
		TrackedTexture GetCurrentTrackedBackBuffer() const;
		// The NRI format of the back buffers (and of their color-attachment views).
		nri::Format GetNRIColorFormat() const { return m_Format; }

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		// True when the swapchain must be rebuilt: it went out of date, could not be created, or a
		// minimized window kept it. Cleared by a successful Create().
		bool NeedsRecreate() const { return m_NeedsRecreate; }

	private:
		struct BackBuffer
		{
			// Owned by the NRI swapchain.
			nri::Texture* Texture = nullptr;
			nri::Descriptor* ColorAttachment = nullptr;
			TrackedTexture Tracked;
		};

		GLFWwindow* m_Window = nullptr;
		nri::SwapChain* m_SwapChain = nullptr;
		uint32_t m_Width = 0, m_Height = 0;
		nri::Format m_Format = {};

		std::vector<BackBuffer> m_BackBuffers;
		uint32_t m_BackBufferIndex = 0;

		// Binary semaphores (NRI SWAPCHAIN_SEMAPHORE fences). The acquire semaphores are a ring, used
		// in turn; one present semaphore per back buffer avoids reusing one a present still waits for.
		std::vector<nri::Fence*> m_AcquireSemaphores;
		std::vector<nri::Fence*> m_PresentSemaphores;
		uint32_t m_AcquireSemaphoreIndex = 0;
		// Aliases one of m_AcquireSemaphores until it is taken.
		nri::Fence* m_AcquiredSemaphore = nullptr;

		bool m_NeedsRecreate = false;
		// Render thread only: set by a successful BeginFrame, consumed by Present.
		bool m_ImageAcquired = false;

		// Graphics timeline values of the presented frames still on the GPU.
		std::queue<uint64_t> m_FramesInFlight;
	};

}
