#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Core/Events/Event.h"

#include "Lux/Renderer/DeviceManager.h"
#include "Lux/Renderer/RendererContext.h"

#include <functional>
#include <filesystem>

#include <vulkan/vulkan.h> // NOTE(Emily): This ensures that the first inclusion of GLFW defines
						   //			   Vulkan exclusive procs before include guards trip.

#include <GLFW/glfw3.h>

namespace Lux {

	struct WindowSpecification
	{
		std::string Title = "Lux";
		uint32_t Width = 1600;
		uint32_t Height = 900;
		bool Decorated = true;
		bool Fullscreen = false;
		bool VSync = true;
		// Swapchain image count. Clamped to what the surface reports it supports.
		uint32_t SwapChainBufferCount = 3;
		// Only consulted when VSync is off: IMMEDIATE instead of MAILBOX.
		bool PreferImmediatePresentMode = false;
		std::filesystem::path IconPath;
	};

	class VulkanSwapChain;

	class Window
	{
	public:
		using EventCallbackFn = std::function<void(Event&)>;

		Window(const WindowSpecification& specification);
		virtual ~Window();

		virtual void Init();
		virtual void ProcessEvents();
		virtual void Present();

		inline uint32_t GetWidth() const { return m_Data.Width; }
		inline uint32_t GetHeight() const { return m_Data.Height; }

		virtual std::pair<uint32_t, uint32_t> GetSize() const { return { m_Data.Width, m_Data.Height }; }
		virtual std::pair<float, float> GetWindowPos() const;

		// Window attributes
		virtual void SetEventCallback(const EventCallbackFn& callback) { m_Data.EventCallback = callback; }
		virtual void SetVSync(bool enabled);
		virtual bool IsVSync() const;
		// Applied on the next swapchain recreate, deferred to ProcessEvents like VSync.
		// Under MAILBOX this is what sets the frame-rate ceiling; see DeviceManager.
		virtual void SetSwapChainBufferCount(uint32_t count);
		virtual uint32_t GetSwapChainBufferCount() const;
		// IMMEDIATE vs MAILBOX when VSync is off. Same deferred-recreate path.
		virtual void SetPreferImmediatePresentMode(bool prefer);
		virtual bool PrefersImmediatePresentMode() const;
		virtual void SetResizable(bool resizable) const;

		bool BeginFrame();

		virtual void Maximize();
		virtual void CenterWindow();
		virtual void Show();

		virtual const std::string& GetTitle() const { return m_Data.Title; }
		virtual void SetTitle(const std::string& title);

		inline GLFWwindow* GetNativeWindow() const { return m_WindowHandle; }

		virtual Ref<RendererContext> GetRenderContext() { return m_RendererContext; }
		virtual VulkanSwapChain& GetSwapChain();
		DeviceManager* GetDeviceManager() { return m_DeviceManager; }

		// High DPI scale factor
		inline static float s_HighDPIScaleFactor;

		void OnWindowSizeCallback(int width, int height);
		void OnWindowCloseCallback();
		void OnKeyCallback(int key, int scancode, int action, int mods);
		void OnCharCallback(uint32_t codepoint);
		void OnMouseButtonCallback(int button, int action, int mods);
		void OnMouseScrollCallback(double xOffset, double yOffset);
		void OnMousePosCallback(double x, double y);
		void OnTitlebarHitTestCallback(int x, int y, int* hit);
		void OnWindowIconifyCallback(int iconified);
	public:
		static Window* Create(const WindowSpecification& specification = WindowSpecification());
	private:
		bool CreateWindowSurface();
		virtual void Shutdown();
	private:
		DeviceManager* m_DeviceManager = nullptr;
		GLFWwindow* m_WindowHandle = nullptr;
		GLFWcursor* m_ImGuiMouseCursors[9] = { 0 };
		WindowSpecification m_Specification;
		struct WindowData
		{
			std::string Title;
			uint32_t Width, Height;
			bool SizeDirty = false;

			EventCallbackFn EventCallback;
			Window* Self = nullptr;
		};

		WindowData m_Data;
		float m_LastFrameTime = 0.0f;

		Ref<RendererContext> m_RendererContext;
		VulkanSwapChain* m_SwapChain;
		bool m_VSyncDirty = false; // pending VSync change, applied in ProcessEvents
		bool m_SwapChainBufferCountDirty = false; // pending image-count change, same path
		bool m_PresentModeDirty = false;          // pending Immediate/Mailbox change, same path

		VkSurfaceKHR m_WindowSurface;

		friend class DeviceManager;
	};

}
