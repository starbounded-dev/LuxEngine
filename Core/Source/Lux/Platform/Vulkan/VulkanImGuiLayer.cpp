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
			// Kept in lockstep with ImGuiLayer::OnAttach (the layer actually instantiated). Archivo
			// for UI text; add-order is significant (Fonts[0]="Bold", Fonts[1]="Large") and the name
			// keys must not change.
			ImGuiEx::FontConfiguration archivoBold;
			archivoBold.FontName = "Bold";
			archivoBold.FilePath = "Resources/Fonts/Archivo/static/Archivo-Bold.ttf";
			archivoBold.Size = 18.0f;
			ImGuiEx::Fonts::Add(archivoBold);

			ImGuiEx::FontConfiguration archivoLarge;
			archivoLarge.FontName = "Large";
			archivoLarge.FilePath = "Resources/Fonts/Archivo/static/Archivo-Regular.ttf";
			archivoLarge.Size = 24.0f;
			ImGuiEx::Fonts::Add(archivoLarge);

			ImGuiEx::FontConfiguration archivoDefault;
			archivoDefault.FontName = "Default";
			archivoDefault.FilePath = "Resources/Fonts/Archivo/static/Archivo-Medium.ttf";
			archivoDefault.Size = 15.0f;
			ImGuiEx::Fonts::Add(archivoDefault, true);

			static const ImWchar s_FontAwesomeRanges[] = { LUX_ICON_MIN, LUX_ICON_MAX, 0 };
			ImGuiEx::FontConfiguration fontAwesome;
			fontAwesome.FontName = "FontAwesome";
			fontAwesome.FilePath = "Resources/Fonts/FontAwesome/fontawesome-webfont.ttf";
			fontAwesome.Size = 16.0f;
			fontAwesome.GlyphRanges = s_FontAwesomeRanges;
			fontAwesome.MergeWithLast = true;
			ImGuiEx::Fonts::Add(fontAwesome);

			ImGuiEx::FontConfiguration archivoMedium;
			archivoMedium.FontName = "Medium";
			archivoMedium.FilePath = "Resources/Fonts/Archivo/static/Archivo-Medium.ttf";
			archivoMedium.Size = 18.0f;
			ImGuiEx::Fonts::Add(archivoMedium);

			ImGuiEx::FontConfiguration archivoSmall;
			archivoSmall.FontName = "Small";
			archivoSmall.FilePath = "Resources/Fonts/Archivo/static/Archivo-Regular.ttf";
			archivoSmall.Size = 12.0f;
			ImGuiEx::Fonts::Add(archivoSmall);

			ImGuiEx::FontConfiguration archivoExtraSmall;
			archivoExtraSmall.FontName = "ExtraSmall";
			archivoExtraSmall.FilePath = "Resources/Fonts/Archivo/static/Archivo-Regular.ttf";
			archivoExtraSmall.Size = 10.0f;
			ImGuiEx::Fonts::Add(archivoExtraSmall);

			ImGuiEx::FontConfiguration archivoBoldTitle;
			archivoBoldTitle.FontName = "BoldTitle";
			archivoBoldTitle.FilePath = "Resources/Fonts/Archivo/static/Archivo-SemiBold.ttf";
			archivoBoldTitle.Size = 16.0f;
			ImGuiEx::Fonts::Add(archivoBoldTitle);

			ImGuiEx::FontConfiguration jetBrainsMono;
			jetBrainsMono.FontName = "Mono";
			jetBrainsMono.FilePath = "Resources/Fonts/JetBrainsMono/JetBrainsMono-Bold.ttf";
			jetBrainsMono.Size = 13.0f;
			ImGuiEx::Fonts::Add(jetBrainsMono);

			ImGuiEx::FontConfiguration bricolageDisplay;
			bricolageDisplay.FontName = "Display";
			bricolageDisplay.FilePath = "Resources/Fonts/Bricolage_Grotesque/static/BricolageGrotesque-Bold.ttf";
			bricolageDisplay.Size = 18.0f;
			ImGuiEx::Fonts::Add(bricolageDisplay);
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
