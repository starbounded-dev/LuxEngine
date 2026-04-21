#include "lpch.h"
#include "ProjectSettingsWindow.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Project/ProjectSerializer.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Scripting/ScriptEngine.h"
#include "Lux/Utilities/FileDialogs.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cstdlib>

namespace Lux {

	namespace
	{
		constexpr const char* s_RenderResolutionOptions[] = { "128", "256", "512", "1024", "2048", "4096" };
		constexpr size_t s_RenderResolutionOptionCount = sizeof(s_RenderResolutionOptions) / sizeof(s_RenderResolutionOptions[0]);

		int32_t ResolutionToComboIndex(uint32_t resolution)
		{
			switch (resolution)
			{
			case 128: return 0;
			case 256: return 1;
			case 512: return 2;
			case 1024: return 3;
			case 2048: return 4;
			case 4096: return 5;
			default: return 3;
			}
		}

		uint32_t ComboIndexToResolution(int32_t index)
		{
			index = std::clamp(index, 0, (int32_t)s_RenderResolutionOptionCount - 1);
			return (uint32_t)std::atoi(s_RenderResolutionOptions[index]);
		}
	}

	ProjectSettingsWindow::ProjectSettingsWindow()
	{
	}

	void ProjectSettingsWindow::OnImGuiRender(bool& isOpen)
	{
		if (!m_Project)
		{
			isOpen = false;
			return;
		}

		if (ImGui::Begin("Project Settings", &isOpen))
		{
			RenderGeneralSettings();
			RenderRendererSettings();
			RenderAudioSettings();
			RenderScriptingSettings();
			RenderPhysicsSettings();
			RenderLogSettings();

			ImGui::Spacing();
			if (m_Dirty)
				ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "Unsaved project changes");

			if (ImGui::Button("Save Project Settings"))
				SaveProject();

			if (!isOpen)
				OnClose();
		}

		ImGui::End();
	}

	void ProjectSettingsWindow::OnProjectChanged(const Ref<Project>& project)
	{
		m_Project = project;
		m_Dirty = false;

		if (!m_Project)
		{
			m_DefaultScene = 0;
			m_NameBuffer[0] = '\0';
			m_ScriptModulePathBuffer[0] = '\0';
			return;
		}

		m_DefaultScene = m_Project->GetConfig().StartSceneHandle;
		SyncBuffersFromProject();
	}

	void ProjectSettingsWindow::OnClose()
	{
		if (m_Dirty)
			SaveProject();
	}

	void ProjectSettingsWindow::SyncBuffersFromProject()
	{
		if (!m_Project)
			return;

		const std::string& name = m_Project->GetConfig().Name;
		const std::string scriptModulePath = m_Project->GetConfig().ScriptModulePath.generic_string();

		strncpy_s(m_NameBuffer, name.c_str(), _TRUNCATE);
		strncpy_s(m_ScriptModulePathBuffer, scriptModulePath.c_str(), _TRUNCATE);
	}

	void ProjectSettingsWindow::SaveProject()
	{
		if (!m_Project)
			return;

		std::filesystem::path projectFilePath = m_Project->GetProjectFilePath();
		if (projectFilePath.empty())
		{
			std::string filepath = FileDialogs::SaveFile("Lux Project (*.luxproj)\0*.luxproj\0");
			if (filepath.empty())
				return;

			projectFilePath = filepath;
		}

		if (Project::SaveActive(projectFilePath))
		{
			m_Project = Project::GetActive();
			m_DefaultScene = m_Project->GetConfig().StartSceneHandle;
			SyncBuffersFromProject();
			m_Dirty = false;
		}
	}

	void ProjectSettingsWindow::RenderGeneralSettings()
	{
		if (!ImGuiEx::PropertyGridHeader("General"))
			return;

		auto& config = m_Project->GetConfig();

		ImGuiEx::BeginPropertyGrid();
		if (ImGuiEx::Property("Name", m_NameBuffer, sizeof(m_NameBuffer)))
		{
			config.Name = m_NameBuffer;
			m_Dirty = true;
		}

		ImGuiEx::Property("Project File", m_Project->GetProjectFilePath().generic_string());
		ImGuiEx::Property("Project Directory", m_Project->GetProjectDirectory().generic_string());
		ImGuiEx::Property("Asset Directory", config.AssetDirectory.generic_string());
		ImGuiEx::Property("Asset Registry", config.AssetRegistryPath.generic_string());
		ImGuiEx::EndPropertyGrid();

		ImGui::Spacing();
		ImGui::TextUnformatted("Startup Scene");

		std::string sceneLabel = "None";
		if (m_DefaultScene)
		{
			const AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(m_DefaultScene);
			if (metadata.IsValid())
				sceneLabel = metadata.FilePath.generic_string();
		}

		ImGui::Button(sceneLabel.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
			{
				const size_t itemCount = payload->DataSize / sizeof(AssetHandle);
				if (itemCount > 0)
				{
					const AssetHandle droppedHandle = *(const AssetHandle*)payload->Data;
					if (AssetManager::GetAssetType(droppedHandle) == AssetType::Scene)
					{
						const AssetMetadata metadata = Project::GetEditorAssetManager()->GetMetadata(droppedHandle);
						if (metadata.IsValid())
						{
							m_DefaultScene = droppedHandle;
							config.StartSceneHandle = droppedHandle;
							config.StartScene = metadata.FilePath.generic_string();
							m_Dirty = true;
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (m_DefaultScene)
		{
			if (ImGui::Button("Clear Startup Scene"))
			{
				m_DefaultScene = 0;
				config.StartSceneHandle = 0;
				config.StartScene.clear();
				m_Dirty = true;
			}
		}

		ImGui::TreePop();
	}

	void ProjectSettingsWindow::RenderRendererSettings()
	{
		if (!ImGuiEx::PropertyGridHeader("Renderer", false))
			return;

		auto& rendererConfig = Renderer::GetConfig();

		ImGuiEx::BeginPropertyGrid();
		ImGuiEx::Property("Compute HDR Environment Maps", rendererConfig.ComputeEnvironmentMaps);

		int32_t environmentMapSizeIndex = ResolutionToComboIndex(rendererConfig.EnvironmentMapResolution);
		if (ImGui::BeginCombo("Environment Map Size", s_RenderResolutionOptions[environmentMapSizeIndex]))
		{
			for (int32_t i = 0; i < (int32_t)s_RenderResolutionOptionCount; i++)
			{
				const bool selected = (environmentMapSizeIndex == i);
				if (ImGui::Selectable(s_RenderResolutionOptions[i], selected))
					rendererConfig.EnvironmentMapResolution = ComboIndexToResolution(i);
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		int32_t irradianceSamplesIndex = ResolutionToComboIndex(rendererConfig.IrradianceMapComputeSamples);
		if (ImGui::BeginCombo("Irradiance Samples", s_RenderResolutionOptions[irradianceSamplesIndex]))
		{
			for (int32_t i = 0; i < (int32_t)s_RenderResolutionOptionCount; i++)
			{
				const bool selected = (irradianceSamplesIndex == i);
				if (ImGui::Selectable(s_RenderResolutionOptions[i], selected))
					rendererConfig.IrradianceMapComputeSamples = ComboIndexToResolution(i);
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGuiEx::EndPropertyGrid();

		ImGui::TreePop();
	}

	void ProjectSettingsWindow::RenderAudioSettings()
	{
		if (!ImGuiEx::PropertyGridHeader("Audio", false))
			return;

		ImGui::TextDisabled("Lux does not expose project-level audio settings here yet.");
		ImGui::TextDisabled("Audio engine initialization is handled automatically when the project opens.");
		ImGui::TreePop();
	}

	void ProjectSettingsWindow::RenderScriptingSettings()
	{
		if (!ImGuiEx::PropertyGridHeader("Scripting", false))
			return;

		auto& config = m_Project->GetConfig();

		ImGuiEx::BeginPropertyGrid();
		if (ImGuiEx::Property("Script Module Path", m_ScriptModulePathBuffer, sizeof(m_ScriptModulePathBuffer)))
		{
			config.ScriptModulePath = m_ScriptModulePathBuffer;
			m_Dirty = true;
		}
		ImGuiEx::EndPropertyGrid();

		if (!config.ScriptModulePath.empty())
		{
			const std::filesystem::path resolvedModulePath = Project::GetActiveAssetDirectory() / config.ScriptModulePath;
			ImGui::TextDisabled("Resolved Path: %s", resolvedModulePath.generic_string().c_str());
		}

		if (ImGui::Button("Reload Assembly"))
			ScriptEngine::ReloadAssembly();

		ImGui::TreePop();
	}

	void ProjectSettingsWindow::RenderPhysicsSettings()
	{
		if (!ImGuiEx::PropertyGridHeader("Physics", false))
			return;

		ImGui::TextDisabled("Project-level physics layers and filters are not wired into Lux yet.");
		ImGui::TreePop();
	}

	void ProjectSettingsWindow::RenderLogSettings()
	{
		if (!ImGuiEx::PropertyGridHeader("Log", false))
			return;

		if (ImGui::Button("Reset Tag Filters"))
			Log::SetDefaultTagSettings();

		ImGui::Spacing();
		if (ImGui::BeginTable("##project_log_settings", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Tag");
			ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 120.0f);
			ImGui::TableHeadersRow();

			int32_t rowIndex = 0;
			for (auto& [tag, tagDetails] : Log::EnabledTags())
			{
				ImGui::PushID(rowIndex++);
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(tag.empty() ? "(Default)" : tag.c_str());

				ImGui::TableSetColumnIndex(1);
				ImGui::Checkbox("##enabled", &tagDetails.Enabled);

				ImGui::TableSetColumnIndex(2);
				const char* currentLevel = Log::LevelToString(tagDetails.LevelFilter);
				if (ImGui::BeginCombo("##level", currentLevel))
				{
					for (Log::Level level : { Log::Level::Trace, Log::Level::Info, Log::Level::Warn, Log::Level::Error, Log::Level::Fatal })
					{
						const bool selected = (tagDetails.LevelFilter == level);
						if (ImGui::Selectable(Log::LevelToString(level), selected))
							tagDetails.LevelFilter = level;
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		ImGui::TreePop();
	}

}
