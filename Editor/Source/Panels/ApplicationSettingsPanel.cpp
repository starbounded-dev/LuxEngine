// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ApplicationSettingsPanel.h"

#include "ContentBrowserPanel.h"

#include "Lux/Core/Application.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Social/DiscordSocial.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <format>
#include <iterator>

namespace Lux {

	namespace
	{
		constexpr int s_MaxRecentProjects = 10;

		// Frame-rate limit presets. 0 means unpaced; the trailing Custom entry has no fixed
		// value and instead reveals a slider, which is what the sentinel marks.
		constexpr int kUnlimitedFrameRate = 0;
		constexpr int kCustomFrameRateSentinel = -1;
		constexpr int kMinCustomFrameRate = 30;
		constexpr int kMaxCustomFrameRate = 1000;

		// Not constexpr: PropertyDropdown takes a mutable const char**.
		const char* s_FrameRateLimitOptions[] = { "Unlimited", "60", "120", "165", "240", "360", "720", "Custom" };
		constexpr int s_FrameRateLimitValues[] = { kUnlimitedFrameRate, 60, 120, 165, 240, 360, 720, kCustomFrameRateSentinel };
		static_assert(std::size(s_FrameRateLimitOptions) == std::size(s_FrameRateLimitValues),
			"Frame-rate limit labels and values must stay in step.");

		// Index 0 == Mailbox, 1 == Immediate; matches PreferImmediatePresentMode.
		const char* s_PresentModeOptions[] = { "Mailbox (no tearing)", "Immediate (uncapped, may tear)" };

		// Swapchain image counts offered in the UI. 2 is the Vulkan minimum; beyond 5 the
		// added latency and VRAM outweigh any throughput gain.
		constexpr int kMinSwapChainBufferCount = 2;
		const char* s_SwapChainBufferCountOptions[] = { "2 (double)", "3 (triple)", "4", "5" };
		constexpr int kSwapChainBufferCountOptionCount = (int)std::size(s_SwapChainBufferCountOptions);

		int32_t FindFrameRatePresetIndex(int targetFrameRate)
		{
			for (int32_t i = 0; i < (int32_t)std::size(s_FrameRateLimitValues); i++)
			{
				if (s_FrameRateLimitValues[i] == targetFrameRate)
					return i;
			}

			// Not one of the presets, so it came from the slider. Custom is the last entry.
			return (int32_t)std::size(s_FrameRateLimitValues) - 1;
		}
	}

	ApplicationSettingsPanel::ApplicationSettingsPanel(const Ref<ContentBrowserPanel>& contentBrowserPanel, EditorPreferencesBindings bindings, const Ref<UserPreferences>& userPreferences)
		: m_ContentBrowserPanel(contentBrowserPanel), m_UserPreferences(userPreferences), m_Bindings(std::move(bindings))
	{
		m_Pages.push_back({ "Editor", [this]() { DrawEditorPage(); } });
		m_Pages.push_back({ "Viewport", [this]() { DrawViewportPage(); } });
		m_Pages.push_back({ "Content Browser", [this]() { DrawContentBrowserPage(); } });
		m_Pages.push_back({ "Discord", [this]() { DrawDiscordPage(); } });

		m_DiscordApplicationID = Application::Get().GetSettings().Get("Discord.ApplicationID", "");
	}

	void ApplicationSettingsPanel::OnImGuiRender(bool& isOpen)
	{
		m_IsFocused = false;
		if (!isOpen)
			return;

		if (ImGui::Begin("Application Settings", &isOpen))
		{
			m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			const float font = ImGui::GetFontSize();
			const auto split = ImGuiEx::PanelSplitWidths(ImGui::GetContentRegionAvail().x, m_PageFraction,
														 font * 12.0f, font * 24.0f);
			if (split.SideBySide)
			{
				if (ImGui::BeginChild("##application_settings_pages", {split.First, 0.0f}))
					DrawPageList();
				ImGui::EndChild();
				ImGui::SameLine();
				ImGuiEx::PanelSplitHandle("##settings_split", split, m_PageFraction);
				ImGui::SameLine();
			}
			else
			{
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::BeginCombo("##settings_page", m_Pages[m_CurrentPage].Name))
				{
					DrawPageList();
					ImGui::EndCombo();
				}
			}
			if (ImGui::BeginChild("##application_settings_content"))
				m_Pages[m_CurrentPage].RenderFunction();
			ImGui::EndChild();
		}

		ImGui::End();
	}

	void ApplicationSettingsPanel::OnEvent(Event& event)
	{
		if (!m_IsFocused)
			return;
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>(
			[](KeyPressedEvent&) { return ImGui::GetIO().WantTextInput || GImGui->OpenPopupStack.Size > 0; });
	}

	void ApplicationSettingsPanel::DrawPageList()
	{
		for (uint32_t i = 0; i < m_Pages.size(); i++)
		{
			const bool selected = (m_CurrentPage == i);
			if (ImGui::Selectable(m_Pages[i].Name, selected, ImGuiSelectableFlags_SpanAllColumns))
				m_CurrentPage = i;
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
	}

	void ApplicationSettingsPanel::SaveState(Yaml::Writer& out) const
	{
		out << Yaml::Key << "Page" << Yaml::Value << m_Pages[m_CurrentPage].Name;
		out << Yaml::Key << "PageFraction" << Yaml::Value << m_PageFraction;
	}

	void ApplicationSettingsPanel::LoadState(const Yaml::Node& state)
	{
		const std::string page = state["Page"].as<std::string>("");
		for (uint32_t index = 0; index < m_Pages.size(); ++index)
			if (page == m_Pages[index].Name)
				m_CurrentPage = index;
		const float fraction = state["PageFraction"].as<float>(m_PageFraction);
		if (std::isfinite(fraction))
			m_PageFraction = std::clamp(fraction, 0.1f, 0.8f);
	}

	void ApplicationSettingsPanel::DrawEditorPage()
	{
		bool notifyBindings = false;
		bool autoOpenMostRecentProject = LoadAutoOpenMostRecentProjectSetting();

		ImGui::TextUnformatted("Editor");
		ImGui::Separator();

		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
		if (m_Bindings.VSync && ImGuiEx::Property("VSync", *m_Bindings.VSync))
			notifyBindings = true;

		// Frame-rate cap. Only meaningful with VSync off: with it on, the display paces the
		// loop and a cap could only ever sit below the refresh rate, so the control is
		// disabled rather than left looking active but inert.
		if (m_Bindings.TargetFrameRate)
		{
			const bool vsyncOn = m_Bindings.VSync && *m_Bindings.VSync;
			ImGuiEx::ScopedDisable disableWhenVSynced(vsyncOn);

			int32_t& targetFrameRate = *m_Bindings.TargetFrameRate;
			int32_t presetIndex = FindFrameRatePresetIndex(targetFrameRate);

			if (ImGuiEx::PropertyDropdown("Frame Rate Limit", s_FrameRateLimitOptions, (int32_t)std::size(s_FrameRateLimitOptions), &presetIndex))
			{
				const int32_t presetValue = s_FrameRateLimitValues[presetIndex];
				// Switching *to* Custom keeps whatever rate was already active as the
				// slider's starting point, so the view does not jump on selection.
				targetFrameRate = (presetValue == kCustomFrameRateSentinel)
					? std::clamp(targetFrameRate <= 0 ? 144 : targetFrameRate, kMinCustomFrameRate, kMaxCustomFrameRate)
					: presetValue;
				notifyBindings = true;
			}

			if (s_FrameRateLimitValues[presetIndex] == kCustomFrameRateSentinel)
			{
				int32_t customFrameRate = std::clamp(targetFrameRate <= 0 ? 144 : targetFrameRate, kMinCustomFrameRate, kMaxCustomFrameRate);
				if (ImGuiEx::Property("Custom Limit (FPS)", customFrameRate, kMinCustomFrameRate, kMaxCustomFrameRate))
				{
					targetFrameRate = std::clamp(customFrameRate, kMinCustomFrameRate, kMaxCustomFrameRate);
					notifyBindings = true;
				}
			}
		}

		// Present mode. Only applies with VSync off, where the choice is between never
		// tearing (Mailbox) and being able to exceed the display refresh (Immediate).
		if (m_Bindings.PreferImmediatePresentMode)
		{
			const bool vsyncOn = m_Bindings.VSync && *m_Bindings.VSync;
			ImGuiEx::ScopedDisable disableWhenVSynced(vsyncOn);

			int32_t modeIndex = *m_Bindings.PreferImmediatePresentMode ? 1 : 0;
			if (ImGuiEx::PropertyDropdown("Present Mode", s_PresentModeOptions, (int32_t)std::size(s_PresentModeOptions), &modeIndex))
			{
				*m_Bindings.PreferImmediatePresentMode = (modeIndex == 1);
				notifyBindings = true;
			}
		}

		// Swapchain images. Left enabled under VSync because it still affects latency
		// there, it just stops being a frame-rate control.
		if (m_Bindings.SwapChainBufferCount)
		{
			int32_t& bufferCount = *m_Bindings.SwapChainBufferCount;
			int32_t optionIndex = std::clamp(bufferCount - kMinSwapChainBufferCount, 0, kSwapChainBufferCountOptionCount - 1);

			if (ImGuiEx::PropertyDropdown("Swapchain Images", s_SwapChainBufferCountOptions, kSwapChainBufferCountOptionCount, &optionIndex))
			{
				bufferCount = optionIndex + kMinSwapChainBufferCount;
				notifyBindings = true;
			}
		}

		if (ImGuiEx::Property("Auto-open Most Recent Project", autoOpenMostRecentProject))
			SaveAutoOpenMostRecentProjectSetting(autoOpenMostRecentProject);

		if (m_UserPreferences)
		{
			bool showSplashScreen = m_UserPreferences->ShowSplashScreen;
			if (ImGuiEx::Property("Show Splash Screen", showSplashScreen))
			{
				m_UserPreferences->ShowSplashScreen = showSplashScreen;
				SaveUserPreferences();
			}
		}

			// Simple UX = the minimal default layout; unchecking it switches to the fuller Advanced
			// workspace. The switch re-docks panels, so it is routed through the callback.
			if (m_Bindings.SimpleLayout)
			{
				bool simpleLayout = *m_Bindings.SimpleLayout;
				if (ImGuiEx::Property("Simple UX", simpleLayout) && m_Bindings.OnLayoutModeChanged)
					m_Bindings.OnLayoutModeChanged(simpleLayout);
			}

		if (m_Bindings.GlassOverlays && ImGuiEx::Property("Glass Overlays", *m_Bindings.GlassOverlays,
			"Popups, menus, tooltips and the viewport overlays blur what is behind them."))
			notifyBindings = true;
		ImGuiEx::EndPropertyGrid();

		if (notifyBindings && m_Bindings.OnPreferencesChanged)
			m_Bindings.OnPreferencesChanged();

		ImGui::Spacing();
		ImGui::TextUnformatted("Threading");
		ImGui::Separator();

		{
			auto& settings = Application::Get().GetSettings();
			const ThreadingPolicy currentPolicy = ThreadingPolicyFromString(settings.Get("Core.ThreadingPolicy", "Multi"));
			int32_t selected = (currentPolicy == ThreadingPolicy::SingleThreaded) ? 1 : 0;

			static const char* s_ThreadingOptions[] = { "Multi-threaded", "Single-threaded" };

			ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
			if (ImGuiEx::PropertyDropdown("Threading Mode", s_ThreadingOptions, 2, &selected))
			{
				settings.Set("Core.ThreadingPolicy", selected == 1 ? "Single" : "Multi");
				settings.Serialize();
			}
			ImGuiEx::EndPropertyGrid();

			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped("Applies after restarting the editor.");
			}

			// Async transfer queue — routes mesh/texture uploads onto the GPU's
			// dedicated copy queue so asset streaming doesn't stall rendering.
			// Applies live; no-op when the GPU has no dedicated transfer queue.
			bool asyncTransfer = settings.Get("Renderer.AsyncTransferQueue", "false") != "false";
			const bool transferAvailable = Renderer::HasDedicatedTransferQueue();

			ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
			if (ImGuiEx::Property("Async Transfer Queue", asyncTransfer))
			{
				settings.Set("Renderer.AsyncTransferQueue", asyncTransfer ? "true" : "false");
				settings.Serialize();
				Renderer::SetAsyncTransferQueueEnabled(asyncTransfer);
			}
			ImGuiEx::EndPropertyGrid();

			if (transferAvailable)
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped("Uploads meshes/textures on the GPU's dedicated copy queue.");
			}
			else
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped(
					"No dedicated transfer queue on this GPU; uploads use the graphics queue.");
			}
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Startup Project");
		ImGui::Separator();

		const std::string startupProject = (m_UserPreferences && !m_UserPreferences->StartupProject.empty()) ? m_UserPreferences->StartupProject : "None";
		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
		ImGuiEx::Property("Project", startupProject);
		ImGuiEx::EndPropertyGrid();
		const bool hasStartupProject = m_UserPreferences && !m_UserPreferences->StartupProject.empty();
		const ImGuiEx::PanelAction startupActions[] = {
			{0, "Use current project", LUX_ICON_FOLDER_OPEN, nullptr,
			 m_UserPreferences && Project::GetActive()},
			{1, "Clear startup project", LUX_ICON_TIMES, nullptr, hasStartupProject}};
		switch (ImGuiEx::PanelActionBar("##startup_actions", startupActions))
		{
		case 0:
			m_UserPreferences->StartupProject = Utils::PathToUTF8(Project::GetActive()->GetProjectFilePath());
			SaveUserPreferences();
			break;
		case 1:
			m_UserPreferences->StartupProject.clear();
			SaveUserPreferences();
			break;
		default:
			break;
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Recent Projects");
		ImGui::Separator();

		const int recentProjectCount = m_UserPreferences ? (int)m_UserPreferences->RecentProjects.size() : 0;
		ImGui::Text("%d recent project%s saved", recentProjectCount, recentProjectCount == 1 ? "" : "s");

		const ImGuiEx::PanelAction recentActions[] = {{0, "Clear recent projects", LUX_ICON_TRASH}};
		if (ImGuiEx::PanelActionBar("##recent_actions", recentActions) == 0)
			ClearRecentProjects();

		ImGui::Spacing();
		if (ImGui::BeginChild("##recent_projects_list", ImVec2(0.0f, 180.0f), true))
		{
			if (recentProjectCount == 0)
			{
				{
					ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
					ImGui::TextWrapped("No recent projects stored.");
				}
			}
			else
			{
				int i = 0;
				for (const auto& [_, recentProject] : m_UserPreferences->RecentProjects)
				{
					if (i++ >= s_MaxRecentProjects)
						break;

					ImGui::PushTextWrapPos();
					ImGui::BulletText("%s", recentProject.FilePath.c_str());
					ImGui::PopTextWrapPos();
				}
			}
		}
		ImGui::EndChild();
	}

	void ApplicationSettingsPanel::DrawViewportPage()
	{
		bool notifyBindings = false;

		ImGui::TextUnformatted("Viewport");
		ImGui::Separator();

		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
		if (m_Bindings.UseGizmoSnap && ImGuiEx::Property("Enable Gizmo Snap", *m_Bindings.UseGizmoSnap))
			notifyBindings = true;

		if (m_Bindings.TranslationSnapValue && ImGuiEx::Property("Translation Snap", *m_Bindings.TranslationSnapValue, 0.05f, 0.05f, 100.0f))
			notifyBindings = true;

		if (m_Bindings.RotationSnapValue && ImGuiEx::Property("Rotation Snap", *m_Bindings.RotationSnapValue, 1.0f, 1.0f, 360.0f))
			notifyBindings = true;

		if (m_Bindings.ScaleSnapValue && ImGuiEx::Property("Scale Snap", *m_Bindings.ScaleSnapValue, 0.01f, 0.01f, 1.0f))
			notifyBindings = true;

		if (m_Bindings.ShowBoundingBoxes && ImGuiEx::Property("Show Bounding Boxes", *m_Bindings.ShowBoundingBoxes))
			notifyBindings = true;

		if (m_Bindings.ShowEntityIcons && ImGuiEx::Property("Show Entity Icons", *m_Bindings.ShowEntityIcons))
			notifyBindings = true;

		if (m_Bindings.ShowViewportPerformanceHUD && ImGuiEx::Property("Performance HUD", *m_Bindings.ShowViewportPerformanceHUD))
			notifyBindings = true;

		if (m_Bindings.ShowPhysicsColliders && ImGuiEx::Property("Show Physics Colliders", *m_Bindings.ShowPhysicsColliders))
			notifyBindings = true;
		ImGuiEx::EndPropertyGrid();

		if (m_Bindings.Grid)
		{
			SceneRenderer::GridSettings& grid = *m_Bindings.Grid;

			ImGui::Spacing();
			ImGui::TextUnformatted("Grid");
			ImGui::Separator();

			ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
			notifyBindings |= ImGuiEx::PropertyColor("X Axis Colour", grid.XAxisColor);
			notifyBindings |= ImGuiEx::PropertyColor("Z Axis Colour", grid.ZAxisColor);
			notifyBindings |= ImGuiEx::PropertyColor("Minor Line Colour", grid.MinorColor);
			notifyBindings |= ImGuiEx::PropertyColor("Major Line Colour", grid.MajorColor);
			notifyBindings |= ImGuiEx::Property("Axis Line Width", grid.AxisLineWidth, 0.05f, 0.5f, 10.0f, "Pixels.");
			notifyBindings |= ImGuiEx::Property("Minor Line Width", grid.MinorLineWidth, 0.05f, 0.5f, 10.0f, "Pixels.");
			notifyBindings |= ImGuiEx::Property("Major Line Width", grid.MajorLineWidth, 0.05f, 0.5f, 10.0f, "Pixels.");
			notifyBindings |= ImGuiEx::Property("Cell Size", grid.CellSize, 0.01f, 0.001f, 1000.0f,
				"Finest cell in metres. Coarser cells step up by powers of ten as the camera moves away.");
			notifyBindings |= ImGuiEx::Property("Fade Distance", grid.FadeDistance, 1.0f, 1.0f, 100000.0f, "Metres from the camera.");
			ImGuiEx::EndPropertyGrid();

			const ImGuiEx::PanelAction gridActions[] = {{0, "Reset grid", LUX_ICON_UNDO}};
			if (ImGuiEx::PanelActionBar("##grid_actions", gridActions) == 0)
			{
				grid = SceneRenderer::GridSettings{};
				notifyBindings = true;
			}
		}

		if (m_Bindings.GamepadNavigation)
		{
			ImGui::Spacing();
			ImGui::TextUnformatted("Gamepad");
			ImGui::Separator();

			ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
			if (ImGuiEx::Property("Gamepad Navigation", *m_Bindings.GamepadNavigation,
				"Navigate the editor UI with a controller (D-pad to move, A to activate, B to cancel).\nSuspended while playing so the game receives the controller."))
				notifyBindings = true;
			ImGuiEx::EndPropertyGrid();

			if (*m_Bindings.GamepadNavigation && m_Bindings.GamepadNavigationDevice && DrawGamepadDeviceList())
				notifyBindings = true;
		}

		if (notifyBindings && m_Bindings.OnPreferencesChanged)
			m_Bindings.OnPreferencesChanged();
	}

	bool ApplicationSettingsPanel::DrawGamepadDeviceList()
	{
		ImGuiGamepadInfo& selection = *m_Bindings.GamepadNavigationDevice;
		const std::vector<ImGuiGamepadInfo> gamepads = ImGuiLayer::GetConnectedGamepads();
		const ImGuiLayer* imguiLayer = Application::Get().GetImGuiLayer();
		// Highlight the slot actually driving the UI; fall back to the remembered one while
		// navigation is suspended (e.g. during Play).
		const int activeID = (imguiLayer && imguiLayer->GetNavGamepad() >= 0) ? imguiLayer->GetNavGamepad() : selection.JoystickID;
		bool changed = false;

		{
			ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
			ImGui::TextWrapped(
				"Controller that drives the editor UI. Press a button to see which row lights up.");
		}

		const float listHeight = ImGui::GetFrameHeightWithSpacing() * (float)std::clamp((int)gamepads.size() + 2, 3, 8);
		if (ImGui::BeginChild("##gamepad_devices", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders))
		{
			if (ImGui::Selectable("Auto (first connected controller)", selection.GUID.empty()))
			{
				selection = {};
				changed = true;
			}

			bool selectionConnected = selection.GUID.empty();
			for (const ImGuiGamepadInfo& gamepad : gamepads)
			{
				ImGui::PushID(gamepad.JoystickID);

				const bool sameModel = !selection.GUID.empty() && gamepad.GUID == selection.GUID;
				selectionConnected |= sameModel;

				std::string label = std::format("{}  (#{})", gamepad.Name, gamepad.JoystickID + 1);
				if (!gamepad.HasMapping)
					label += "  - no gamepad mapping";

				{
					ImGuiEx::ScopedDisable disableUnmapped(!gamepad.HasMapping);
					if (ImGui::Selectable(label.c_str(), sameModel && gamepad.JoystickID == activeID))
					{
						selection = gamepad;
						selection.HasInput = false;
						changed = true;
					}
				}

				// Live activity dot, so several similar entries can be told apart.
				if (gamepad.HasInput)
				{
					const ImVec2 rowMax = ImGui::GetItemRectMax();
					const float radius = ImGui::GetFontSize() * 0.25f;
					ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(rowMax.x - radius * 3.0f, (ImGui::GetItemRectMin().y + rowMax.y) * 0.5f),
						radius, IM_COL32(80, 230, 100, 255));
				}

				ImGui::PopID();
			}

			if (!selectionConnected)
			{
				ImGuiEx::ScopedDisable disableMissing;
				ImGui::Selectable(std::format("{}  - disconnected", selection.Name).c_str(), true);
			}

			if (gamepads.empty())
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped("No controllers connected.");
			}
		}
		ImGui::EndChild();

		return changed;
	}

	void ApplicationSettingsPanel::DrawContentBrowserPage()
	{
		ImGui::TextUnformatted("Content Browser");
		ImGui::Separator();

		if (!m_ContentBrowserPanel)
		{
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped("Content Browser panel is not available.");
			}
			return;
		}

		float thumbnailSize = m_ContentBrowserPanel->GetThumbnailSize();
		bool showAssetTypes = m_ContentBrowserPanel->GetShowAssetTypes();

		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
		if (ImGuiEx::Property("Thumbnail Size", thumbnailSize, 1.0f, 32.0f, 256.0f))
			m_ContentBrowserPanel->SetThumbnailSize(thumbnailSize);

		if (ImGuiEx::Property("Show Asset Types", showAssetTypes))
			m_ContentBrowserPanel->SetShowAssetTypes(showAssetTypes);
		ImGuiEx::EndPropertyGrid();

		ImGui::Spacing();
		{
			ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
			ImGui::TextWrapped("Changes apply immediately and are stored in the editor settings file.");
		}
	}

	void ApplicationSettingsPanel::DrawDiscordPage()
	{
		ImGui::TextUnformatted("Discord Rich Presence");
		ImGui::Separator();

		// Note this is IsAvailable(), not GetState() - a build with the SDK compiled in still
		// reports State::Disabled while the integration is switched off, and bailing out here on
		// that would hide the very toggle needed to switch it on.
		if (!DiscordSocial::IsAvailable())
		{
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped("The Discord integration is not compiled into this build.");
			}
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped(
					"Regenerate projects with \"Win-GenProjects.bat --discord\" to enable it.");
			}
			return;
		}

		auto& settings = Application::Get().GetSettings();
		bool enabled = settings.Get("Discord.RichPresenceEnabled", "false") == "true";

		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
		if (ImGuiEx::Property("Enable Rich Presence", enabled))
		{
			settings.Set("Discord.RichPresenceEnabled", enabled ? "true" : "false");
			settings.Serialize();
		}

		if (ImGuiEx::Property("Application ID", m_DiscordApplicationID))
		{
			settings.Set("Discord.ApplicationID", m_DiscordApplicationID);
			settings.Serialize();
		}
		ImGuiEx::EndPropertyGrid();

		{
			ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
			ImGui::TextWrapped("Applies after restarting the editor.");
		}
		{
			ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
			ImGui::TextWrapped("Create an application at discord.com/developers to get an ID.");
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Connection");
		ImGui::Separator();

		// The subsystem only initializes during Application startup, so anything changed above
		// needs a restart before there's a client to connect with. Say which thing is missing
		// rather than offering a Connect button that would silently do nothing.
		if (DiscordSocial::GetState() == DiscordSocial::State::Disabled)
		{
			if (!enabled)
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped("Turn on 'Enable Rich Presence' above, then restart the editor.");
			}
			else if (m_DiscordApplicationID.empty())
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped("Set an Application ID above, then restart the editor.");
			}
			else
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped("Enabled. Restart the editor to initialize Discord.");
			}
			return;
		}

		switch (DiscordSocial::GetState())
		{
			case DiscordSocial::State::Ready:
			{
				const std::string& username = DiscordSocial::GetUsername();
				ImGui::Text("Connected as %s", username.empty() ? "<unknown>" : username.c_str());

				if (ImGui::Button("Disconnect"))
					DiscordSocial::Disconnect();
				break;
			}
			case DiscordSocial::State::Authenticating:
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped("Waiting for authorization in your browser...");
			}
				break;
			case DiscordSocial::State::Connecting:
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped("Connecting...");
			}
				break;
			case DiscordSocial::State::Failed:
			{
				const std::string& error = DiscordSocial::GetLastError();
				ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "Failed: %s", error.empty() ? "unknown error" : error.c_str());

				if (ImGui::Button("Retry"))
					DiscordSocial::Connect();
				break;
			}
			default:
			{
				{
					ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
					ImGui::TextWrapped("Not connected.");
				}

				if (ImGui::Button("Connect"))
					DiscordSocial::Connect();

				ImGui::SameLine();
				{
					ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
					ImGui::TextWrapped("Opens your browser to authorize Lux.");
				}
				break;
			}
		}
	}

	void ApplicationSettingsPanel::SaveAutoOpenMostRecentProjectSetting(bool enabled) const
	{
		auto& settings = Application::Get().GetSettings();
		settings.SetInt("Editor.AutoOpenMostRecentProject", enabled ? 1 : 0);
		settings.Serialize();
	}

	bool ApplicationSettingsPanel::LoadAutoOpenMostRecentProjectSetting() const
	{
		auto& settings = Application::Get().GetSettings();
		return settings.GetInt("Editor.AutoOpenMostRecentProject", 1) != 0;
	}

	void ApplicationSettingsPanel::SaveUserPreferences() const
	{
		if (!m_UserPreferences)
			return;

		UserPreferencesSerializer serializer(m_UserPreferences);
		const std::filesystem::path filepath = m_UserPreferences->FilePath.empty() ? (FileSystem::GetPersistentStoragePath() / "UserPreferences.yaml") : m_UserPreferences->FilePath;
		serializer.Serialize(filepath);
	}

	void ApplicationSettingsPanel::ClearRecentProjects()
	{
		if (!m_UserPreferences)
			return;

		m_UserPreferences->RecentProjects.clear();
		SaveUserPreferences();
	}

}
