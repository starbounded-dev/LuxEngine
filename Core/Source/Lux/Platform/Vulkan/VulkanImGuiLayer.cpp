#include "lpch.h"
#include "VulkanImGuiLayer.h"

#include "imgui.h"
#include "implot/implot.h"
#include "Lux/Core/Input.h"
#include "Lux/ImGui/ImGuizmo.h"
#include "Lux/ImGui/ImGuiFonts.h"

#ifndef IMGUI_IMPL_API
#define IMGUI_IMPL_API
#endif
#include "backends/imgui_impl_glfw.h"

#include "Lux/Core/Application.h"
#include <GLFW/glfw3.h>

#include "Lux/Editor/FontAwesome.h"

#include "Lux/Renderer/Renderer.h"

#include "Lux/Platform/Vulkan/VulkanContext.h"

namespace Lux {

	static std::vector<VkCommandBuffer> s_ImGuiCommandBuffers;

	VulkanImGuiLayer::VulkanImGuiLayer()
	{
	}

	VulkanImGuiLayer::VulkanImGuiLayer(const std::string& name)
	{

	}

	VulkanImGuiLayer::~VulkanImGuiLayer()
	{
	}

	void VulkanImGuiLayer::OnAttach()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImPlot::CreateContext();
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

	}

	void VulkanImGuiLayer::OnDetach()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (ImGui::GetCurrentContext())
		{
			ImGuiIO& io = ImGui::GetIO();
			if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
				ImGui::DestroyPlatformWindows();
		}

		// Device wait is handled in Renderer::Shutdown via DeviceManager.
		ImGui_ImplGlfw_Shutdown();
		ImPlot::DestroyContext();
		ImGui::DestroyContext();
	}

	void VulkanImGuiLayer::Begin()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		ImGui::SetMouseCursor(Input::GetCursorMode() == CursorMode::Normal ? ImGuiMouseCursor_Arrow : ImGuiMouseCursor_None);

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}

	void VulkanImGuiLayer::End()
	{
		LUX_PROFILE_FUNCTION_AUTO;
	}

	void VulkanImGuiLayer::OnImGuiRender()
	{
		LUX_PROFILE_FUNCTION_AUTO;
	}

}
