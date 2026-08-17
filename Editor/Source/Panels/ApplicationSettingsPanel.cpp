#include "lpch.h"
#include "ApplicationSettingsPanel.h"

#include "ContentBrowserPanel.h"

#include "Lux/Core/Application.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Social/DiscordSocial.h"
#include "Lux/Utilities/FileSystem.h"

#include <imgui/imgui.h>

#include <algorithm>
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
		if (!isOpen)
			return;

		if (ImGui::Begin("Application Settings", &isOpen))
		{
			const ImGuiTableFlags tableFlags = ImGuiTableFlags_Resizable
				| ImGuiTableFlags_SizingFixedFit
				| ImGuiTableFlags_BordersInnerV;

			if (ImGui::BeginTable("##application_settings_table", 2, tableFlags, ImGui::GetContentRegionAvail()))
			{
				ImGui::TableSetupColumn("Pages", ImGuiTableColumnFlags_WidthFixed, 220.0f);
				ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				DrawPageList();

				ImGui::TableSetColumnIndex(1);
				if (m_CurrentPage < m_Pages.size())
				{
					ImGui::BeginChild("##application_settings_content", ImGui::GetContentRegionAvail(), false);
					m_Pages[m_CurrentPage].RenderFunction();
					ImGui::EndChild();
				}

				ImGui::EndTable();
			}
		}

		ImGui::End();
	}

	void ApplicationSettingsPanel::DrawPageList()
	{
		if (!ImGui::BeginChild("##application_settings_pages"))
			return;

		for (uint32_t i = 0; i < m_Pages.size(); i++)
		{
			const bool selected = (m_CurrentPage == i);
			if (ImGui::Selectable(m_Pages[i].Name, selected, ImGuiSelectableFlags_SpanAllColumns))
				m_CurrentPage = i;
		}

		ImGui::EndChild();
	}

	void ApplicationSettingsPanel::DrawEditorPage()
	{
		bool notifyBindings = false;
		bool autoOpenMostRecentProject = LoadAutoOpenMostRecentProjectSetting();

		ImGui::TextUnformatted("Editor");
		ImGui::Separator();

		ImGuiEx::BeginPropertyGrid();
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
			bool showWelcomeScreen = m_UserPreferences->ShowWelcomeScreen;
			if (ImGuiEx::Property("Show Welcome Screen", showWelcomeScreen))
			{
				m_UserPreferences->ShowWelcomeScreen = showWelcomeScreen;
				SaveUserPreferences();
			}
		}
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

			ImGuiEx::BeginPropertyGrid();
			if (ImGuiEx::PropertyDropdown("Threading Mode", s_ThreadingOptions, 2, &selected))
			{
				settings.Set("Core.ThreadingPolicy", selected == 1 ? "Single" : "Multi");
				settings.Serialize();
			}
			ImGuiEx::EndPropertyGrid();

			ImGui::TextDisabled("Applies after restarting the editor.");

			// Async transfer queue — routes mesh/texture uploads onto the GPU's
			// dedicated copy queue so asset streaming doesn't stall rendering.
			// Applies live; no-op when the GPU has no dedicated transfer queue.
			bool asyncTransfer = settings.Get("Renderer.AsyncTransferQueue", "false") != "false";
			const bool transferAvailable = Application::GetGraphicsDeviceManager() &&
				Application::GetGraphicsDeviceManager()->IsTransferQueueAvailable();

			ImGuiEx::BeginPropertyGrid();
			if (ImGuiEx::Property("Async Transfer Queue", asyncTransfer))
			{
				settings.Set("Renderer.AsyncTransferQueue", asyncTransfer ? "true" : "false");
				settings.Serialize();
				Renderer::SetAsyncTransferQueueEnabled(asyncTransfer);
			}
			ImGuiEx::EndPropertyGrid();

			if (transferAvailable)
				ImGui::TextDisabled("Uploads meshes/textures on the GPU's dedicated copy queue.");
			else
				ImGui::TextDisabled("No dedicated transfer queue on this GPU; uploads use the graphics queue.");
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Startup Project");
		ImGui::Separator();

		const std::string startupProject = (m_UserPreferences && !m_UserPreferences->StartupProject.empty()) ? m_UserPreferences->StartupProject : "None";
		ImGuiEx::Property("Project", startupProject);

		if (Project::GetActive())
		{
			if (ImGui::Button("Use Current Project"))
			{
				m_UserPreferences->StartupProject = Project::GetActive()->GetProjectFilePath().generic_string();
				SaveUserPreferences();
			}
			ImGui::SameLine();
		}

		const bool hasStartupProject = m_UserPreferences && !m_UserPreferences->StartupProject.empty();
		ImGui::BeginDisabled(!hasStartupProject);
		if (ImGui::Button("Clear Startup Project"))
		{
			m_UserPreferences->StartupProject.clear();
			SaveUserPreferences();
		}
		ImGui::EndDisabled();

		ImGui::Spacing();
		ImGui::TextUnformatted("Recent Projects");
		ImGui::Separator();

		const int recentProjectCount = m_UserPreferences ? (int)m_UserPreferences->RecentProjects.size() : 0;
		ImGui::Text("%d recent project%s saved", recentProjectCount, recentProjectCount == 1 ? "" : "s");

		if (ImGui::Button("Clear Recent Projects"))
			ClearRecentProjects();

		ImGui::Spacing();
		if (ImGui::BeginChild("##recent_projects_list", ImVec2(0.0f, 180.0f), true))
		{
			if (recentProjectCount == 0)
			{
				ImGui::TextDisabled("No recent projects stored.");
			}
			else
			{
				int i = 0;
				for (const auto& [_, recentProject] : m_UserPreferences->RecentProjects)
				{
					if (i++ >= s_MaxRecentProjects)
						break;

					ImGui::BulletText("%s", recentProject.FilePath.c_str());
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

		ImGuiEx::BeginPropertyGrid();
		if (m_Bindings.UseGizmoSnap && ImGuiEx::Property("Enable Gizmo Snap", *m_Bindings.UseGizmoSnap))
			notifyBindings = true;

		if (m_Bindings.TranslationSnapValue && ImGuiEx::Property("Translation Snap", *m_Bindings.TranslationSnapValue, 0.05f, 0.05f, 100.0f))
			notifyBindings = true;

		if (m_Bindings.RotationSnapValue && ImGuiEx::Property("Rotation Snap", *m_Bindings.RotationSnapValue, 1.0f, 1.0f, 360.0f))
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

		if (notifyBindings && m_Bindings.OnPreferencesChanged)
			m_Bindings.OnPreferencesChanged();
	}

	void ApplicationSettingsPanel::DrawContentBrowserPage()
	{
		ImGui::TextUnformatted("Content Browser");
		ImGui::Separator();

		if (!m_ContentBrowserPanel)
		{
			ImGui::TextDisabled("Content Browser panel is not available.");
			return;
		}

		float thumbnailSize = m_ContentBrowserPanel->GetThumbnailSize();
		bool showAssetTypes = m_ContentBrowserPanel->GetShowAssetTypes();

		ImGuiEx::BeginPropertyGrid();
		if (ImGuiEx::Property("Thumbnail Size", thumbnailSize, 1.0f, 32.0f, 256.0f))
			m_ContentBrowserPanel->SetThumbnailSize(thumbnailSize);

		if (ImGuiEx::Property("Show Asset Types", showAssetTypes))
			m_ContentBrowserPanel->SetShowAssetTypes(showAssetTypes);
		ImGuiEx::EndPropertyGrid();

		ImGui::Spacing();
		ImGui::TextDisabled("Changes apply immediately and are stored in the editor settings file.");
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
			ImGui::TextDisabled("The Discord integration is not compiled into this build.");
			ImGui::TextDisabled("Regenerate projects with \"Win-GenProjects.bat --discord\" to enable it.");
			return;
		}

		auto& settings = Application::Get().GetSettings();
		bool enabled = settings.Get("Discord.RichPresenceEnabled", "false") == "true";

		ImGuiEx::BeginPropertyGrid();
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

		ImGui::TextDisabled("Applies after restarting the editor.");
		ImGui::TextDisabled("Create an application at discord.com/developers to get an ID.");

		ImGui::Spacing();
		ImGui::TextUnformatted("Connection");
		ImGui::Separator();

		// The subsystem only initializes during Application startup, so anything changed above
		// needs a restart before there's a client to connect with. Say which thing is missing
		// rather than offering a Connect button that would silently do nothing.
		if (DiscordSocial::GetState() == DiscordSocial::State::Disabled)
		{
			if (!enabled)
				ImGui::TextDisabled("Turn on 'Enable Rich Presence' above, then restart the editor.");
			else if (m_DiscordApplicationID.empty())
				ImGui::TextDisabled("Set an Application ID above, then restart the editor.");
			else
				ImGui::TextDisabled("Enabled. Restart the editor to initialize Discord.");
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
				ImGui::TextDisabled("Waiting for authorization in your browser...");
				break;
			case DiscordSocial::State::Connecting:
				ImGui::TextDisabled("Connecting...");
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
				ImGui::TextDisabled("Not connected.");

				if (ImGui::Button("Connect"))
					DiscordSocial::Connect();

				ImGui::SameLine();
				ImGui::TextDisabled("Opens your browser to authorize Lux.");
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
