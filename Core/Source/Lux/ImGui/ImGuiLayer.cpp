#include "lpch.h"
#include "ImGuiLayer.h"

#include "Colors.h"

#include "Lux/Core/Input.h"

#include "Lux/Renderer/Renderer.h"

#include "Lux/Platform/Vulkan/VulkanImGuiLayer.h"
#include "Lux/Platform/Vulkan/VulkanSwapChain.h"
#include "Lux/Platform/Vulkan/VulkanDeviceManager.h"

#include "Lux/Renderer/RendererAPI.h"

#include "Lux/Editor/FontAwesome.h"

#include "Lux/ImGui/ImGuizmo.h"
#include "Lux/ImGui/ImGuiFonts.h"

#include <imgui.h>

#include "backends/imgui_impl_glfw.h"

// TODO(Yan): WIP
// Defined in imgui_impl_glfw.cpp
// extern bool g_DisableImGuiEvents;

namespace Lux {

	void ImGuiLayer::OnAttach()
	{
		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
		//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows

		// Configure Fonts
		{
			ImGuiEx::FontConfiguration robotoBold;
			robotoBold.FontName = "Bold";
			robotoBold.FilePath = "Resources/Fonts/Roboto/Roboto-Bold.ttf";
			robotoBold.Size = 18.0f;
			ImGuiEx::Fonts::Add(robotoBold);

			ImGuiEx::FontConfiguration robotoLarge;
			robotoLarge.FontName = "Large";
			robotoLarge.FilePath = "Resources/Fonts/Roboto/Roboto-Regular.ttf";
			robotoLarge.Size = 24.0f;
			ImGuiEx::Fonts::Add(robotoLarge);

			ImGuiEx::FontConfiguration robotoDefault;
			robotoDefault.FontName = "Default";
			robotoDefault.FilePath = "Resources/Fonts/Roboto/Roboto-SemiMedium.ttf";
			robotoDefault.Size = 15.0f;
			ImGuiEx::Fonts::Add(robotoDefault, true);

			static const ImWchar s_FontAwesomeRanges[] = { LUX_ICON_MIN, LUX_ICON_MAX, 0 };
			ImGuiEx::FontConfiguration fontAwesome;
			fontAwesome.FontName = "FontAwesome";
			fontAwesome.FilePath = "Resources/Fonts/FontAwesome/fontawesome-webfont.ttf";
			fontAwesome.Size = 16.0f;
			fontAwesome.GlyphRanges = s_FontAwesomeRanges;
			fontAwesome.MergeWithLast = true;
			ImGuiEx::Fonts::Add(fontAwesome);

			ImGuiEx::FontConfiguration robotoMedium;
			robotoMedium.FontName = "Medium";
			robotoMedium.FilePath = "Resources/Fonts/Roboto/Roboto-SemiMedium.ttf";
			robotoMedium.Size = 18.0f;
			ImGuiEx::Fonts::Add(robotoMedium);

			ImGuiEx::FontConfiguration robotoSmall;
			robotoSmall.FontName = "Small";
			robotoSmall.FilePath = "Resources/Fonts/Roboto/Roboto-SemiMedium.ttf";
			robotoSmall.Size = 12.0f;
			ImGuiEx::Fonts::Add(robotoSmall);

			ImGuiEx::FontConfiguration robotoExtraSmall;
			robotoExtraSmall.FontName = "ExtraSmall";
			robotoExtraSmall.FilePath = "Resources/Fonts/Roboto/Roboto-SemiMedium.ttf";
			robotoExtraSmall.Size = 10.0f;
			ImGuiEx::Fonts::Add(robotoExtraSmall);

			ImGuiEx::FontConfiguration robotoBoldTitle;
			robotoBoldTitle.FontName = "BoldTitle";
			robotoBoldTitle.FilePath = "Resources/Fonts/Roboto/Roboto-Bold.ttf";
			robotoBoldTitle.Size = 16.0f;
			ImGuiEx::Fonts::Add(robotoBoldTitle);
		}

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();
		SetDarkThemeV2Colors();

		// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.15f, style.Colors[ImGuiCol_WindowBg].w);

		ImGui_ImplGlfw_InitForVulkan((GLFWwindow*)Application::Get().GetWindow().GetNativeWindow(), true);

		m_ImGuiRenderer = std::make_unique<ImGuiRenderer>();
		m_ImGuiRenderer->Init();

		InitPlatformInterface();
	}

	struct ImGuiViewportData
	{
		bool WindowOwned = false;
		std::unique_ptr<VulkanSwapChain> SC;
		std::unique_ptr<ImGuiRenderer> Renderer;
	};

	static void ImGuiRenderer_CreateWindow(ImGuiViewport* viewport)
	{
		ImGuiViewportData* data = IM_NEW(ImGuiViewportData)();
		viewport->RendererUserData = data;

		vk::Instance vInstance = ((VulkanDeviceManager*)Application::GetGraphicsDeviceManager())->GetVulkanInstance();

		// Create surface
		ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
		vk::SurfaceKHR surface;
		VkResult err = (VkResult)platform_io.Platform_CreateVkSurface(viewport, *(ImU64*)&vInstance, nullptr, (ImU64*)&surface);
		LUX_CORE_ASSERT(err == VkResult::VK_SUCCESS);

		data->SC = std::make_unique<VulkanSwapChain>(surface);
		data->SC->Create((uint32_t)viewport->Size.x, (uint32_t)viewport->Size.y);
		data->WindowOwned = true;

		data->Renderer = std::make_unique<ImGuiRenderer>();
		ImGuiLayer* imguiLayer = Application::Get().GetImGuiLayer();
		ImGuiRenderer* mainRenderer = imguiLayer ? imguiLayer->GetImGuiRenderer() : nullptr;
		data->Renderer->Init(mainRenderer ? mainRenderer->GetRegistry() : nullptr);
	}

	static void ImGuiRenderer_DestroyWindow(ImGuiViewport* viewport)
	{
		ImGuiViewportData* vd = (ImGuiViewportData*)viewport->RendererUserData;
		ldelete vd;
		viewport->RendererUserData = nullptr;
	}

	static void ImGuiRenderer_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
	{
		ImGuiViewportData* vd = (ImGuiViewportData*)viewport->RendererUserData;
		vd->SC->OnResize((uint32_t)size.x, (uint32_t)size.y);
	}

	static void ImGuiRenderer_RenderWindow(ImGuiViewport* viewport, void*)
	{
		ImGuiViewportData* vd = (ImGuiViewportData*)viewport->RendererUserData;
		ImGuiLayer* imguiLayer = Application::Get().GetImGuiLayer();
		ImGuiRenderer* mainRenderer = imguiLayer ? imguiLayer->GetImGuiRenderer() : nullptr;
		auto snapshot = ImGuiDrawDataSnapshot::Create(
			viewport->DrawData, mainRenderer ? mainRenderer->GetRegistry() : nullptr);
		vd->SC->BeginFrame();
		vd->Renderer->RenderToSwapchain(snapshot, vd->SC.get());
	}

	static void ImGuiRenderer_SwapBuffers(ImGuiViewport* viewport, void*)
	{
		ImGuiViewportData* vd = (ImGuiViewportData*)viewport->RendererUserData;
		vd->SC->Present();
	}

	void ImGuiLayer::InitPlatformInterface()
	{
		ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			IM_ASSERT(platform_io.Platform_CreateVkSurface != NULL && "Platform needs to setup the CreateVkSurface handler.");

		platform_io.Renderer_CreateWindow = ImGuiRenderer_CreateWindow;
		platform_io.Renderer_DestroyWindow = ImGuiRenderer_DestroyWindow;
		platform_io.Renderer_SetWindowSize = ImGuiRenderer_SetWindowSize;
		platform_io.Renderer_RenderWindow = ImGuiRenderer_RenderWindow;
		platform_io.Renderer_SwapBuffers = ImGuiRenderer_SwapBuffers;
	}

	void ImGuiLayer::OnDetach()
	{
		if (ImGui::GetCurrentContext())
		{
			ImGuiIO& io = ImGui::GetIO();
			if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
				ImGui::DestroyPlatformWindows();
		}

		m_ImGuiRenderer.reset();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void ImGuiLayer::Begin()
	{
		LUX_CORE_ASSERT(Application::GetMainThreadID() == std::this_thread::get_id(),
			"ImGui frame construction must run on the window-owning main thread");

		ImGui::SetMouseCursor(Input::GetCursorMode() == CursorMode::Normal ? ImGuiMouseCursor_Arrow : ImGuiMouseCursor_None);

		if (auto registry = m_ImGuiRenderer->GetRegistry())
			registry->NewFrame();

		m_ImGuiRenderer->UpdateFontTexture();
		ImGui_ImplGlfw_NewFrame();

		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}

	void ImGuiLayer::End()
	{
		LUX_CORE_ASSERT(Application::GetMainThreadID() == std::this_thread::get_id(),
			"ImGui frame finalization must run on the window-owning main thread");

		ImGui::Render();

		// Platform window creation, sizing, and style changes are GLFW/Win32 operations
		// and must stay on the main thread. GPU rendering is captured and deferred.
		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			ImGui::UpdatePlatformWindows();

		m_PendingRenderTasks.clear();
		const auto registry = m_ImGuiRenderer->GetRegistry();

		if (auto snapshot = ImGuiDrawDataSnapshot::Create(ImGui::GetDrawData(), registry))
		{
			ImGuiRenderer* renderer = m_ImGuiRenderer.get();
			VulkanSwapChain* swapchain = &Application::Get().GetWindow().GetSwapChain();
			m_PendingRenderTasks.emplace_back([renderer, swapchain, snapshot]()
			{
				renderer->RenderToSwapchain(snapshot, swapchain);
			});
		}

		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
			for (int32_t i = 1; i < platformIO.Viewports.Size; i++)
			{
				ImGuiViewport* viewport = platformIO.Viewports[i];
				auto* viewportData = static_cast<ImGuiViewportData*>(viewport->RendererUserData);
				auto snapshot = ImGuiDrawDataSnapshot::Create(viewport->DrawData, registry);
				if (!viewportData || !viewportData->SC || !viewportData->Renderer || !snapshot)
					continue;

				m_PendingRenderTasks.emplace_back([viewportData, snapshot]()
				{
					if (viewportData->SC->BeginFrame())
					{
						viewportData->Renderer->RenderToSwapchain(snapshot, viewportData->SC.get());
						viewportData->SC->Present();
					}
				});
			}
		}
	}

	void ImGuiLayer::SubmitDrawData()
	{
		for (std::function<void()>& renderTask : m_PendingRenderTasks)
			Renderer::Submit(std::move(renderTask));
		m_PendingRenderTasks.clear();
	}

	void ImGuiLayer::SetDarkThemeColors()
	{
		auto& colors = ImGui::GetStyle().Colors;
		colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };

		// Headers
		colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
		colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
		colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		// Buttons
		colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
		colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
		colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		// Frame BG
		colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
		colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
		colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		// Tabs
		colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
		colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
		colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };

		// Title
		colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		// Resize Grip
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.91f, 0.91f, 0.91f, 0.25f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.81f, 0.81f, 0.81f, 0.67f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.46f, 0.46f, 0.46f, 0.95f);

		// Scrollbar
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
		colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.0f);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.0f);
		colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.0f);

		// Check Mark
		colors[ImGuiCol_CheckMark] = ImVec4(0.94f, 0.94f, 0.94f, 1.0f);

		// Slider
		colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.51f, 0.51f, 0.7f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.66f, 0.66f, 0.66f, 1.0f);

	}

	void ImGuiLayer::SetDarkThemeV2Colors()
	{
		auto& style = ImGui::GetStyle();
		auto& colors = ImGui::GetStyle().Colors;

		//========================================================
		/// Colours

		// Headers
		colors[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::groupHeader);
		colors[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::groupHeader);
		colors[ImGuiCol_HeaderActive] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::groupHeader);

		// Buttons
		colors[ImGuiCol_Button] = ImColor(56, 56, 56, 200);
		colors[ImGuiCol_ButtonHovered] = ImColor(70, 70, 70, 255);
		colors[ImGuiCol_ButtonActive] = ImColor(56, 56, 56, 150);

		// Frame BG
		colors[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::propertyField);
		colors[ImGuiCol_FrameBgHovered] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::propertyField);
		colors[ImGuiCol_FrameBgActive] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::propertyField);

		// Tabs
		colors[ImGuiCol_Tab] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::titlebar);
		colors[ImGuiCol_TabHovered] = ImColor(255, 225, 135, 30);
		colors[ImGuiCol_TabActive] = ImColor(255, 225, 135, 60);
		colors[ImGuiCol_TabUnfocused] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::titlebar);
		colors[ImGuiCol_TabUnfocusedActive] = colors[ImGuiCol_TabHovered];

		// Title
		colors[ImGuiCol_TitleBg] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::titlebar);
		colors[ImGuiCol_TitleBgActive] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::titlebar);
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

		// Resize Grip
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.91f, 0.91f, 0.91f, 0.25f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.81f, 0.81f, 0.81f, 0.67f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.46f, 0.46f, 0.46f, 0.95f);

		// Scrollbar
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
		colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.0f);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.0f);
		colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.0f);

		// Check Mark
		colors[ImGuiCol_CheckMark] = ImColor(200, 200, 200, 255);

		// Slider
		colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.51f, 0.51f, 0.7f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.66f, 0.66f, 0.66f, 1.0f);

		// Text
		colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::text);

		// Checkbox
		colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::text);

		// Separator
		colors[ImGuiCol_Separator] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::backgroundDark);
		colors[ImGuiCol_SeparatorActive] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::highlight);
		colors[ImGuiCol_SeparatorHovered] = ImColor(39, 185, 242, 150);

		// Window Background
		colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::titlebar);
		colors[ImGuiCol_ChildBg] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::background);
		colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::backgroundPopup);
		colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::backgroundDark);

		// Tables
		colors[ImGuiCol_TableHeaderBg] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::groupHeader);
		colors[ImGuiCol_TableBorderLight] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::backgroundDark);

		// Menubar
		colors[ImGuiCol_MenuBarBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };

		//========================================================
		/// Style
		style.FrameRounding = 2.5f;
		style.FrameBorderSize = 1.0f;
		style.IndentSpacing = 11.0f;
	}

	void ImGuiLayer::AllowInputEvents(bool allowEvents)
	{
		// TODO
		// g_DisableImGuiEvents = !allowEvents;
	}

	Lux::ImGuiRenderer* ImGuiLayer::GetImGuiRenderer()
	{
		return m_ImGuiRenderer.get();
	}
}
