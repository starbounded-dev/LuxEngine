#include "lpch.h"
#include "ApplicationSettingsPanel.h"

#include "ContentBrowserPanel.h"

#include "Lux/Core/Application.h"
#include "Lux/ImGui/ImGuiEx.h"

#include <imgui/imgui.h>

#include <algorithm>

namespace Lux {

	namespace
	{
		constexpr int s_MaxRecentProjects = 10;

		std::string GetRecentProjectKey(size_t index)
		{
			return "RecentProjects." + std::to_string(index);
		}

		int GetRecentProjectCount()
		{
			auto& settings = Application::Get().GetSettings();
			return std::max(settings.GetInt("RecentProjects.Count", 0), 0);
		}
	}

	ApplicationSettingsPanel::ApplicationSettingsPanel(const Ref<ContentBrowserPanel>& contentBrowserPanel, EditorPreferencesBindings bindings)
		: m_ContentBrowserPanel(contentBrowserPanel), m_Bindings(std::move(bindings))
	{
		m_Pages.push_back({ "Editor", [this]() { DrawEditorPage(); } });
		m_Pages.push_back({ "Viewport", [this]() { DrawViewportPage(); } });
		m_Pages.push_back({ "Content Browser", [this]() { DrawContentBrowserPage(); } });
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

		if (ImGuiEx::Property("Auto-open Most Recent Project", autoOpenMostRecentProject))
			SaveAutoOpenMostRecentProjectSetting(autoOpenMostRecentProject);
		ImGuiEx::EndPropertyGrid();

		if (notifyBindings && m_Bindings.OnPreferencesChanged)
			m_Bindings.OnPreferencesChanged();

		ImGui::Spacing();
		ImGui::TextUnformatted("Recent Projects");
		ImGui::Separator();

		const int recentProjectCount = GetRecentProjectCount();
		ImGui::Text("%d recent project%s saved", recentProjectCount, recentProjectCount == 1 ? "" : "s");

		if (ImGui::Button("Clear Recent Projects"))
			ClearRecentProjects();

		ImGui::Spacing();
		if (ImGui::BeginChild("##recent_projects_list", ImVec2(0.0f, 180.0f), true))
		{
			auto& settings = Application::Get().GetSettings();
			if (recentProjectCount == 0)
			{
				ImGui::TextDisabled("No recent projects stored.");
			}
			else
			{
				for (int i = 0; i < recentProjectCount && i < s_MaxRecentProjects; i++)
				{
					const std::string projectPath = settings.Get(GetRecentProjectKey((size_t)i));
					if (projectPath.empty())
						continue;

					ImGui::BulletText("%s", projectPath.c_str());
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

	void ApplicationSettingsPanel::ClearRecentProjects() const
	{
		auto& settings = Application::Get().GetSettings();
		settings.SetInt("RecentProjects.Count", 0);
		for (int i = 0; i < s_MaxRecentProjects; i++)
			settings.Set(GetRecentProjectKey((size_t)i), "");
		settings.Serialize();
	}

}
