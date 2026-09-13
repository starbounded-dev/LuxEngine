#include "lpch.h"
#include "ProjectSettingsWindow.h"

#include "Lux/Audio/AudioBankBuilder.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioSurfaceTable.h"
#include "Lux/ImGui/AudioWidgets.h"
#include "RuntimeExportUtils.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Project/ProjectSerializer.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Scripting/ScriptEngine.h"
#include "Lux/Scripting/ScriptBuilder.h"
#include "Lux/Utilities/FileDialogs.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>

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

		std::string JoinLayerNames(const std::vector<std::string>& names)
		{
			std::string result;
			for (size_t i = 0; i < names.size(); i++)
			{
				if (i > 0)
					result += ", ";
				result += names[i];
			}

			return result;
		}

		std::vector<std::string> SplitLayerNames(const std::string& names)
		{
			std::vector<std::string> result;
			std::stringstream stream(names);
			std::string item;
			while (std::getline(stream, item, ','))
			{
				const size_t first = item.find_first_not_of(" \t");
				if (first == std::string::npos)
					continue;

				const size_t last = item.find_last_not_of(" \t");
				result.emplace_back(item.substr(first, last - first + 1));
			}

			return result;
		}


		using RuntimeExport::FileExists;
		using RuntimeExport::FindRepositoryRootFrom;
		using RuntimeExport::GetRuntimeExecutablePath;
		using RuntimeExport::BuildRuntimeExecutable;
		using RuntimeExport::ResolveScriptProjectFile;
		using RuntimeExport::IsScriptModuleOutdated;
		using RuntimeExport::BuildScriptModule;

		constexpr RuntimeExportTarget s_RuntimeExportTargets[] = {
			RuntimeExportTarget::Debug,
			RuntimeExportTarget::Release,
			RuntimeExportTarget::Dist
		};
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
			RenderRuntimeExportSettings();
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
			m_RuntimeIcon = 0;
			m_NameBuffer[0] = '\0';
			m_RuntimeGameNameBuffer[0] = '\0';
			m_ScriptModulePathBuffer[0] = '\0';
			return;
		}

		m_DefaultScene = m_Project->GetConfig().StartSceneHandle;
		m_RuntimeIcon = m_Project->GetConfig().RuntimeExport.IconHandle;
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
		const std::string runtimeGameName = m_Project->GetConfig().RuntimeExport.GameName.empty() ? name : m_Project->GetConfig().RuntimeExport.GameName;
		const std::string scriptModulePath = m_Project->GetConfig().ScriptModulePath.generic_string();
		const std::string& defaultNamespace = m_Project->GetConfig().DefaultNamespace;

		std::strncpy(m_NameBuffer, name.c_str(), sizeof(m_NameBuffer) - 1);
		std::strncpy(m_RuntimeGameNameBuffer, runtimeGameName.c_str(), sizeof(m_RuntimeGameNameBuffer) - 1);
		std::strncpy(m_ScriptModulePathBuffer, scriptModulePath.c_str(), sizeof(m_ScriptModulePathBuffer) - 1);
		std::strncpy(m_DefaultNamespaceBuffer, defaultNamespace.c_str(), sizeof(m_DefaultNamespaceBuffer) - 1);
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
		auto syncStartupScenePath = [&config](AssetHandle sceneHandle)
		{
			config.StartSceneHandle = sceneHandle;
			config.StartScene.clear();

			if (!sceneHandle)
				return;

			if (Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager())
			{
				const AssetMetadata metadata = editorAssetManager->GetMetadata(sceneHandle);
				if (metadata.IsValid())
					config.StartScene = metadata.FilePath.generic_string();
			}
		};

		ImGuiEx::BeginPropertyGrid();
		if (ImGuiEx::Property("Name", m_NameBuffer, sizeof(m_NameBuffer)))
		{
			config.Name = m_NameBuffer;
			m_Dirty = true;
		}

		ImGuiEx::Property("Project File", m_Project->GetProjectFilePath().generic_string());
		ImGuiEx::Property("Project Directory", m_Project->GetProjectDirectory().generic_string());

		std::string assetDirectory = config.AssetDirectory.generic_string();
		if (ImGuiEx::Property("Asset Directory", assetDirectory))
		{
			config.AssetDirectory = assetDirectory;
			m_Dirty = true;
		}

		std::string assetRegistryPath = config.AssetRegistryPath.generic_string();
		if (ImGuiEx::Property("Asset Registry", assetRegistryPath))
		{
			config.AssetRegistryPath = assetRegistryPath;
			m_Dirty = true;
		}

		std::string audioCommandsPath = config.AudioCommandsRegistryPath.generic_string();
		if (ImGuiEx::Property("Audio Commands Registry", audioCommandsPath))
		{
			config.AudioCommandsRegistryPath = audioCommandsPath;
			m_Dirty = true;
		}

		std::string meshPath = config.MeshPath.generic_string();
		if (ImGuiEx::Property("Mesh Path", meshPath))
		{
			config.MeshPath = meshPath;
			m_Dirty = true;
		}

		std::string meshSourcePath = config.MeshSourcePath.generic_string();
		if (ImGuiEx::Property("Mesh Source Path", meshSourcePath))
		{
			config.MeshSourcePath = meshSourcePath;
			m_Dirty = true;
		}

		std::string animationPath = config.AnimationPath.generic_string();
		if (ImGuiEx::Property("Animation Path", animationPath))
		{
			config.AnimationPath = animationPath;
			m_Dirty = true;
		}

		if (ImGuiEx::Property("Auto Save", config.EnableAutoSave))
			m_Dirty = true;

		int32_t autoSaveInterval = config.AutoSaveIntervalSeconds;
		if (ImGuiEx::Property("Auto Save Interval", autoSaveInterval, 0, 86400))
		{
			config.AutoSaveIntervalSeconds = autoSaveInterval;
			m_Dirty = true;
		}

		AssetHandle startupScene = config.StartSceneHandle;
		ImGuiEx::PropertyAssetReferenceSettings startupSceneSettings;
		startupSceneSettings.ShowFullFilePath = true;
		if (ImGuiEx::PropertyAssetReference<Scene>("Startup Scene", startupScene, "Scene loaded when entering play mode and when exporting a runtime build.", nullptr, startupSceneSettings))
		{
			m_DefaultScene = startupScene;
			syncStartupScenePath(startupScene);
			m_Dirty = true;
		}
		ImGuiEx::EndPropertyGrid();

		ImGui::TextDisabled("Path changes affect the active project immediately and are persisted on save.");

		ImGui::TreePop();
	}

	void ProjectSettingsWindow::RenderRuntimeExportSettings()
	{
		if (!ImGuiEx::PropertyGridHeader("Runtime Export", false))
			return;

		auto& config = m_Project->GetConfig();
		auto& runtime = config.RuntimeExport;

		auto syncRuntimeIconPath = [&runtime](AssetHandle iconHandle)
		{
			runtime.IconHandle = iconHandle;
			runtime.IconPath.clear();

			if (!iconHandle)
				return;

			if (Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager())
			{
				const AssetMetadata metadata = editorAssetManager->GetMetadata(iconHandle);
				if (metadata.IsValid())
					runtime.IconPath = metadata.FilePath.generic_string();
			}
		};

		ImGuiEx::BeginPropertyGrid();
		if (ImGuiEx::Property("Game Name", m_RuntimeGameNameBuffer, sizeof(m_RuntimeGameNameBuffer)))
		{
			runtime.GameName = m_RuntimeGameNameBuffer;
			m_Dirty = true;
		}

		int32_t width = (int32_t)runtime.WindowWidth;
		if (ImGuiEx::Property("Window Width", width, 320, 16384))
		{
			runtime.WindowWidth = (uint32_t)std::max(width, 320);
			m_Dirty = true;
		}

		int32_t height = (int32_t)runtime.WindowHeight;
		if (ImGuiEx::Property("Window Height", height, 240, 16384))
		{
			runtime.WindowHeight = (uint32_t)std::max(height, 240);
			m_Dirty = true;
		}

		if (ImGuiEx::Property("Fullscreen", runtime.Fullscreen))
			m_Dirty = true;
		if (ImGuiEx::Property("VSync", runtime.VSync))
			m_Dirty = true;

		AssetHandle icon = runtime.IconHandle;
		ImGuiEx::PropertyAssetReferenceSettings iconSettings;
		iconSettings.ShowFullFilePath = true;
		if (ImGuiEx::PropertyAssetReference<Texture2D>("Icon", icon, "Optional PNG/JPG window icon copied beside exported runtime resources.", nullptr, iconSettings))
		{
			m_RuntimeIcon = icon;
			syncRuntimeIconPath(icon);
			m_Dirty = true;
		}
		ImGuiEx::EndPropertyGrid();

		if (ImGui::BeginCombo("Target Config", RuntimeExportTargetToString(runtime.TargetConfig)))
		{
			for (RuntimeExportTarget target : s_RuntimeExportTargets)
			{
				const bool selected = runtime.TargetConfig == target;
				if (ImGui::Selectable(RuntimeExportTargetToString(target), selected))
				{
					runtime.TargetConfig = target;
					m_Dirty = true;
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Export Preflight");
		ImGui::Separator();

		auto drawStatus = [](const char* label, bool ok, const char* okText, const char* failText)
		{
			ImGui::TextColored(ok ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f) : ImVec4(0.95f, 0.55f, 0.35f, 1.0f), "%s: %s", label, ok ? okText : failText);
		};

		std::error_code ec;
		const std::filesystem::path runtimeExe = GetRuntimeExecutablePath(runtime.TargetConfig);
		const std::filesystem::path assetPack = Project::GetActiveAssetDirectory() / "AssetPack.lap";
		const std::filesystem::path resources = FindRepositoryRootFrom(m_Project->GetProjectDirectory()) / "Editor" / "Resources";
		const std::filesystem::path dotnet = FindRepositoryRootFrom(m_Project->GetProjectDirectory()) / "Editor" / "DotNet";
		const std::filesystem::path scriptModule = Project::GetActiveScriptModuleFilePath();
		const std::filesystem::path scriptProject = ResolveScriptProjectFile(m_Project);
		const bool scriptModuleExists = config.ScriptModulePath.empty() || FileExists(scriptModule);
		const bool scriptModuleStale = !config.ScriptModulePath.empty() && scriptModuleExists && IsScriptModuleOutdated(scriptModule, scriptProject);

		drawStatus("Startup Scene", config.StartSceneHandle != 0, "set", "missing");
		drawStatus("Lux-Runtime.exe", !runtimeExe.empty(), runtimeExe.empty() ? "" : runtimeExe.string().c_str(), "missing");
		drawStatus("AssetPack.lap", std::filesystem::exists(assetPack, ec), "created", "will be created during export");
		drawStatus("Resources", std::filesystem::exists(resources, ec), "found", "missing");
		if (config.ScriptModulePath.empty())
			ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "Script Module: optional");
		else if (!scriptModuleExists)
			ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f), "Script Module: missing");
		else
			ImGui::TextColored(scriptModuleStale ? ImVec4(0.95f, 0.75f, 0.35f, 1.0f) : ImVec4(0.35f, 0.85f, 0.45f, 1.0f),
				"Script Module: %s", scriptModuleStale ? "stale" : "found");
		drawStatus("DotNet", std::filesystem::exists(dotnet, ec), "found", "missing");

		if (ImGui::Button("Build Runtime"))
			BuildRuntimeExecutable(runtime.TargetConfig);

		ImGui::SameLine();
		if (ImGui::Button("Build Scripts"))
			BuildScriptModule(runtime.TargetConfig);

		ImGui::SameLine();
		ImGui::TextDisabled("Dist exports skip .pdb files.");

		ImGui::TreePop();
	}

	void ProjectSettingsWindow::RenderRendererSettings()
	{
		if (!ImGuiEx::PropertyGridHeader("Renderer", false))
			return;

		auto& projectConfig = m_Project->GetConfig();
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

		auto& audioSettings = m_Project->GetConfig().Audio;

		ImGuiEx::BeginPropertyGrid();
		double fileStreamingThreshold = audioSettings.FileStreamingDurationThreshold;
		if (ImGuiEx::Property("File Streaming Threshold", fileStreamingThreshold, 0.1f, 0.0, 600.0))
		{
			audioSettings.FileStreamingDurationThreshold = fileStreamingThreshold;
			m_Dirty = true;
		}
		ImGuiEx::EndPropertyGrid();

		ImGui::TextDisabled("Stored in both the YAML project file and the runtime project data.");

		ImGui::Spacing();
		ImGui::TextUnformatted("FMOD Studio");

		ImGuiEx::BeginPropertyGrid();

		std::string studioProjectPath = audioSettings.StudioProjectPath.generic_string();
		if (ImGuiEx::Property("Studio Project (.fspro)", studioProjectPath,
			"Path to the FMOD Studio project that authors this game's audio, relative to the asset directory. Leave empty if the project has no authored audio."))
		{
			audioSettings.StudioProjectPath = studioProjectPath;
			m_Dirty = true;
		}

		std::string bankOutputPath = audioSettings.StudioBankOutputPath.generic_string();
		if (ImGuiEx::Property("Bank Output", bankOutputPath,
			"Where FMOD writes built banks, relative to the .fspro's own directory. 'Desktop' is FMOD's default platform name."))
		{
			audioSettings.StudioBankOutputPath = bankOutputPath;
			m_Dirty = true;
		}

		bool rebuildOnPlay = audioSettings.RebuildBanksOnPlay;
		if (ImGuiEx::Property("Rebuild Banks On Play", rebuildOnPlay,
			"Rebuilds banks before entering Play when the Studio project has changed since they were last built. A timestamp check, so it costs nothing when nothing changed."))
		{
			audioSettings.RebuildBanksOnPlay = rebuildOnPlay;
			m_Dirty = true;
		}

		if (ImGuiEx::PropertyAssetReference<AudioSurfaceTable>("Surface Table", audioSettings.SurfaceTable,
			"Shared footstep, impact, scrape and roll events. Create a table in Content Browser > New. Loaded on Play and included in runtime exports."))
			m_Dirty = true;

		static const char* zoneModes[] = { "Layer zones and VA", "Prefer zones", "Prefer VA" };
		if (ImGuiEx::PropertyDropdown("Zone Reverb", zoneModes, 3, audioSettings.ZoneReverbMode,
			"Layered keeps both. Prefer zones reduces VA ReverbSend by active snapshot coverage. Prefer VA suppresses zone snapshots while valid VA ambience is available.", false))
			m_Dirty = true;

		bool liveUpdate = audioSettings.EnableLiveUpdate;
		if (ImGuiEx::Property("Live Update", liveUpdate,
			"Lets the FMOD Studio application connect to the running editor and mix in real time. Takes effect the next time the project is opened, since the audio engine is initialised then."))
		{
			audioSettings.EnableLiveUpdate = liveUpdate;
			m_Dirty = true;
		}

		ImGuiEx::EndPropertyGrid();

		ImGui::Spacing();
		if (audioSettings.SurfaceTable && ImGuiEx::PropertyGridHeader("Surface Sounds", false))
		{
			if (AssetManager::IsAssetHandleValid(audioSettings.SurfaceTable) && AssetManager::GetAssetType(audioSettings.SurfaceTable) == AssetType::AudioSurfaceTable)
			{
				if (auto table = AssetManager::GetAsset<AudioSurfaceTable>(audioSettings.SurfaceTable))
				{
					ImGui::TextWrapped("Edit the shared table, then Save Surface Table. Empty material slots use Default. Footsteps and impacts require one-shot events; scrape and roll require continuous events.");
					ImGuiEx::BeginPropertyGrid();
					ImGuiEx::Property("Impact Cooldown (s)", table->ImpactCooldown, 0.01f, 0.0f, 60.0f, "", false);
					ImGuiEx::Property("Minimum Impulse", table->MinimumImpulse, 0.1f, 0.0f, 100000.0f, "Jolt estimated collision impulse in kg m/s.", false);
					ImGuiEx::Property("Minimum Motion Speed", table->MinimumMotionSpeed, 0.01f, 0.0f, 1000.0f, "", false);
					ImGuiEx::EndPropertyGrid();
					for (size_t i = 0; i < AcousticMaterialCount; ++i)
					{
						ImGuiEx::ScopedID id(static_cast<int>(i));
						if (!ImGui::TreeNode(AcousticMaterialNames[i]))
							continue;
						auto& sounds = table->Surfaces[i];
						ImGuiEx::SurfaceEventPicker("Footstep", sounds.Footstep, true);
						ImGuiEx::SurfaceEventPicker("Impact", sounds.Impact, true);
						ImGuiEx::SurfaceEventPicker("Scrape", sounds.Scrape, false);
						ImGuiEx::SurfaceEventPicker("Roll", sounds.Roll, false);
						ImGui::TreePop();
					}
					if (ImGui::Button("Save Surface Table"))
						AssetManager::SaveAsset(table);
				}
			}
			else
				ImGui::TextWrapped("The assigned surface table is missing or has the wrong asset type.");
		}
		if (ImGuiEx::PropertyGridHeader("Acoustic Materials", false))
		{
			ImGui::TextWrapped("Applied on the next Play session and included in runtime exports. Presets are starting points; tune them for your game's scale.");
			for (size_t i = 0; i < AcousticMaterialCount; ++i)
			{
				ImGuiEx::ScopedID id(static_cast<int>(i));
				if (!ImGui::TreeNode(AcousticMaterialNames[i]))
					continue;
				const auto material = static_cast<AcousticMaterial>(i);
				ImGui::Text("VA base preset: %s", AcousticMaterialPresetName(material));
				auto& entry = audioSettings.AcousticMaterials.Overrides[i];
				ImGuiEx::BeginPropertyGrid();
				if (ImGuiEx::Property("Override Preset", entry.Enabled))
				{
					if (entry.Enabled)
						entry.Properties = GetDefaultAcousticMaterialProperties(material);
					m_Dirty = true;
				}
				auto properties = entry.Enabled ? entry.Properties : GetDefaultAcousticMaterialProperties(material);
				bool propertiesChanged = false;
				{
					ImGuiEx::ScopedDisable disabled(!entry.Enabled);
					propertiesChanged |= ImGuiEx::Property("Absorption LF", properties.AbsorptionLF, 0.01f, 0.0f, 1.0f, "", false);
					propertiesChanged |= ImGuiEx::Property("Absorption HF", properties.AbsorptionHF, 0.01f, 0.0f, 1.0f, "", false);
					propertiesChanged |= ImGuiEx::Property("Scattering", properties.Scattering, 0.01f, 0.0f, 1.0f, "", false);
					propertiesChanged |= ImGuiEx::Property("Transmission LF (m)", properties.TransmissionLF, 0.01f, 0.001f, 10000.0f, "", false);
					propertiesChanged |= ImGuiEx::Property("Transmission HF (m)", properties.TransmissionHF, 0.01f, 0.001f, 10000.0f, "", false);
					propertiesChanged |= ImGuiEx::Property("Thin Surface Loss LF", properties.FlatTransmissionLF, 0.01f, 0.0f, 10000.0f, "", false);
					propertiesChanged |= ImGuiEx::Property("Thin Surface Loss HF", properties.FlatTransmissionHF, 0.01f, 0.0f, 10000.0f, "", false);
				}
				if (entry.Enabled && propertiesChanged)
				{
					if (properties.IsValid())
					{
						entry.Properties = properties;
						m_Dirty = true;
					}
					else
						LUX_CORE_ERROR_TAG("Audio", "Rejected invalid acoustic properties for {0}; enter finite values within the displayed ranges", AcousticMaterialNames[i]);
				}
				ImGuiEx::EndPropertyGrid();
				ImGui::TreePop();
			}
			ImGui::TreePop();
		}

		RenderAudioBankStatus();

		ImGui::TreePop();
	}

	// Live state rather than settings: whether the tooling was found, what is loaded right now, and
	// a way to rebuild without entering Play.
	void ProjectSettingsWindow::RenderAudioBankStatus()
	{
		const std::filesystem::path studioProject = m_Project->GetStudioProjectPath();
		if (studioProject.empty())
		{
			ImGui::TextDisabled("No FMOD Studio project configured.");
			return;
		}

		std::error_code ec;
		const bool projectExists = std::filesystem::exists(studioProject, ec);
		if (!projectExists)
		{
			ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.31f, 1.0f), "Not found: %s", studioProject.string().c_str());
			return;
		}

		const std::vector<AudioBankInfo>& banks = AudioEngine::GetLoadedBanks();
		const std::vector<AudioEventInfo>& events = AudioEngine::GetEvents();
		ImGui::Text("%zu bank(s) loaded, %zu event(s)", banks.size(), events.size());

		const bool builderAvailable = AudioBankBuilder::IsAvailable();
		if (!builderAvailable)
		{
			ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.31f, 1.0f),
				"fmodstudiocl not found - set LUX_FMOD_STUDIO_CL to build banks from the editor.");
		}

		{
			ImGuiEx::ScopedDisable disabled(!builderAvailable);
			if (ImGui::Button("Build Banks Now"))
			{
				if (AudioBankBuilder::Build(studioProject))
					AudioEngine::LoadBanks(m_Project->GetStudioBankDirectory());
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Open In FMOD Studio"))
			AudioBankBuilder::OpenInStudio(studioProject);
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

		if (ImGuiEx::Property("Default Namespace", m_DefaultNamespaceBuffer, sizeof(m_DefaultNamespaceBuffer)))
		{
			config.DefaultNamespace = m_DefaultNamespaceBuffer;
			m_Dirty = true;
		}

		if (ImGuiEx::Property("Automatically Reload Assembly", config.AutomaticallyReloadAssembly))
			m_Dirty = true;
		ImGuiEx::EndPropertyGrid();

		if (!config.ScriptModulePath.empty())
		{
			const std::filesystem::path resolvedModulePath = Project::GetActiveAssetDirectory() / config.ScriptModulePath;
			ImGui::TextDisabled("Resolved Path: %s", resolvedModulePath.generic_string().c_str());
		}

		if (ImGui::Button("Reload Assembly"))
			Project::GetActive()->ReloadScriptEngine();

		ImGui::TreePop();
	}

	void ProjectSettingsWindow::RenderPhysicsSettings()
	{
		if (!ImGuiEx::PropertyGridHeader("Physics", false))
			return;

		auto& physicsSettings = m_Project->GetConfig().Physics;

		ImGuiEx::BeginPropertyGrid();
		if (ImGuiEx::Property("Fixed Timestep", physicsSettings.FixedTimestep, 0.001f, 0.001f, 1.0f))
			m_Dirty = true;
		if (ImGuiEx::Property("Gravity", physicsSettings.Gravity, 0.1f, -1000.0f, 1000.0f))
			m_Dirty = true;
		if (ImGuiEx::Property("Solver Position Iterations", physicsSettings.PositionSolverIterations, 0, 1024))
			m_Dirty = true;
		if (ImGuiEx::Property("Solver Velocity Iterations", physicsSettings.VelocitySolverIterations, 0, 1024))
			m_Dirty = true;
		if (ImGuiEx::Property("Max Bodies", physicsSettings.MaxBodies, 0, 1000000))
			m_Dirty = true;
		if (ImGuiEx::Property("Capture On Play", physicsSettings.CaptureOnPlay))
			m_Dirty = true;
		ImGuiEx::EndPropertyGrid();

		int captureMethod = (int)physicsSettings.CaptureMethod;
		const char* captureMethodLabel = captureMethod == (int)PhysicsCaptureMethod::CaptureToFile ? "Capture To File" : "Live Debug";
		if (ImGui::BeginCombo("Capture Method", captureMethodLabel))
		{
			for (const auto& [label, value] : std::initializer_list<std::pair<const char*, PhysicsCaptureMethod>>{
				{ "Live Debug", PhysicsCaptureMethod::LiveDebug },
				{ "Capture To File", PhysicsCaptureMethod::CaptureToFile }
				})
			{
				const bool selected = physicsSettings.CaptureMethod == value;
				if (ImGui::Selectable(label, selected))
				{
					physicsSettings.CaptureMethod = value;
					m_Dirty = true;
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Physics Layers");
		ImGui::Separator();

		if (ImGui::Button("Add Physics Layer"))
		{
			ProjectPhysicsLayer layer;
			layer.Name = "Layer " + std::to_string(physicsSettings.Layers.size() + 1);
			physicsSettings.Layers.emplace_back(std::move(layer));
			m_Dirty = true;
		}

		int removeLayerIndex = -1;
		for (size_t i = 0; i < physicsSettings.Layers.size(); i++)
		{
			auto& layer = physicsSettings.Layers[i];
			const std::string header = layer.Name.empty() ? ("Layer " + std::to_string(i + 1)) : layer.Name;

			ImGui::PushID((int)i);
			if (ImGui::TreeNodeEx("##physics_layer", ImGuiTreeNodeFlags_DefaultOpen, "%s", header.c_str()))
			{
				ImGuiEx::BeginPropertyGrid();
				std::string layerName = layer.Name;
				if (ImGuiEx::Property("Name", layerName))
				{
					layer.Name = layerName;
					m_Dirty = true;
				}

				if (ImGuiEx::Property("Collides With Self", layer.CollidesWithSelf))
					m_Dirty = true;

				std::string collidesWith = JoinLayerNames(layer.CollidesWith);
				if (ImGuiEx::Property("Collides With", collidesWith))
				{
					layer.CollidesWith = SplitLayerNames(collidesWith);
					m_Dirty = true;
				}
				ImGuiEx::EndPropertyGrid();

				if (ImGui::Button("Remove Layer"))
					removeLayerIndex = (int)i;

				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		if (removeLayerIndex >= 0)
		{
			physicsSettings.Layers.erase(physicsSettings.Layers.begin() + removeLayerIndex);
			m_Dirty = true;
		}

		ImGui::TreePop();
	}

	void ProjectSettingsWindow::RenderLogSettings()
	{
		if (!ImGuiEx::PropertyGridHeader("Log", false))
			return;

		if (ImGui::Button("Reset Tag Filters"))
		{
			Log::SetDefaultTagSettings();
			m_Dirty = true;
		}

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
				if (ImGui::Checkbox("##enabled", &tagDetails.Enabled))
					m_Dirty = true;

				ImGui::TableSetColumnIndex(2);
				const char* currentLevel = Log::LevelToString(tagDetails.LevelFilter);
				if (ImGui::BeginCombo("##level", currentLevel))
				{
					for (Log::Level level : { Log::Level::Trace, Log::Level::Info, Log::Level::Warn, Log::Level::Error, Log::Level::Fatal })
					{
						const bool selected = (tagDetails.LevelFilter == level);
						if (ImGui::Selectable(Log::LevelToString(level), selected))
						{
							tagDetails.LevelFilter = level;
							m_Dirty = true;
						}
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
