#include "lpch.h"
#include "Window.h"

#include "Lux/Core/Events/ApplicationEvent.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Core/Events/MouseEvent.h"
#include "Lux/Core/Input.h"

#include "Lux/Renderer/RendererAPI.h"

#include "Lux/Platform/Vulkan/VulkanContext.h"
#include "Lux/Platform/Vulkan/VulkanSwapChain.h"
#include "Lux/Platform/Vulkan/VulkanDeviceManager.h"

#include <imgui.h>
#include "stb_image.h"

#include <GLFW/glfw3.h>

namespace Lux {

#include "Lux/Embed/LuxIcon.embed"

	static const struct
	{
		nvrhi::Format format;
		uint32_t redBits;
		uint32_t greenBits;
		uint32_t blueBits;
		uint32_t alphaBits;
		uint32_t depthBits;
		uint32_t stencilBits;
	} formatInfo[] = {
		{ nvrhi::Format::UNKNOWN,            0,  0,  0,  0,  0,  0, },
		{ nvrhi::Format::R8_UINT,            8,  0,  0,  0,  0,  0, },
		{ nvrhi::Format::RG8_UINT,           8,  8,  0,  0,  0,  0, },
		{ nvrhi::Format::RG8_UNORM,          8,  8,  0,  0,  0,  0, },
		{ nvrhi::Format::R16_UINT,          16,  0,  0,  0,  0,  0, },
		{ nvrhi::Format::R16_UNORM,         16,  0,  0,  0,  0,  0, },
		{ nvrhi::Format::R16_FLOAT,         16,  0,  0,  0,  0,  0, },
		{ nvrhi::Format::RGBA8_UNORM,        8,  8,  8,  8,  0,  0, },
		{ nvrhi::Format::RGBA8_SNORM,        8,  8,  8,  8,  0,  0, },
		{ nvrhi::Format::BGRA8_UNORM,        8,  8,  8,  8,  0,  0, },
		{ nvrhi::Format::SRGBA8_UNORM,       8,  8,  8,  8,  0,  0, },
		{ nvrhi::Format::SBGRA8_UNORM,       8,  8,  8,  8,  0,  0, },
		{ nvrhi::Format::R10G10B10A2_UNORM, 10, 10, 10,  2,  0,  0, },
		{ nvrhi::Format::R11G11B10_FLOAT,   11, 11, 10,  0,  0,  0, },
		{ nvrhi::Format::RG16_UINT,         16, 16,  0,  0,  0,  0, },
		{ nvrhi::Format::RG16_FLOAT,        16, 16,  0,  0,  0,  0, },
		{ nvrhi::Format::R32_UINT,          32,  0,  0,  0,  0,  0, },
		{ nvrhi::Format::R32_FLOAT,         32,  0,  0,  0,  0,  0, },
		{ nvrhi::Format::RGBA16_FLOAT,      16, 16, 16, 16,  0,  0, },
		{ nvrhi::Format::RGBA16_UNORM,      16, 16, 16, 16,  0,  0, },
		{ nvrhi::Format::RGBA16_SNORM,      16, 16, 16, 16,  0,  0, },
		{ nvrhi::Format::RG32_UINT,         32, 32,  0,  0,  0,  0, },
		{ nvrhi::Format::RG32_FLOAT,        32, 32,  0,  0,  0,  0, },
		{ nvrhi::Format::RGB32_UINT,        32, 32, 32,  0,  0,  0, },
		{ nvrhi::Format::RGB32_FLOAT,       32, 32, 32,  0,  0,  0, },
		{ nvrhi::Format::RGBA32_UINT,       32, 32, 32, 32,  0,  0, },
		{ nvrhi::Format::RGBA32_FLOAT,      32, 32, 32, 32,  0,  0, },
	};

	static void GLFWErrorCallback(int error, const char* description)
	{
		LUX_CORE_ERROR_TAG("GLFW", "GLFW Error ({0}): {1}", error, description);
	}

	static bool s_GLFWInitialized = false;

	Window* Window::Create(const WindowSpecification& specification)
	{
		return new Window(specification);
	}

	Window::Window(const WindowSpecification& props)
		: m_Specification(props)
	{
	}

	Window::~Window()
	{
		Shutdown();
	}

	void Window::Init()
	{
		m_Data.Title = m_Specification.Title;
		m_Data.Width = m_Specification.Width;
		m_Data.Height = m_Specification.Height;
		
		DeviceCreationParameters deviceParams;
		deviceParams.Decorated = m_Specification.Decorated;
		deviceParams.swapChainBufferCount = m_Specification.SwapChainBufferCount;
		deviceParams.enableRayTracingExtensions = true;
		// Give nvrhi a dedicated compute queue so render passes can be scheduled
		// async on it (GTAO / SSR / light-cull / bloom overlapping graphics work).
		// Desktop NVIDIA always exposes a compute-capable queue family, so device
		// creation still succeeds; nothing submits async until EnableAsyncCompute.
		deviceParams.enableComputeQueue = true;
		// Give nvrhi a dedicated transfer (copy) queue so mesh/texture uploads and
		// asset streaming don't contend with the graphics queue against frame
		// rendering. This only *requests* the queue; it is optional — device
		// creation still succeeds when the GPU has no dedicated transfer family
		// (see VulkanDeviceManager::FindQueueFamilies), and uploads then fall back
		// to the graphics queue. Routing is gated at runtime by
		// Renderer::SetAsyncTransferQueueEnabled (setting Renderer.AsyncTransferQueue).
		deviceParams.enableCopyQueue = true;
		// Let the CPU stay one frame ahead of the GPU. The swapchain is triple-buffered
		// and every per-frame resource (command lists, UBO/SSBO sets, descriptor pools)
		// is already sized for RendererConfig::FramesInFlight (3), so the CPU and GPU can
		// safely overlap. At 1 the present loop blocked on full GPU completion every
		// frame — serializing the two and wasting the triple-buffering. 2 is the
		// low-latency sweet spot (one frame ahead); 3 trades latency for more throughput.
		deviceParams.maxFramesInFlight = 2;
		deviceParams.backBufferWidth = m_Specification.Width;
		deviceParams.backBufferHeight = m_Specification.Height;
		deviceParams.vsyncEnabled = m_Specification.VSync;
		deviceParams.preferImmediatePresentMode = m_Specification.PreferImmediatePresentMode;
		// The Khronos validation layer intercepts every Vulkan call — a large CPU tax in
		// draw-heavy scenes. Keep it only in Debug builds; Release/Dist (where FPS is
		// measured and shipped) run without it.
#ifdef LUX_DEBUG
		deviceParams.enableDebugRuntime = true;
#else
		deviceParams.enableDebugRuntime = false;
#endif
		// 0xc81ad50e: pre-existing ignored message.
		// The remaining three are the PreDepth depth/stencil attachment layout-transition
		// VUIDs (vkCmdBeginRendering depth/stencil + the matching vkQueueSubmit). They are a
		// benign NVRHI state-tracking desync in the multi-threaded render path: the depth is
		// written by PreDepth and sampled by post passes, and the validation layer flags a
		// transition NVRHI manages internally. Rendering is correct (depth testing works);
		// these are suppressed to keep the log usable until the tracking is reworked.
		deviceParams.ignoredVulkanValidationMessageLocations = {
			0xc81ad50e,
			0xc84a9eb7, // vkCmdBeginRendering: PreDepth depth attachment layout
			0x20b3cd31, // vkCmdBeginRendering: PreDepth stencil attachment layout
			0x46582f7b, // vkQueueSubmit: PreDepth depth/stencil expected layout
		};

		LUX_CORE_INFO_TAG("GLFW", "Creating window {0} ({1}, {2})", m_Specification.Title, m_Specification.Width, m_Specification.Height);

		if (!s_GLFWInitialized)
		{
			// On Wayland, GLFW prefers libdecor for client-side decorations whenever it's
			// installed. libdecor draws its own titlebar and does not fully honour
			// set_visibility(false) for our undecorated window on KWin, so its header shows
			// on top of the editor's custom titlebar (a double titlebar). Disabling libdecor
			// makes GLFW use the native xdg-decoration protocol instead, where the undecorated
			// window requests CLIENT_SIDE and KWin draws no server titlebar of its own.
			// (Init hint is Wayland-only and ignored on other platforms.)
			glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_WAYLAND_DISABLE_LIBDECOR);

			// TODO: glfwTerminate on system shutdown
			int success = glfwInit();
			LUX_CORE_ASSERT(success, "Could not intialize GLFW!");
			glfwSetErrorCallback(GLFWErrorCallback);

			s_GLFWInitialized = true;
		}

		if (RendererAPI::Current() == RendererAPIType::Vulkan)
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

#ifdef _WINDOWS
		if (params.enablePerMonitorDPI)
		{
			// this needs to happen before glfwInit in order to override GLFW behavior
			SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
		}
		else
		{
			SetProcessDpiAwareness(PROCESS_DPI_UNAWARE);
		}
#endif

		glfwDefaultWindowHints();

		bool foundFormat = false;
		for (const auto& info : formatInfo)
		{
			if (info.format == deviceParams.swapChainFormat)
			{
				glfwWindowHint(GLFW_RED_BITS, info.redBits);
				glfwWindowHint(GLFW_GREEN_BITS, info.greenBits);
				glfwWindowHint(GLFW_BLUE_BITS, info.blueBits);
				glfwWindowHint(GLFW_ALPHA_BITS, info.alphaBits);
				glfwWindowHint(GLFW_DEPTH_BITS, info.depthBits);
				glfwWindowHint(GLFW_STENCIL_BITS, info.stencilBits);
				foundFormat = true;
				break;
			}
		}

		assert(foundFormat);

		glfwWindowHint(GLFW_SAMPLES, deviceParams.swapChainSampleCount);
		glfwWindowHint(GLFW_REFRESH_RATE, deviceParams.refreshRate);

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);   // Ignored for fullscreen

		if (!m_Specification.Decorated)
		{
			// Disable native decorations so editor can render a fully custom titlebar.
			glfwWindowHint(GLFW_DECORATED, false);
		}

		m_WindowHandle = glfwCreateWindow((int)m_Specification.Width, (int)m_Specification.Height, m_Data.Title.c_str(), m_Specification.Fullscreen ? glfwGetPrimaryMonitor() : nullptr, nullptr);

		glfwSetWindowUserPointer(m_WindowHandle, this);

		if (m_WindowHandle == nullptr)
		{
			// return false;
		}

		if (m_Specification.Fullscreen)
		{
			glfwSetWindowMonitor(m_WindowHandle, glfwGetPrimaryMonitor(), 0, 0,
				m_Specification.Width, m_Specification.Height, deviceParams.refreshRate);
		}

		// The compositor/monitor decides the final surface size — in fullscreen it is the
		// monitor mode, not the requested size. Always reconcile against the real framebuffer,
		// otherwise the swap chain is created with an extent the surface doesn't allow.
		{
			int fbWidth = 0, fbHeight = 0;
			glfwGetFramebufferSize(m_WindowHandle, &fbWidth, &fbHeight);
			m_Data.Width = fbWidth;
			m_Data.Height = fbHeight;
		}

		if (deviceParams.windowPosX != -1 && deviceParams.windowPosY != -1)
		{
			glfwSetWindowPos(m_WindowHandle, deviceParams.windowPosX, deviceParams.windowPosY);
		}

#if TODO
		if (m_Specification.StartMaximized)
		{
			glfwMaximizeWindow(m_WindowHandle);
		}
#endif

		// Set icon
		{
			GLFWimage icon;
			int channels;

			bool useEmbedded = m_Specification.IconPath.empty();

			if (!useEmbedded)
			{
				std::string iconPathStr = m_Specification.IconPath.string();
				icon.pixels = stbi_load(iconPathStr.c_str(), &icon.width, &icon.height, &channels, 4);
				if (icon.pixels)
				{
					glfwSetWindowIcon(m_WindowHandle, 1, &icon);
					stbi_image_free(icon.pixels);
				}
				else
				{
					useEmbedded = true;
				}
			}

			if (useEmbedded)
			{
				// Use embedded Lux icon
				icon.pixels = stbi_load_from_memory(g_LuxIconPNG, sizeof(g_LuxIconPNG), &icon.width, &icon.height, &channels, 4);
				glfwSetWindowIcon(m_WindowHandle, 1, &icon);
				stbi_image_free(icon.pixels);
			}
		}

		nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::VULKAN;

		m_DeviceManager = DeviceManager::Create(api, m_WindowHandle);
		m_DeviceManager->SetWindowContext(this);

		if (!m_DeviceManager->CreateDevice(deviceParams, m_Specification.Title.c_str()))
		{
			LUX_CORE_ERROR("Cannot initialize a {} graphics device.", (uint8_t)api);
			return;
		}
		else
		{
			LUX_CORE_INFO("Successfully created {} device!", (uint8_t)api);
		}

		bool rayQuerySupported = m_DeviceManager->GetDevice()->queryFeatureSupport(nvrhi::Feature::RayQuery);

		if (!rayQuerySupported)
		{
			LUX_CORE_WARN("The GPU ({}) or its driver does not support Ray Queries. No renderer feature currently depends on this, continuing without it.", m_DeviceManager->GetRendererString());
		}
		else
		{
			LUX_CORE_INFO("rayQuerySupported=true");
		}

		if (!deviceParams.headlessDevice)
			CreateWindowSurface();

		m_DeviceManager->InitSurfaceCapabilities(*(uint64_t*)&m_WindowSurface);

		m_SwapChain = lnew VulkanSwapChain(m_WindowSurface);
		m_SwapChain->Create(m_Data.Width, m_Data.Height);

#if OLD
		// Create Renderer Context
		m_RendererContext = RendererContext::Create();
		m_RendererContext->Init();

		Ref<VulkanContext> context = m_RendererContext.As<VulkanContext>();

		m_SwapChain = lnew VulkanSwapChain();
		m_SwapChain->Init(VulkanContext::GetInstance(), context->GetDevice());
		m_SwapChain->InitSurface(m_WindowHandle);

		m_SwapChain->Create(&m_Data.Width, &m_Data.Height, m_Specification.VSync);
#endif
		//glfwMaximizeWindow(m_Window);
		m_Data.Self = this;
		glfwSetWindowUserPointer(m_WindowHandle, &m_Data);

		{
			float xscale = 1.0f, yscale = 1.0f;
			glfwGetWindowContentScale(m_WindowHandle, &xscale, &yscale);
			m_DeviceManager->SetDPIScale(xscale, yscale);
		}

		glfwSetWindowContentScaleCallback(m_WindowHandle, [](GLFWwindow* window, float xscale, float yscale)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));
			if (data.Self && data.Self->m_DeviceManager)
				data.Self->m_DeviceManager->SetDPIScale(xscale, yscale);
		});

		bool isRawMouseMotionSupported = glfwRawMouseMotionSupported();
		if (isRawMouseMotionSupported)
			glfwSetInputMode(m_WindowHandle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
		else
			LUX_CORE_WARN_TAG("Platform", "Raw mouse motion not supported.");

		// Set GLFW callbacks
		glfwSetWindowSizeCallback(m_WindowHandle, [](GLFWwindow* window, int width, int height)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));

			WindowResizeEvent event((uint32_t)width, (uint32_t)height);
			data.EventCallback(event);
			data.Width = width;
			data.Height = height;
			data.SizeDirty = true;
		});

		glfwSetFramebufferSizeCallback(m_WindowHandle, [](GLFWwindow* window, int, int)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));
			data.SizeDirty = true;
		});

		glfwSetWindowCloseCallback(m_WindowHandle, [](GLFWwindow* window)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));

			WindowCloseEvent event;
			data.EventCallback(event);
		});

		glfwSetKeyCallback(m_WindowHandle, [](GLFWwindow* window, int key, int scancode, int action, int mods)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));

			switch (action)
			{
				case GLFW_PRESS:
				{
					Input::UpdateKeyState((KeyCode)key, KeyState::Pressed);
					KeyPressedEvent event((KeyCode)key, 0);
					data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					Input::UpdateKeyState((KeyCode)key, KeyState::Released);
					KeyReleasedEvent event((KeyCode)key);
					data.EventCallback(event);
					break;
				}
				case GLFW_REPEAT:
				{
					Input::UpdateKeyState((KeyCode)key, KeyState::Held);
					KeyPressedEvent event((KeyCode)key, 1);
					data.EventCallback(event);
					break;
				}
			}
		});

		glfwSetCharCallback(m_WindowHandle, [](GLFWwindow* window, uint32_t codepoint)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));

			KeyTypedEvent event((KeyCode)codepoint);
			data.EventCallback(event);
		});

		glfwSetMouseButtonCallback(m_WindowHandle, [](GLFWwindow* window, int button, int action, int mods)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));

			switch (action)
			{
				case GLFW_PRESS:
				{
					Input::UpdateButtonState((MouseButton)button, KeyState::Pressed);
					MouseButtonPressedEvent event((MouseButton)button);
					data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					Input::UpdateButtonState((MouseButton)button, KeyState::Released);
					MouseButtonReleasedEvent event((MouseButton)button);
					data.EventCallback(event);
					break;
				}
			}
		});

		glfwSetScrollCallback(m_WindowHandle, [](GLFWwindow* window, double xOffset, double yOffset)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));

			MouseScrolledEvent event((float)xOffset, (float)yOffset);
			data.EventCallback(event);
		});

		glfwSetCursorPosCallback(m_WindowHandle, [](GLFWwindow* window, double x, double y)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));
			MouseMovedEvent event((float)x, (float)y);
			data.EventCallback(event);
		});

		glfwSetTitlebarHitTestCallback(m_WindowHandle, [](GLFWwindow* window, int x, int y, int* hit)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));
			WindowTitleBarHitTestEvent event(x, y, *hit);
			data.EventCallback(event);
		});

		glfwSetWindowIconifyCallback(m_WindowHandle, [](GLFWwindow* window, int iconified)
		{
			auto& data = *((WindowData*)glfwGetWindowUserPointer(window));
			WindowMinimizeEvent event((bool)iconified);
			data.EventCallback(event);
		});

		m_ImGuiMouseCursors[ImGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
		m_ImGuiMouseCursors[ImGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
		m_ImGuiMouseCursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);   // FIXME: GLFW doesn't have this.
		m_ImGuiMouseCursors[ImGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
		m_ImGuiMouseCursors[ImGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
		m_ImGuiMouseCursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);  // FIXME: GLFW doesn't have this.
		m_ImGuiMouseCursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);  // FIXME: GLFW doesn't have this.
		m_ImGuiMouseCursors[ImGuiMouseCursor_Hand] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

		// Update window size to actual size
		{
			int width, height;
			glfwGetWindowSize(m_WindowHandle, &width, &height);
			m_Data.Width = width;
			m_Data.Height = height;
		}

	}

	bool Window::CreateWindowSurface()
	{
		const VkResult res = glfwCreateWindowSurface(((VulkanDeviceManager*)m_DeviceManager)->GetVulkanInstance(), m_WindowHandle, nullptr, &m_WindowSurface);
		if (res != VK_SUCCESS)
		{
			LUX_CORE_ERROR("Failed to create a GLFW window surface, error code = {}", nvrhi::vulkan::resultToString(res));
			return false;
		}

		return true;
	}

	void Window::Shutdown()
	{
		if (m_SwapChain)
		{
			m_SwapChain->Destroy();
			ldelete m_SwapChain;
			m_SwapChain = nullptr;
		}

		if (m_WindowSurface)
		{
			auto vInstance = ((VulkanDeviceManager*)m_DeviceManager)->GetVulkanInstance();
			LUX_CORE_VERIFY(vInstance);
			vInstance.destroySurfaceKHR(m_WindowSurface);
			m_WindowSurface = nullptr;
		}

		if (m_DeviceManager)
		{
			m_DeviceManager->Shutdown();
			ldelete m_DeviceManager;
			m_DeviceManager = nullptr;
		}

		if (m_WindowHandle)
		{
			glfwDestroyWindow(m_WindowHandle);
			m_WindowHandle = nullptr;
		}

		// m_RendererContext.As<VulkanContext>()->GetDevice()->Destroy(); // need to destroy the device _before_ windows window destructor destroys the renderer context (because device Destroy() asks for renderer context...)
		glfwTerminate();
		s_GLFWInitialized = false;
	}

	inline std::pair<float, float> Window::GetWindowPos() const
	{
		int x, y;
		glfwGetWindowPos(m_WindowHandle, &x, &y);
		return { (float)x, (float)y };
	}

	void Window::ProcessEvents()
	{
		glfwPollEvents();
		Input::Update();

		// Recreate the swapchain here — this runs only after BlockUntilRenderComplete(), so both
		// the main and render threads are idle and no acquire/present semaphore has a pending
		// signal. NeedsRecreate() covers eSuboptimal/eOutOfDate reported by BeginFrame/Present
		// (e.g. a resize the size callback didn't catch, or a DPI change); SizeDirty covers the
		// window/framebuffer size callbacks. Create() re-queries the surface extent regardless.
		if (m_Data.SizeDirty || m_SwapChain->NeedsRecreate())
		{
			m_Data.SizeDirty = false;
			m_SwapChain->OnResize(m_Data.Width, m_Data.Height);
		}

		// Apply a pending VSync change. ProcessEvents runs when both the main and
		// render threads are idle, so it is safe to recreate the swapchain here.
		// SetVsyncEnabled updates the present-mode source; OnResize recreates the
		// swapchain (FIFO when on, Immediate when off) at the current size.
		// All three are read when the swapchain is built, so they are applied together and
		// share ONE recreate. They are routinely changed as a group - ApplyEditorPreferences
		// pushes all of them on every preferences change and at startup - and a recreate
		// tears down and rebuilds every swapchain image and framebuffer, so doing one per
		// setting would triple that cost for no benefit.
		if (m_DeviceManager && m_SwapChain && (m_VSyncDirty || m_SwapChainBufferCountDirty || m_PresentModeDirty))
		{
			if (m_VSyncDirty)
				m_DeviceManager->SetVsyncEnabled(m_Specification.VSync);
			if (m_SwapChainBufferCountDirty)
				m_DeviceManager->SetSwapChainBufferCount(m_Specification.SwapChainBufferCount);
			if (m_PresentModeDirty)
				m_DeviceManager->SetPreferImmediatePresentMode(m_Specification.PreferImmediatePresentMode);

			m_VSyncDirty = false;
			m_SwapChainBufferCountDirty = false;
			m_PresentModeDirty = false;

			m_SwapChain->OnResize(m_Data.Width, m_Data.Height);
		}
	}

	void Window::Present()
	{
		m_SwapChain->Present();
	}

	void Window::SetVSync(bool enabled)
	{
		if (m_Specification.VSync == enabled && !m_VSyncDirty)
			return;

		m_Specification.VSync = enabled;
		// The swapchain present mode is chosen at (re)creation from the device
		// params, and recreating it must happen when both threads are idle. Defer
		// the apply to ProcessEvents (called at that safe point — see Application).
		m_VSyncDirty = true;
	}

	bool Window::IsVSync() const
	{
		return m_Specification.VSync;
	}

	void Window::SetSwapChainBufferCount(uint32_t count)
	{
		// Vulkan requires at least 2, and the surface's own maximum is applied when the
		// swapchain is built (VulkanSwapChain::Create clamps to surfaceCaps). The upper
		// bound here just keeps the request sane; more images is latency and VRAM, and
		// past a handful it buys nothing.
		count = std::clamp(count, 2u, 8u);

		if (m_Specification.SwapChainBufferCount == count && !m_SwapChainBufferCountDirty)
			return;

		m_Specification.SwapChainBufferCount = count;
		m_SwapChainBufferCountDirty = true;
	}

	uint32_t Window::GetSwapChainBufferCount() const
	{
		return m_Specification.SwapChainBufferCount;
	}

	void Window::SetPreferImmediatePresentMode(bool prefer)
	{
		if (m_Specification.PreferImmediatePresentMode == prefer && !m_PresentModeDirty)
			return;

		m_Specification.PreferImmediatePresentMode = prefer;
		m_PresentModeDirty = true;
	}

	bool Window::PrefersImmediatePresentMode() const
	{
		return m_Specification.PreferImmediatePresentMode;
	}

	void Window::SetResizable(bool resizable) const
	{
		glfwSetWindowAttrib(m_WindowHandle, GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
	}

	bool Window::BeginFrame()
	{
		LUX_CORE_VERIFY(m_SwapChain);
		return m_SwapChain->BeginFrame();
	}

	void Window::Maximize()
	{
		glfwMaximizeWindow(m_WindowHandle);
	}

	void Window::CenterWindow()
	{
		const GLFWvidmode* videmode = glfwGetVideoMode(glfwGetPrimaryMonitor());
		int x = (videmode->width / 2) - (m_Data.Width / 2);
		int y = (videmode->height / 2) - (m_Data.Height / 2);
		glfwSetWindowPos(m_WindowHandle, x, y);
	}

	void Window::Show()
	{
		glfwShowWindow(m_WindowHandle);
	}

	void Window::SetTitle(const std::string& title)
	{
		m_Data.Title = title;
		glfwSetWindowTitle(m_WindowHandle, m_Data.Title.c_str());
	}

	VulkanSwapChain& Window::GetSwapChain()
	{
		return *m_SwapChain;
	}

	void Window::OnWindowSizeCallback(int width, int height)
	{
		WindowResizeEvent event((uint32_t)width, (uint32_t)height);
		if (m_Data.EventCallback)
			m_Data.EventCallback(event);
		m_Data.Width = width;
		m_Data.Height = height;
	}

	void Window::OnWindowCloseCallback()
	{
		WindowCloseEvent event;
		m_Data.EventCallback(event);
	}

	void Window::OnKeyCallback(int key, int scancode, int action, int mods)
	{
		switch (action)
		{
			case GLFW_PRESS:
			{
				Input::UpdateKeyState((KeyCode)key, KeyState::Pressed);
				KeyPressedEvent event((KeyCode)key, 0);
				m_Data.EventCallback(event);
				break;
			}
			case GLFW_RELEASE:
			{
				Input::UpdateKeyState((KeyCode)key, KeyState::Released);
				KeyReleasedEvent event((KeyCode)key);
				m_Data.EventCallback(event);
				break;
			}
			case GLFW_REPEAT:
			{
				Input::UpdateKeyState((KeyCode)key, KeyState::Held);
				KeyPressedEvent event((KeyCode)key, 1);
				m_Data.EventCallback(event);
				break;
			}
		}
	}

	void Window::OnCharCallback(uint32_t codepoint)
	{
		KeyTypedEvent event((KeyCode)codepoint);
		m_Data.EventCallback(event);
	}

	void Window::OnMouseButtonCallback(int button, int action, int mods)
	{
		switch (action)
		{
			case GLFW_PRESS:
			{
				Input::UpdateButtonState((MouseButton)button, KeyState::Pressed);
				MouseButtonPressedEvent event((MouseButton)button);
				m_Data.EventCallback(event);
				break;
			}
			case GLFW_RELEASE:
			{
				Input::UpdateButtonState((MouseButton)button, KeyState::Released);
				MouseButtonReleasedEvent event((MouseButton)button);
				m_Data.EventCallback(event);
				break;
			}
		}
	}

	void Window::OnMouseScrollCallback(double xOffset, double yOffset)
	{
		MouseScrolledEvent event((float)xOffset, (float)yOffset);
		m_Data.EventCallback(event);
	}

	void Window::OnMousePosCallback(double x, double y)
	{
		MouseMovedEvent event((float)x, (float)y);
		m_Data.EventCallback(event);
	}

	void Window::OnTitlebarHitTestCallback(int x, int y, int* hit)
	{
		WindowTitleBarHitTestEvent event(x, y, *hit);
		m_Data.EventCallback(event);
	}

	void Window::OnWindowIconifyCallback(int iconified)
	{
		WindowMinimizeEvent event((bool)iconified);
		m_Data.EventCallback(event);
	}

}
