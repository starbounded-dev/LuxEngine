// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "StartupSplash.h"

#include "Lux/Core/Application.h"
#include "Lux/Core/Version.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiFonts.h"
#include "Lux/ImGui/ImGuiUtilities.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Lux {

	namespace {

		// Board 06 of the Lux brand sheet, in unscaled pixels; everything follows the UI scale
		// (ImGuiEx::Fonts::GetScale) the way the editor's own cards do.
		constexpr float k_Width = 960.0f;
		constexpr float k_Height = 540.0f;
		constexpr float k_Margin = 48.0f;
		constexpr float k_DotGridLeft = 512.0f;   // the dot grid covers the right 448 px only
		constexpr float k_DotPitch = 16.0f;
		constexpr float k_DotSize = 1.5f;
		constexpr float k_MarkX = 616.0f;
		constexpr float k_MarkY = 160.0f;
		constexpr float k_MarkSize = 216.0f;      // 72 px cells
		constexpr float k_MarkClearSpace = 16.0f; // keeps the dots off the mark
		constexpr float k_WordmarkSize = 72.0f;
		constexpr float k_CodeSize = 13.0f;
		constexpr float k_LabelSize = 11.0f;
		constexpr float k_VersionGap = 8.0f;      // wordmark -> version line
		constexpr float k_BarGap = 24.0f;         // version line -> progress bar
		constexpr float k_StatusGap = 8.0f;       // progress bar -> status
		constexpr float k_BarWidth = 400.0f;
		constexpr float k_BarHeight = 2.0f;

		constexpr const char* k_VersionLine = LUX_VERSION " \xC2\xB7 " LUX_BUILD_CONFIG_NAME " \xC2\xB7 Vulkan";

		// Run() presents a frame while it builds the next, so after this many hand-off frames the
		// owner's first two frames are in the main window's swapchain. The first is not enough: the
		// dockspace lays itself out during it.
		constexpr uint32_t k_HandOffFrames = 3;

		// Finish() draws at most this many frames (about half a second) waiting for the maximize.
		constexpr uint32_t k_MaximizeWaitFrames = 30;

	}

	StartupSplash::StartupSplash()
	{
		LUX_CORE_VERIFY(ImGui::GetCurrentContext(), "The startup splash needs ImGui (ApplicationSpecification::EnableImGui)");
		LUX_CORE_ASSERT(!s_Instance, "Only one startup splash at a time");
		s_Instance = this;
		m_OwnWindow = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;

		// Without platform viewports the splash can only draw inside the main window, so that window
		// is the card until Finish(). Otherwise (own window, or a fullscreen main window, which keeps
		// its mode) the main window is placed now, while it is still hidden.
		Application& app = Application::Get();
		Window& window = app.GetWindow();
		m_WindowIsCard = !m_OwnWindow && !app.GetSpecification().Fullscreen;
		if (m_WindowIsCard)
		{
			const float scale = ImGuiEx::Fonts::GetScale();
			window.SetSize(static_cast<uint32_t>(std::round(k_Width * scale)), static_cast<uint32_t>(std::round(k_Height * scale)));
			window.CenterWindow();
		}
		else
		{
			app.PlaceWindow();
		}

		if (!m_OwnWindow)
			window.Show();
	}

	StartupSplash::~StartupSplash()
	{
		s_Instance = nullptr;
		Application::Get().GetWindow().Show();
	}

	void StartupSplash::Report(float progress, std::string status)
	{
		// Hand-off frames are Run()'s own; a startup frame there would nest inside one.
		if (!s_Instance || s_Instance->m_Finished)
			return;

		LUX_CORE_ASSERT(Application::IsMainThread(), "The startup splash is drawn with ImGui, on the main thread");
		s_Instance->m_Progress = std::clamp(progress, 0.0f, 1.0f);
		s_Instance->m_Status = std::move(status);
		Application::Get().RenderStartupFrame([]() { s_Instance->Draw(); });
	}

	void StartupSplash::Finish()
	{
		LUX_CORE_ASSERT(Application::IsMainThread(), "The startup splash is drawn with ImGui, on the main thread");
		LUX_CORE_ASSERT(!m_Finished, "StartupSplash::Finish called twice");
		m_Finished = true;
		if (!m_WindowIsCard)
			return;

		Application& app = Application::Get();
		Window& window = app.GetWindow();
		const ApplicationSpecification& specification = app.GetSpecification();

		// The application's size first, even when maximizing: the window manager restores a
		// maximized window to the size it had before, which would otherwise be the card's.
		window.SetSize(specification.WindowWidth, specification.WindowHeight);
		app.PlaceWindow();
		m_WindowIsCard = false;
		if (!specification.StartMaximized)
			return;

		// A frame laid out at a smaller size would pull floating ImGui windows into it, and ImGui
		// saves where they end up, so the owner's first frame has to wait for the maximize.
		GLFWwindow* nativeWindow = window.GetNativeWindow();
		for (uint32_t frame = 0; frame < k_MaximizeWaitFrames; frame++)
		{
			if (glfwGetWindowAttrib(nativeWindow, GLFW_MAXIMIZED))
				return;
			app.RenderStartupFrame([this]() { Draw(); });
		}

		if (!glfwGetWindowAttrib(nativeWindow, GLFW_MAXIMIZED))
			LUX_CORE_WARN_TAG("StartupSplash", "The window manager has not maximized the main window after {} frames; continuing at its current size", k_MaximizeWaitFrames);
	}

	bool StartupSplash::DrawUntilPresented()
	{
		LUX_CORE_ASSERT(m_Finished, "Call StartupSplash::Finish at the end of OnAttach, before the hand-off frames");
		if (m_HandOffFrames >= k_HandOffFrames)
			return false;

		m_HandOffFrames++;
		Draw();
		return true;
	}

	void StartupSplash::Draw() const
	{
		const float scale = ImGuiEx::Fonts::GetScale();
		const auto S = [scale](float value) { return std::round(value * scale); };

		const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
		const ImVec2 size(S(k_Width), S(k_Height));
		ImGui::SetNextWindowPos(mainViewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(size, ImGuiCond_Always);
		if (m_OwnWindow)
		{
			// NoAutoMerge keeps it a platform window of its own although it sits over the main one.
			ImGuiWindowClass windowClass;
			windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoAutoMerge;
			ImGui::SetNextWindowClass(&windowClass);
		}

		constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;

		ImGuiEx::ScopedStyleStack style(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f), ImGuiStyleVar_WindowBorderSize, 0.0f, ImGuiStyleVar_WindowRounding, 0.0f);
		ImGuiEx::ScopedColour background(ImGuiCol_WindowBg, Colors::Brand::ground);
		if (ImGui::Begin("##lux_startup_splash", nullptr, flags))
		{
			// Over the owner's windows during the hand-off when it shares their viewport (Linux).
			ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec2 origin = ImGui::GetWindowPos();
			const ImVec2 max(origin.x + size.x, origin.y + size.y);

			// Drawn in the main window, the rest of that window is the splash's ground too.
			if (!m_OwnWindow)
			{
				drawList->PushClipRectFullScreen();
				drawList->AddRectFilled(mainViewport->Pos, ImVec2(mainViewport->Pos.x + mainViewport->Size.x, mainViewport->Pos.y + mainViewport->Size.y), Colors::Brand::ground);
				drawList->PopClipRect();
			}

			ImGuiEx::Draw::DotGrid(drawList, ImVec2(origin.x + S(k_DotGridLeft), origin.y), max, S(k_DotPitch), std::max(1.0f, k_DotSize * scale), Colors::Brand::inkMuted);

			const ImVec2 markMin(origin.x + S(k_MarkX), origin.y + S(k_MarkY));
			const float markSize = S(k_MarkSize);
			const float clearSpace = S(k_MarkClearSpace);
			drawList->AddRectFilled(ImVec2(markMin.x - clearSpace, markMin.y - clearSpace), ImVec2(markMin.x + markSize + clearSpace, markMin.y + markSize + clearSpace), Colors::Brand::ground);
			ImGuiEx::Draw::LuxMark(drawList, markMin, markSize, Colors::Brand::ink, Colors::Brand::lux);

			ImFont* monoFont = ImGuiEx::Fonts::Get("Mono");
			ImFont* displayFont = ImGuiEx::Fonts::Get("Display");
			const float left = origin.x + S(k_Margin);
			drawList->AddText(monoFont, S(k_LabelSize), ImVec2(left, origin.y + S(k_Margin)), Colors::Brand::inkMuted, "LUX STUDIO");

			// The lower-left block, laid out upwards from the bottom margin.
			const float codeSize = S(k_CodeSize);
			const float codeHeight = monoFont->CalcTextSizeA(codeSize, FLT_MAX, 0.0f, k_VersionLine).y;
			const float statusY = max.y - S(k_Margin) - codeHeight;
			const float barY = statusY - S(k_StatusGap) - S(k_BarHeight);
			const float versionY = barY - S(k_BarGap) - codeHeight;
			const float wordmarkHeight = displayFont->CalcTextSizeA(S(k_WordmarkSize), FLT_MAX, 0.0f, "LuxEngine").y;
			const float wordmarkY = versionY - S(k_VersionGap) - wordmarkHeight;

			drawList->AddText(displayFont, S(k_WordmarkSize), ImVec2(left, wordmarkY), Colors::Brand::ink, "LuxEngine");
			drawList->AddText(monoFont, codeSize, ImVec2(left, versionY), Colors::Brand::inkMuted, k_VersionLine);
			drawList->AddRectFilled(ImVec2(left, barY), ImVec2(left + S(k_BarWidth), barY + S(k_BarHeight)), Colors::Brand::line);
			drawList->AddRectFilled(ImVec2(left, barY), ImVec2(left + S(k_BarWidth) * m_Progress, barY + S(k_BarHeight)), Colors::Brand::lux);
			// Clipped to the bar so a long project name cannot run into the dot grid.
			const ImVec4 statusClip(left, statusY, left + S(k_BarWidth), statusY + codeHeight);
			drawList->AddText(monoFont, codeSize, ImVec2(left, statusY), Colors::Brand::inkMuted, m_Status.c_str(), nullptr, 0.0f, &statusClip);

			drawList->AddRect(origin, max, Colors::Brand::line);
		}
		ImGui::End();
	}

}
