// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ProjectSettingsWindow.h"

#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioSurfaceTable.h"
#include "Lux/Audio/Sound.h"
#include "Lux/Serialization/Yaml.h"
#include <soundwave/Settings.h>
#include "Lux/ImGui/AudioWidgets.h"
#include "Lux/ImGui/AudioAccessibilityWidgets.h"
#include "RuntimeExportUtils.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Asset/AssetManager/EditorAssetManager.h"
#include "Lux/Editor/EditorStack.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Scene.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Project/ProjectSerializer.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Scripting/ScriptEngine.h"
#include "Lux/Scripting/ScriptBuilder.h"
#include "Lux/Utilities/FileDialogs.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <charconv>
#include <array>
#include <set>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace Lux {

	namespace
	{
		constexpr const char* s_ProjectPages[] = {"General",   "Runtime Export", "Renderer", "Audio",
												  "Scripting", "Physics",		 "Log"};
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


		enum class AcousticControl : size_t
		{
			Strength, Rate, Sources, Bounces, Depth, Openings, Absorption, Candidates, Paths, Nodes, Compare,
			ReverbRays, ReverbBounces, ReverbReference, AirLF, AirHF, Count
		};
		using AcousticControlIDs = std::array<ImGuiID, static_cast<size_t>(AcousticControl::Count)>;

		bool DrawAcousticControls(AudioOcclusionSettings& settings, AcousticControlIDs* ids = nullptr)
		{
			const ImGuiEx::ScopedID scope("SoundwaveSettings");
			const auto before = settings;
			bool changed = false;
			const auto record = [&](AcousticControl control, bool edited)
			{
				changed |= edited;
				if (ids)
					(*ids)[static_cast<size_t>(control)] = ImGui::GetItemID();
			};
			ImGui::TextUnformatted("Soundwave acoustics");
			ImGui::TextWrapped("Applies on the next Play session and in runtime builds.");
			ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
			record(AcousticControl::Strength, ImGuiEx::Property("Transmission Strength", settings.Strength, 0.05f, 0.0f, 4.0f,
				"Scales authored wall loss in decibels. 1 uses each material's LF/HF transmission.", false));
			record(AcousticControl::Rate, ImGuiEx::Property("Acoustic Rate (Hz)", settings.UpdateRateHz, 1.0f, 1.0f, 120.0f,
				"How often each source is solved. Gains interpolate between completed updates.", false));
			record(AcousticControl::Sources, ImGuiEx::Property("Sources per Frame", settings.CastBudget, 1u, 1024u,
				"Maximum source solves in a frame. Remaining sources take turns in later frames.", false));
			record(AcousticControl::Bounces, ImGuiEx::Property("Maximum Bounces", settings.MaxBounces, 1u, Soundwave::kMaximumInteractions,
				"Maximum reflection or opening interactions along a candidate path. Lowering this also limits reflection depth.", false));
			settings.MaxBounces = std::clamp(settings.MaxBounces, 1u, Soundwave::kMaximumInteractions);
			settings.ReflectionDepth = std::min(settings.ReflectionDepth, settings.MaxBounces);
			record(AcousticControl::Depth, ImGuiEx::Property("Reflection Depth", settings.ReflectionDepth, 0u, settings.MaxBounces,
				"Specular reflection depth, up to Maximum Bounces. Zero disables reflections.", false));
			record(AcousticControl::Openings, ImGuiEx::Property("Opening Samples", settings.OpeningGridSize, 1u, 32u,
				"Samples along boundary and crease edges. 1 with depth 0 and bounces 1 selects the direct-only reference.", false));
			record(AcousticControl::Absorption, ImGuiEx::Property("Reflection Absorption", settings.Absorption, 0.05f, 0.0f, 1.0f,
				"Fraction of authored material absorption applied at each reflection. Does not add distance attenuation to the direct signal.", false));
			record(AcousticControl::Candidates, ImGuiEx::Property("Candidate Work Budget", settings.CandidateBudget, 1u, 65536u,
				"Bounded indirect search work per source update. Exhaustion is reported in path diagnostics.", false));
			record(AcousticControl::Paths, ImGuiEx::Property("Stored Path Budget", settings.PathBudget, 1u, 4096u,
				"Maximum stored indirect paths per source. The strongest audible opening is retained separately.", false));
			record(AcousticControl::Nodes, ImGuiEx::Property("Opening Node Budget", settings.OpeningNodeBudget, 1u, 2048u,
				"Maximum opening graph nodes per source update. Exhaustion is reported.", false));
			record(AcousticControl::Compare, ImGuiEx::Property("Compare Engine Ray", settings.CompareRaycast,
				"Also run the existing physics-ray occlusion as a diagnostic oracle. Soundwave continues to drive playback.", false));
			record(AcousticControl::ReverbRays, ImGuiEx::Property("Reverb Rays", settings.ReverbRays, 16u, 4096u,
				"Rays bounced around the listener to measure the space. More rays give a steadier reverb send.", false));
			record(AcousticControl::ReverbBounces, ImGuiEx::Property("Reverb Bounces", settings.ReverbBounces, 1u, 64u,
				"Surface hits followed per reverb ray before it stops.", false));
			record(AcousticControl::ReverbReference, ImGuiEx::Property("Reverb Reference Decay (s)", settings.ReverbReferenceSeconds, 0.05f, 0.1f, 10.0f,
				"A space that takes this long to fall 60 dB gets a 63% reverb send. Lower values make rooms wetter.", false));
			record(AcousticControl::AirLF, ImGuiEx::Property("Air Absorption LF (dB/100 m)", settings.AirAbsorptionLF, 0.05f, 0.0f, 20.0f,
				"Low-frequency loss in air on reflected and doorway routes beyond the direct path, and on the reverb. The Sound's distance attenuation still owns the direct sound.", false));
			record(AcousticControl::AirHF, ImGuiEx::Property("Air Absorption HF (dB/100 m)", settings.AirAbsorptionHF, 0.05f, 0.0f, 20.0f,
				"High-frequency loss in air on reflected and doorway routes beyond the direct path, and on the reverb. 2.5 is about 20 C at 50% humidity.", false));
			ImGuiEx::EndPropertyGrid();
			if (!changed)
				return false;
			// Temporary numeric input can bypass a drag widget's range. Keep authored data valid.
			settings.Strength = std::clamp(settings.Strength, 0.0f, 4.0f);
			settings.UpdateRateHz = std::clamp(settings.UpdateRateHz, 1.0f, 120.0f);
			settings.CastBudget = std::clamp(settings.CastBudget, 1u, 1024u);
			settings.ReflectionDepth = std::min(settings.ReflectionDepth, settings.MaxBounces);
			settings.OpeningGridSize = std::clamp(settings.OpeningGridSize, 1u, 32u);
			settings.Absorption = std::clamp(settings.Absorption, 0.0f, 1.0f);
			settings.CandidateBudget = std::clamp(settings.CandidateBudget, 1u, 65536u);
			settings.PathBudget = std::clamp(settings.PathBudget, 1u, 4096u);
			settings.OpeningNodeBudget = std::clamp(settings.OpeningNodeBudget, 1u, 2048u);
			settings.ReverbRays = std::clamp(settings.ReverbRays, 16u, 4096u);
			settings.ReverbBounces = std::clamp(settings.ReverbBounces, 1u, 64u);
			settings.ReverbReferenceSeconds = std::clamp(settings.ReverbReferenceSeconds, 0.1f, 10.0f);
			settings.AirAbsorptionLF = std::clamp(settings.AirAbsorptionLF, 0.0f, 20.0f);
			settings.AirAbsorptionHF = std::clamp(settings.AirAbsorptionHF, 0.0f, 20.0f);
			if (!settings.Validate())
			{
				settings = before;
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::textError), "Acoustic values must be finite and within the displayed limits.");
				LUX_CORE_WARN_TAG("Audio", "Rejected invalid acoustic settings input; previous settings preserved");
				return false;
			}
			return true;
		}

		using RuntimeExport::FileExists;
		using RuntimeExport::FindRepositoryRootFrom;
		using RuntimeExport::GetRuntimeExecutablePath;
		using RuntimeExport::ResolveScriptProjectFile;
		using RuntimeExport::IsScriptModuleOutdated;

		constexpr RuntimeExportTarget s_RuntimeExportTargets[] = {
			RuntimeExportTarget::Debug,
			RuntimeExportTarget::Release,
			RuntimeExportTarget::Dist
		};
	}

	bool ProjectSettingsWindow::RunAcousticSettingsSelfTests(std::vector<std::string>& failures)
	{
		ImGuiContext* previous = ImGui::GetCurrentContext();
		struct ContextGuard
		{
			ImGuiContext* Previous;
			ImGuiContext* Test;
			~ContextGuard()
			{
				ImGui::DestroyContext(Test);
				ImGui::SetCurrentContext(Previous);
			}
		} guard{previous,ImGui::CreateContext()};
		ImGui::SetCurrentContext(guard.Test);
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.LogFilename = nullptr;
		io.DisplaySize = {1200,1000};
		io.DeltaTime = 1.0f/60.0f;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigInputTrickleEventQueue = false;
		unsigned char* pixels = nullptr;
		int width = 0, height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
		const auto check = [&](bool condition, const std::string& message)
		{
			if (!condition)
				failures.push_back(message);
		};
		check(pixels && width > 0 && height > 0, "Audio settings test font atlas failed");
		AudioOcclusionSettings settings;
		AcousticControlIDs ids{};
		uint32_t edits = 0;
		const auto frame = [&]()
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos({0,0});
			ImGui::SetNextWindowSize({1100,950});
			ImGui::SetNextWindowFocus();
			ImGui::Begin("Audio settings self-test",nullptr,ImGuiWindowFlags_NoSavedSettings);
			if (DrawAcousticControls(settings,&ids))
				++edits;
			std::set<ImGuiID> unique(ids.begin(),ids.end());
			check(unique.size() == ids.size() && !unique.contains(0), "Audio control IDs collide or are missing");
			ImGui::End();
			ImGui::Render();
			check(guard.Test->ErrorCountCurrentFrame == 0 && guard.Test->DebugDrawIdConflictsCount == 0,
				"ImGui reported an audio settings scope or ID conflict");
		};
		frame();
		frame();
		const auto edit = [&](AcousticControl control, const char* text)
		{
			const auto id = ids[static_cast<size_t>(control)];
			const auto previousEdits = edits;
			ImGui::ActivateItemByID(id);
			guard.Test->NavNextActivateFlags |= ImGuiActivateFlags_PreferInput;
			frame();
			check(guard.Test->ActiveId == id && guard.Test->TempInputId == id, "Could not activate acoustic numeric text input");
			io.AddKeyEvent(ImGuiMod_Ctrl,true);
			io.AddKeyEvent(ImGuiKey_A,true);
			frame();
			io.AddKeyEvent(ImGuiKey_A,false);
			io.AddKeyEvent(ImGuiMod_Ctrl,false);
			io.AddInputCharactersUTF8(text);
			frame();
			io.AddKeyEvent(ImGuiKey_Enter,true);
			frame();
			io.AddKeyEvent(ImGuiKey_Enter,false);
			frame();
			check(edits > previousEdits, "Audio numeric edit did not mark project settings dirty");
			check(settings.Validate(), "Audio numeric edit left invalid authored settings");
		};
		edit(AcousticControl::Strength,"0.5");
		edit(AcousticControl::Rate,"73");
		edit(AcousticControl::Sources,"7");
		edit(AcousticControl::Bounces,"3");
		edit(AcousticControl::Depth,"3");
		edit(AcousticControl::Openings,"12");
		edit(AcousticControl::Absorption,"0.25");
		edit(AcousticControl::Candidates,"4321");
		edit(AcousticControl::Paths,"11");
		edit(AcousticControl::Nodes,"111");
		edit(AcousticControl::ReverbRays,"512");
		edit(AcousticControl::ReverbBounces,"9");
		edit(AcousticControl::ReverbReference,"2.5");
		edit(AcousticControl::AirLF,"0.4");
		edit(AcousticControl::AirHF,"6");
		ImGui::ActivateItemByID(ids[static_cast<size_t>(AcousticControl::Compare)]);
		frame();
		check(settings.CompareRaycast && settings.Strength == 0.5f && settings.UpdateRateHz == 73 && settings.CastBudget == 7 &&
			settings.MaxBounces == 3 && settings.ReflectionDepth == 3 && settings.OpeningGridSize == 12 && settings.Absorption == 0.25f &&
			settings.CandidateBudget == 4321 && settings.PathBudget == 11 && settings.OpeningNodeBudget == 111 &&
			settings.ReverbRays == 512 && settings.ReverbBounces == 9 && settings.ReverbReferenceSeconds == 2.5f &&
			settings.AirAbsorptionLF == 0.4f && settings.AirAbsorptionHF == 6.0f,
			"Audio controls did not apply their typed values or comparison toggle");
		Yaml::Writer out;
		settings.SerializeYAML(out);
		AudioOcclusionSettings restored;
		check(restored.DeserializeYAML(Yaml::Load(out.c_str())) && restored.Strength == 0.5f && restored.UpdateRateHz == 73 &&
			restored.CastBudget == 7 && restored.MaxBounces == 3 && restored.ReflectionDepth == 3 && restored.OpeningGridSize == 12 &&
			restored.Absorption == 0.25f && restored.CandidateBudget == 4321 && restored.PathBudget == 11 &&
			restored.OpeningNodeBudget == 111 && restored.CompareRaycast && restored.ReverbRays == 512 && restored.ReverbBounces == 9 &&
			restored.ReverbReferenceSeconds == 2.5f && restored.AirAbsorptionLF == 0.4f && restored.AirAbsorptionHF == 6.0f, "Edited acoustic controls did not survive serialization");
		edit(AcousticControl::Bounces,"1");
		check(settings.MaxBounces == 1 && settings.ReflectionDepth == 1, "Lowering bounces did not limit reflection depth");
		edit(AcousticControl::Depth,"0");
		edit(AcousticControl::Openings,"1");
		check(settings.ReflectionDepth == 0 && settings.OpeningGridSize == 1 && settings.MaxBounces == 1,
			"Direct-only comparison profile is unreachable through the controls");
		ImGui::ActivateItemByID(ids[static_cast<size_t>(AcousticControl::Compare)]);
		frame();
		check(!settings.CompareRaycast, "Comparison toggle could not be turned off");
		edit(AcousticControl::Bounces,"999");
		edit(AcousticControl::Openings,"999");
		edit(AcousticControl::Candidates,"999999");
		edit(AcousticControl::Paths,"999999");
		edit(AcousticControl::Nodes,"999999");
		edit(AcousticControl::ReverbRays,"999999");
		edit(AcousticControl::ReverbBounces,"999");
		edit(AcousticControl::ReverbReference,"999");
		edit(AcousticControl::AirLF,"999");
		edit(AcousticControl::AirHF,"999");
		check(settings.MaxBounces == Soundwave::kMaximumInteractions && settings.OpeningGridSize == 32 &&
			settings.CandidateBudget == 65536 && settings.PathBudget == 4096 && settings.OpeningNodeBudget == 2048 &&
			settings.ReverbRays == 4096 && settings.ReverbBounces == 64 && settings.ReverbReferenceSeconds == 10 &&
			settings.AirAbsorptionLF == 20 && settings.AirAbsorptionHF == 20,
			"Typed work limits bypassed kernel bounds");
		edit(AcousticControl::Sources,"0");
		edit(AcousticControl::Rate,"999");
		edit(AcousticControl::Strength,"999");
		edit(AcousticControl::Absorption,"999");
		check(settings.CastBudget == 1 && settings.UpdateRateHz == 120 && settings.Strength == 4 && settings.Absorption == 1,
			"Typed scalar/source limits bypassed validation");
		return failures.empty();
	}

	ProjectSettingsWindow::ProjectSettingsWindow(RuntimeExport::RuntimeBuildJob& runtimeBuildJob)
		: m_RuntimeBuildJob(runtimeBuildJob)
	{
	}

	void ProjectSettingsWindow::OnImGuiRender(bool& isOpen)
	{
		m_IsFocused = false;
		if (!m_Project)
		{
			isOpen = false;
			return;
		}

		if (ImGui::Begin("Project Settings", &isOpen))
		{
			m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			const ImGuiEx::PanelAction actions[] = {
				{0, "Save project settings", LUX_ICON_FLOPPY_O, nullptr, true, false, true}};
			if (ImGuiEx::PanelActionBar("##project_settings_actions", actions) == 0)
				SaveProject();
			if (m_Dirty)
			{
				ImGuiEx::ScopedColour dirty(ImGuiCol_Text, Colors::Theme::textWarning);
				ImGui::TextWrapped("Unsaved project changes");
			}
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::BeginCombo("##project_settings_page", s_ProjectPages[m_CurrentPage]))
			{
				for (uint32_t page = 0; page < std::size(s_ProjectPages); ++page)
				{
					const bool selected = m_CurrentPage == page;
					if (ImGui::Selectable(s_ProjectPages[page], selected))
					{
						if (m_CurrentPage != page)
							StopDialoguePreview();
						m_CurrentPage = page;
						m_OpenPage = true;
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (ImGui::BeginChild("##project_settings_content"))
			{
				if (m_OpenPage)
				{
					ImGui::SetScrollY(0.0f);
					ImGui::SetNextItemOpen(true);
					m_OpenPage = false;
				}
				switch (m_CurrentPage)
				{
				case 0:
					RenderGeneralSettings();
					break;
				case 1:
					RenderRuntimeExportSettings();
					break;
				case 2:
					RenderRendererSettings();
					break;
				case 3:
					RenderAudioSettings();
					break;
				case 4:
					RenderScriptingSettings();
					break;
				case 5:
					RenderPhysicsSettings();
					break;
				case 6:
					RenderLogSettings();
					break;
				}
			}
			ImGui::EndChild();

			if (!isOpen)
				OnClose();
		}

		ImGui::End();
	}

	void ProjectSettingsWindow::SaveState(Yaml::Writer& out) const
	{
		out << Yaml::Key << "Page" << Yaml::Value << s_ProjectPages[m_CurrentPage];
	}

	void ProjectSettingsWindow::LoadState(const Yaml::Node& state)
	{
		const std::string page = state["Page"].as<std::string>("");
		for (uint32_t index = 0; index < std::size(s_ProjectPages); ++index)
			if (page == s_ProjectPages[index])
			{
				if (m_CurrentPage != index)
					StopDialoguePreview();
				m_CurrentPage = index;
				m_OpenPage = true;
			}
	}

	void ProjectSettingsWindow::OnProjectChanged(const Ref<Project>& project)
	{
		StopDialoguePreview();
		m_LegacyScan.reset();
		m_LegacyErrors.clear();
		m_IsFocused = false;
		m_Project = project;
		m_Dirty = false;
		m_OpenPage = true;

		if (!m_Project)
		{
			m_DefaultScene = 0;
			m_RuntimeIcon = 0;
			m_NameBuffer[0] = '\0';
			m_RuntimeGameNameBuffer[0] = '\0';
			m_ScriptModulePathBuffer[0] = '\0';
			m_DefaultNamespaceBuffer[0] = '\0';
			return;
		}

		m_DefaultScene = m_Project->GetConfig().StartSceneHandle;
		m_RuntimeIcon = m_Project->GetConfig().RuntimeExport.IconHandle;
		SyncBuffersFromProject();
	}

	void ProjectSettingsWindow::OnClose()
	{
		m_IsFocused = false;
		StopDialoguePreview();
		if (m_Dirty)
			SaveProject();
	}

	void ProjectSettingsWindow::OnEvent(Event& event)
	{
		if (!m_IsFocused)
			return;
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>(
			[](KeyPressedEvent&) { return ImGui::GetIO().WantTextInput || GImGui->OpenPopupStack.Size > 0; });
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

		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
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

		{
			ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
			ImGui::TextWrapped(
				"Path changes affect the active project immediately and are persisted on save.");
		}

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

		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
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

		ImGui::TextUnformatted("Target configuration");
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##runtime_target", RuntimeExportTargetToString(runtime.TargetConfig)))
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
			ImGuiEx::ScopedColour color(ImGuiCol_Text, ok ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f)
														  : ImVec4(0.95f, 0.55f, 0.35f, 1.0f));
			ImGui::TextWrapped("%s: %s", label, ok ? okText : failText);
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

		// The build runs on EditorLayer's worker; the export window shares it, so both disable together.
		const bool building = m_RuntimeBuildJob.IsRunning();
		const ImGuiEx::PanelAction buildActions[] = {{0, "Build runtime", LUX_ICON_COG, nullptr, !building},
													 {1, "Build scripts", LUX_ICON_CODE, nullptr, !building}};
		switch (ImGuiEx::PanelActionBar("##project_build_actions", buildActions))
		{
		case 0:
			m_RuntimeBuildJob.Start(runtime.TargetConfig, true, false);
			break;
		case 1:
			m_RuntimeBuildJob.Start(runtime.TargetConfig, false, true);
			break;
		default:
			break;
		}
		{
			ImGuiEx::ScopedColour caption(ImGuiCol_Text, Colors::Theme::textDarker);
			ImGui::TextWrapped("Dist exports skip .pdb files.");
		}

		if (building)
		{
			ImGui::TextUnformatted(m_RuntimeBuildJob.GetStatusText().c_str());
		}
		else if (!m_RuntimeBuildJob.GetLastError().empty())
		{
			ImGuiEx::ScopedColour errorText(ImGuiCol_Text, Colors::Theme::textError);
			ImGui::TextWrapped("%s", m_RuntimeBuildJob.GetLastError().c_str());
		}

		ImGui::TreePop();
	}

	void ProjectSettingsWindow::RenderRendererSettings()
	{
		if (!ImGuiEx::PropertyGridHeader("Renderer", false))
			return;

		auto& projectConfig = m_Project->GetConfig();
		auto& rendererConfig = Renderer::GetConfig();

		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
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

		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
		double fileStreamingThreshold = audioSettings.FileStreamingDurationThreshold;
		if (ImGuiEx::Property("File Streaming Threshold", fileStreamingThreshold, 0.1f, 0.0, 600.0))
		{
			audioSettings.FileStreamingDurationThreshold = fileStreamingThreshold;
			m_Dirty = true;
		}
		ImGuiEx::EndPropertyGrid();

		{
			ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
			ImGui::TextWrapped("Stored in both the YAML project file and the runtime project data.");
		}
		if (ImGuiEx::PropertyGridHeader("Voice and Performance Budgets", false))
		{
			auto edited = audioSettings.Performance;
			bool changed = false;
			ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
			changed |= ImGuiEx::Property("Mute When Unfocused", edited.MuteWhenUnfocused, "Mute output while unfocused; timelines and gameplay pause state are preserved.", false);
			int voices = static_cast<int>(edited.RealVoices), memory = static_cast<int>(edited.BankMemoryMiB);
			changed |= ImGuiEx::Property("Real Voices", voices, 1, 512, "Most voices mixing at once. Lower priority voices go virtual.", false);
			edited.RealVoices = static_cast<uint32_t>(voices);
			changed |= ImGuiEx::Property("CPU Warning (%)", edited.CPUPercent, 0.1f, 0.1f, 100.0f, "Warn once when the measured audio CPU exceeds this value.", false);
			changed |= ImGuiEx::Property("Acoustics Warning (ms)", edited.RaytracingMilliseconds, 0.1f, 0.01f, 1000.0f, "Soundwave main-thread update time.", false);
			changed |= ImGuiEx::Property("Clip Memory (MiB)", memory, 1, 65536, "Warning threshold for the decoded clips in memory.", false);
			edited.BankMemoryMiB = static_cast<uint32_t>(memory);
			ImGuiEx::EndPropertyGrid();
			std::string remove;
			for (auto& [path, limit] : edited.BusVoices)
			{
				ImGuiEx::ScopedID id(path.c_str());
				int value = static_cast<int>(limit);
				ImGui::TextUnformatted(path.c_str());
				ImGui::SameLine();
				changed |= ImGui::InputInt("Voice warning", &value);
				limit = static_cast<uint32_t>(value);
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove"))
					remove = path;
			}
			if (!remove.empty())
			{
				edited.BusVoices.erase(remove);
				changed = true;
			}
			static std::string s_NewBudgetBus = "bus:/SFX";
			ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
			ImGuiEx::Property("New Bus", s_NewBudgetBus, "Inclusive voice warning threshold for this authored bus.", false);
			ImGuiEx::EndPropertyGrid();
			if (ImGui::Button("Add Bus Budget"))
				changed |= edited.BusVoices.emplace(s_NewBudgetBus, 32).second;
			if (changed)
			{
				if (edited.Validate())
				{
					audioSettings.Performance = std::move(edited);
					m_Dirty = true;
				}
				else
					LUX_CORE_ERROR_TAG("Audio", "Invalid audio budget. Use positive limits and valid bus:/ paths (maximum 64 buses).");
			}
			ImGui::TextWrapped("Budgets apply when the project audio system is reopened, and in new exports. Bus limits warn; the real-voice limit controls virtualization. Live meters and validation are in View > Audio Debugger.");
			ImGui::TreePop();
		}
		ImGui::Spacing();
		if (ImGuiEx::PropertyGridHeader("Desktop Audio Profiles", false))
		{
			ImGui::TextWrapped("The current operating system selects its enabled profile for Play and exports. Reopen the project after changing budgets or focus behavior. Console integration is on hold.");
			for (auto entry : { std::pair{ "Windows", &audioSettings.Windows }, std::pair{ "Linux", &audioSettings.Linux } })
			{
				ImGuiEx::ScopedID id(entry.first);
				ImGui::TextUnformatted(entry.first);
				auto& profile = *entry.second;
				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
				bool enabled = profile.Enabled;
				if (ImGuiEx::Property("Override defaults", enabled, "Use this OS's budgets.", false))
				{
					profile.Enabled = enabled;
					m_Dirty = true;
				}
				m_Dirty |= ImGuiEx::Property("Real Voices", profile.Performance.RealVoices, 1u, 512u, "Most voices mixing at once.", false);
				m_Dirty |= ImGuiEx::Property("Clip Memory (MiB)", profile.Performance.BankMemoryMiB, 1u, 65536u, "Warning threshold for decoded clips.", false);
				m_Dirty |= ImGuiEx::Property("CPU Warning (%)", profile.Performance.CPUPercent, 0.1f, 0.1f, 100.0f, "Audio CPU threshold.", false);
				m_Dirty |= ImGuiEx::Property("Acoustics Warning (ms)", profile.Performance.RaytracingMilliseconds, 0.1f, 0.01f, 1000.0f, "Soundwave update threshold.", false);
				m_Dirty |= ImGuiEx::Property("Mute When Unfocused", profile.Performance.MuteWhenUnfocused, "Output mute preserves authored bus and gameplay pause state.", false);
				ImGuiEx::EndPropertyGrid();
				if (ImGui::SmallButton("Copy Default Budgets"))
				{
					profile.Performance = audioSettings.Performance;
					m_Dirty = true;
				}
				for (auto& [path, limit] : profile.Performance.BusVoices)
				{
					ImGuiEx::ScopedID busID(path.c_str());
					int value = static_cast<int>(limit);
					ImGui::TextUnformatted(path.c_str());
					ImGui::SameLine();
					if (ImGui::DragInt("Voice warning", &value, 1.0f, 1, 65536, "%d", ImGuiSliderFlags_AlwaysClamp))
					{
						limit = static_cast<uint32_t>(std::clamp(value, 1, 65536));
						m_Dirty = true;
					}
				}
				if (!profile.Validate())
					ImGui::TextUnformatted("Invalid profile: use positive budgets.");
			}
			ImGui::TreePop();
		}

		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);

		if (ImGuiEx::PropertyAssetReference<AudioSurfaceTable>("Surface Table", audioSettings.SurfaceTable,
			"Shared footstep, impact, scrape and roll Sounds. Create a table in Content Browser > New. Loaded on Play and included in runtime exports."))
			m_Dirty = true;

		if (ImGuiEx::PropertyAssetReference<DialogueTable>("Dialogue Table", audioSettings.Dialogue.Table,
			"Localized speech and subtitles. Create a Dialogue Table in Content Browser > New; included in runtime exports."))
			m_Dirty = true;
		std::string language = audioSettings.Dialogue.Language;
		if (ImGuiEx::Property("Dialogue Language", language, "Language code, e.g. en or fr-CA. Missing translations use the table default.", false))
		{
			if (DialogueTable::ValidLanguage(language))
			{
				audioSettings.Dialogue.Language = language;
				m_Dirty = true;
			}
		}

		static const char* zoneModes[] = { "Layer zones and Soundwave", "Prefer zones", "Prefer Soundwave" };
		if (ImGuiEx::PropertyDropdown("Zone Reverb", zoneModes, 3, audioSettings.ZoneReverbMode,
			"Layered keeps both. Prefer zones reduces Soundwave's ReverbSend by active snapshot coverage. Prefer Soundwave suppresses zone snapshots while a reverb measurement exists.", false))
			m_Dirty = true;

		ImGuiEx::EndPropertyGrid();

		m_Dirty |= DrawAcousticControls(audioSettings.Occlusion);

		ImGui::Spacing();
		if (audioSettings.SurfaceTable && ImGuiEx::PropertyGridHeader("Surface Sounds", false))
		{
			if (AssetManager::IsAssetHandleValid(audioSettings.SurfaceTable) && AssetManager::GetAssetType(audioSettings.SurfaceTable) == AssetType::AudioSurfaceTable)
			{
				if (auto table = AssetManager::GetAsset<AudioSurfaceTable>(audioSettings.SurfaceTable))
				{
					ImGui::TextWrapped("Edit the shared table, then Save Surface Table. Empty material slots use Default. Footsteps and impacts require one-shot Sounds; scrape and roll require looping Sounds.");
					ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
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

			ImGui::TreePop();
		}
		if (audioSettings.Dialogue.Table && ImGuiEx::PropertyGridHeader("Dialogue Lines", false))
		{
			if (AssetManager::IsAssetHandleValid(audioSettings.Dialogue.Table) && AssetManager::GetAssetType(audioSettings.Dialogue.Table) == AssetType::DialogueTable)
			{
				if (auto table = AssetManager::GetAsset<DialogueTable>(audioSettings.Dialogue.Table))
				{
					ImGui::TextWrapped("Edits apply on the next Play. Assign a one-shot Sound; a translation's Clip replaces the Sound's clips in that language. Save after editing.");
					ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
					ImGuiEx::Property("Default Language", table->DefaultLanguage, "Each line must have a translation in this language.", false);
					ImGuiEx::Property("Bark Cooldown", table->BarkCooldown, 0.1f, 0.0f, 60.0f, "Seconds between nearby repetitions of the same bark.", false);
					ImGuiEx::Property("Bark Radius", table->BarkRadius, 0.1f, 0.0f, 1000.0f, "Nearby speaker deduplication radius in world units.", false);
					ImGuiEx::EndPropertyGrid();
					static std::string newKey, newLanguage = "en", filter;
					ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
					ImGuiEx::Property("New Line Key", newKey, "Stable script key, e.g. guard.greeting", false);
					ImGuiEx::Property("Filter Lines", filter, "", false);
					ImGuiEx::EndPropertyGrid();
					if (ImGui::Button("Add Dialogue Line") && !newKey.empty())
					{
						if (auto [entry, added] = table->Lines.try_emplace(newKey); added)
						{
							entry->second.Translations[table->DefaultLanguage].Text = "Enter subtitle text";
							newKey.clear();
						}
					}
					std::string removeLine;
					for (auto& [key, line] : table->Lines)
					{
						if (!filter.empty() && key.find(filter) == std::string::npos)
							continue;
						ImGuiEx::ScopedID lineID(key.c_str());
						if (!ImGui::TreeNode(key.c_str()))
							continue;
						ImGuiEx::SurfaceEventPicker("Speech Sound", line.Event, true);
						ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f,
												   ImGui::GetFontSize() * 28.0f);
						int32_t priority = static_cast<int32_t>(line.Priority);
						static const char* priorities[] = { "Low", "Normal", "High", "Critical" };
						if (ImGuiEx::PropertyDropdown("Priority", priorities, 4, priority, "Higher priority queued lines run first.", false))
							line.Priority = static_cast<DialoguePriority>(priority);
						ImGuiEx::Property("Interruptible", line.Interruptible, "", false);
						ImGuiEx::Property("Add Language", newLanguage, "Language code for a new translation", false);
						ImGuiEx::EndPropertyGrid();
						if (ImGui::Button("Add Translation") && DialogueTable::ValidLanguage(newLanguage))
							line.Translations.try_emplace(newLanguage, DialogueTranslation{ "Enter subtitle text", "", "" });
						std::string removeLanguage;
						for (auto& [locale, translation] : line.Translations)
						{
							ImGuiEx::ScopedID localeID(locale.c_str());
							if (!ImGui::TreeNode(locale.c_str()))
								continue;
							ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f,
													   ImGui::GetFontSize() * 28.0f);
							ImGuiEx::PropertyMultiline("Text", translation.Text, "Localized subtitle", false);
							ImGuiEx::Property("Speaker Name", translation.SpeakerName, "Empty uses the speaker entity name.", false);
							AssetHandle clip = translation.Clip;
							if (ImGuiEx::PropertyAssetReference<AudioFile>("Clip", clip, "This language's recording; it replaces the clips of the line's Sound. The subtitle lasts as long as the clip.", nullptr, {}, false))
								translation.Clip = clip;
							ImGuiEx::EndPropertyGrid();
							const Ref<Sound> speech = line.Event.Sound ? AssetManager::GetAsset<Sound>(AssetHandle(line.Event.Sound)) : nullptr;
							const std::string previewID = key + "/" + locale;
							const bool previewing = m_DialoguePreviewID == previewID && m_DialoguePreview && m_DialoguePreview->IsValid() && m_DialoguePreview->IsPlaying();
							{
								ImGuiEx::ScopedDisable disabled(!speech || !AudioEngine::HasInitializedEngine());
								if (ImGui::Button(previewing ? "Stop##Preview" : "Play##Preview"))
								{
									if (previewing)
										StopDialoguePreview();
									else
										PreviewDialogue(*speech, translation.Clip, previewID);
								}
							}
							ImGui::SetItemTooltip(speech ? "Hear this line: its Sound, playing this language's Clip." : "Assign a Speech Sound to hear the line.");
							if (locale != table->DefaultLanguage)
							{
								ImGui::SameLine();
								if (ImGui::Button("Remove Translation"))
									removeLanguage = locale;
							}
							ImGui::TreePop();
						}
						if (!removeLanguage.empty())
							line.Translations.erase(removeLanguage);
						if (ImGui::Button("Remove Line"))
							removeLine = key;
						ImGui::TreePop();
					}
					if (!removeLine.empty())
						table->Lines.erase(removeLine);
					if (ImGui::Button("Save Dialogue Table"))
						AssetManager::SaveAsset(table);
				}
			}
			else
				ImGui::TextWrapped("The dialogue table is missing or has the wrong asset type.");

			ImGui::TreePop();
		}
		if (ImGuiEx::PropertyGridHeader("Audio Accessibility", false))
		{
			auto& accessibility = audioSettings.Accessibility;
			ImGui::TextWrapped("Project defaults apply when a player has no saved preferences. In Play or the runtime, F10 opens the accessibility menu. Changes to bus mappings and event captions apply on the next Play.");
			m_Dirty |= ImGui::Checkbox("Built-in caption/cue overlay and runtime menu", &accessibility.BuiltInUI);
			if (ImGui::TreeNode("Default player preferences"))
			{
				m_Dirty |= ImGuiEx::AudioAccessibilityOptions(accessibility.Defaults, false);
				ImGui::TreePop();
			}
			ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
			m_Dirty |= ImGuiEx::Property("Caption fallback language", accessibility.DefaultLanguage, "Language used when an event has no caption in the selected dialogue language.", false);
			m_Dirty |= ImGuiEx::Property("Description duck level", accessibility.DescriptionDuck, 0.01f, 0.0f, 1.0f, "Music, SFX, UI and Ambience gain while narration plays. Route narration to Dialogue.", false);
			for (size_t i = 0; i < AudioCategoryCount; ++i)
				m_Dirty |= ImGuiEx::Property(AudioCategoryNames[i], accessibility.BusPaths[i], "Mixer bus path (View > Audio Mixer). Empty disables this category slider. Use separate sibling category buses under Master.", false);
			ImGuiEx::EndPropertyGrid();
			static std::string captionEvent, captionLanguage = "en", captionFilter, speakerName;
			// Captions are keyed by the Sound's reference (its handle in decimal), as playback reports it.
			const auto soundName = [](const std::string& reference)
			{
				uint64_t handle = 0;
				const auto parsed = std::from_chars(reference.data(), reference.data() + reference.size(), handle);
				const std::string path = parsed.ec == std::errc() ? Project::GetEditorAssetManager()->GetMetadata(AssetHandle(handle)).FilePath.generic_string() : std::string{};
				return path.empty() ? reference : path;
			};
			if (ImGui::BeginCombo("Add Sound caption/cue", captionEvent.empty() ? "Select a Sound" : soundName(captionEvent).c_str()))
			{
				for (const AssetHandle sound : AssetManager::GetAllAssetsWithType<Sound>())
				{
					const std::string reference = std::to_string(static_cast<uint64_t>(sound));
					ImGuiEx::ScopedID soundID(reference.c_str());
					if (ImGui::Selectable(soundName(reference).c_str()))
						captionEvent = reference;
				}
				ImGui::EndCombo();
			}
			if (ImGui::Button("Add Sound accessibility") && !captionEvent.empty())
			{
				accessibility.Events.try_emplace(captionEvent);
				m_Dirty = true;
			}
			ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
			ImGuiEx::Property("Filter captions", captionFilter, "Sound path or handle", false);
			ImGuiEx::Property("Caption language", captionLanguage, "Language to add to a Sound", false);
			ImGuiEx::EndPropertyGrid();
			std::string removeEvent;
			for (auto& [guid, entry] : accessibility.Events)
			{
				const std::string name = soundName(guid);
				if (!captionFilter.empty() && name.find(captionFilter) == std::string::npos && guid.find(captionFilter) == std::string::npos)
					continue;
				ImGuiEx::ScopedID eventID(guid.c_str());
				if (!ImGui::TreeNode(name.c_str()))
					continue;
				int category = static_cast<int>(entry.Category);
				if (ImGui::Combo("Category", &category, AudioCategoryNames, static_cast<int>(AudioCategoryCount)))
				{
					entry.Category = static_cast<AudioCategory>(category);
					m_Dirty = true;
				}
				m_Dirty |= ImGui::Checkbox("Visual cue", &entry.VisualCue);
				m_Dirty |= ImGui::SliderFloat("Cue importance", &entry.Intensity, 0, 1);
				m_Dirty |= ImGui::DragFloat("Caption/cue range", &entry.MaxDistance, 0.5f, 0.01f, 100000.0f);
				if (ImGui::Button("Add caption language") && DialogueTable::ValidLanguage(captionLanguage))
				{
					entry.Captions.try_emplace(captionLanguage, "[sound]");
					m_Dirty = true;
				}
				std::string removeCaption;
				for (auto& [locale, text] : entry.Captions)
				{
					ImGuiEx::ScopedID localeID(locale.c_str());
					ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
					m_Dirty |= ImGuiEx::Property(locale.c_str(), text, "Localized closed caption", false);
					ImGuiEx::EndPropertyGrid();
					if (ImGui::SmallButton("Remove caption"))
						removeCaption = locale;
				}
				if (!removeCaption.empty())
				{
					entry.Captions.erase(removeCaption);
					m_Dirty = true;
				}
				if (ImGui::Button("Remove event accessibility"))
					removeEvent = guid;
				ImGui::TreePop();
			}
			if (!removeEvent.empty())
			{
				accessibility.Events.erase(removeEvent);
				m_Dirty = true;
			}
			ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
			ImGuiEx::Property("Speaker colour name", speakerName, "Exact localized speaker name shown in subtitles", false);
			ImGuiEx::EndPropertyGrid();
			if (ImGui::Button("Add speaker colour") && !speakerName.empty())
			{
				accessibility.SpeakerColors.try_emplace(speakerName, glm::vec4(1.0f));
				m_Dirty = true;
			}
			std::string removeColor;
			for (auto& [name, color] : accessibility.SpeakerColors)
			{
				ImGuiEx::ScopedID colorID(name.c_str());
				m_Dirty |= ImGui::ColorEdit4(name.c_str(), &color.r);
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove"))
					removeColor = name;
			}
			if (!removeColor.empty())
			{
				accessibility.SpeakerColors.erase(removeColor);
				m_Dirty = true;
			}

			ImGui::TreePop();
		}
		if (ImGuiEx::PropertyGridHeader("Acoustic Materials", false))
		{
			ImGui::TextWrapped("Applied on the next Play session and included in runtime exports. Defaults come from published absorption data and the mass law; they are starting points, so tune them for your game's scale.");
			for (size_t i = 0; i < AcousticMaterialCount; ++i)
			{
				ImGuiEx::ScopedID id(static_cast<int>(i));
				if (!ImGui::TreeNode(AcousticMaterialNames[i]))
					continue;
				const auto material = static_cast<AcousticMaterial>(i);
				auto& entry = audioSettings.AcousticMaterials.Overrides[i];
				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
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
		RenderLegacyAudioData();

		ImGui::TreePop();
	}

	void ProjectSettingsWindow::PreviewDialogue(const Sound& sound, AssetHandle clip, std::string id)
	{
		StopDialoguePreview();
		// As the Sound Editor auditions: a 2D copy outside the asset's instance limit and cooldown.
		auto copy = Ref<Sound>::Create(sound);
		copy->Handle = 0;
		copy->Spatial.Enabled = false;
		copy->InstanceLimit = 0;
		copy->Cooldown = 0.0f;
		m_DialoguePreview = AudioEventInstance::Create(copy);
		if (!m_DialoguePreview || (clip && !m_DialoguePreview->SetClipOverride(clip)) || !m_DialoguePreview->Start())
		{
			StopDialoguePreview();
			return;
		}
		m_DialoguePreviewID = std::move(id);
	}

	void ProjectSettingsWindow::StopDialoguePreview()
	{
		if (m_DialoguePreview && m_DialoguePreview->IsValid())
			m_DialoguePreview->Stop();
		m_DialoguePreview = nullptr;
		m_DialoguePreviewID.clear();
	}

	void ProjectSettingsWindow::RenderLegacyAudioData()
	{
		if (!ImGuiEx::PropertyGridHeader("Legacy FMOD Data", false))
			return;

		ImGui::TextWrapped("Projects made before FMOD was removed can still carry event GUIDs, Studio settings and FMOD bank registry entries. "
			"They never play. Scan lists them; Remove FMOD Data deletes them from every scene, prefab and table, the project file and the "
			"asset registry, and leaves every other line as it was. Studio project folders on disk are not touched.");
		const bool playing = m_SceneContext && m_SceneContext->IsRunning();
		{
			ImGuiEx::ScopedDisable disabled(playing);
			if (ImGui::Button("Scan for FMOD Data"))
			{
				m_LegacyScan = AudioLegacyCleanup::Scan(m_Project->GetProjectFilePath(), m_Project->GetAssetDirectory(), m_Project->GetAssetRegistryPath());
				m_LegacyErrors.clear();
			}
		}
		if (playing)
		{
			ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
			ImGui::TextWrapped("Stop Play to scan or remove.");
		}

		for (const std::string& error : m_LegacyErrors)
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", error.c_str());
		if (!m_LegacyScan)
		{
			ImGui::TreePop();
			return;
		}

		const auto& scan = *m_LegacyScan;
		for (const std::string& error : scan.Errors)
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Skipped %s", error.c_str());
		if (scan.Files.empty())
			ImGui::TextUnformatted("No FMOD data found.");
		else
			ImGui::Text("%zu FMOD references in %zu files.", scan.Findings.size(), scan.Files.size());

		const auto relative = [&](const std::filesystem::path& path) { return std::filesystem::relative(path, m_Project->GetProjectDirectory()).generic_string(); };
		if (!scan.Findings.empty() && ImGui::BeginTable("##LegacyFMOD", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
			ImVec2(0.0f, std::min(ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(scan.Findings.size()) + 1.5f), 300.0f))))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("File");
			ImGui::TableSetupColumn("Field");
			ImGui::TableSetupColumn("Value");
			ImGui::TableHeadersRow();
			for (const auto& finding : scan.Findings)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s:%u", relative(finding.File).c_str(), finding.Line);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(finding.Where.c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(finding.Value.c_str());
			}
			ImGui::EndTable();
		}

		if (!scan.Files.empty())
		{
			ImGuiEx::ScopedDisable disabled(playing);
			if (ImGui::Button("Remove FMOD Data"))
				ImGui::OpenPopup("Remove FMOD data?");
		}
		if (ImGui::BeginPopupModal("Remove FMOD data?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Rewrite %zu files without their FMOD data?", scan.Files.size());
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped("Undo covers the open scene only; use version control for the files.");
			}
			if (ImGui::Button("Remove"))
			{
				RemoveLegacyAudioData();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::TreePop();
	}

	void ProjectSettingsWindow::RemoveLegacyAudioData()
	{
		m_LegacyErrors.clear();
		AudioLegacyCleanup::Apply(*m_LegacyScan, m_LegacyErrors);

		// The open scene, tables and prefabs in memory still hold the old fields; their next save would
		// write them back. The scene is cleared in place (one undo step), the assets reload from disk.
		const auto clear = [](AudioEventRef& reference)
		{
			const bool had = !reference.Guid.empty() || !reference.Path.empty() || !reference.BankName.empty();
			reference.Guid.clear();
			reference.Path.clear();
			reference.BankName.clear();
			return had;
		};
		bool sceneChanged = false;
		if (m_SceneContext && !m_SceneContext->IsRunning())
		{
			auto sources = m_SceneContext->GetAllEntitiesWith<AudioSourceComponent>();
			for (auto handle : sources)
				sceneChanged |= clear(sources.get<AudioSourceComponent>(handle).Event);
			auto zones = m_SceneContext->GetAllEntitiesWith<AudioZoneComponent>();
			for (auto handle : zones)
			{
				auto& zone = zones.get<AudioZoneComponent>(handle);
				sceneChanged |= clear(zone.AmbienceEvent) | clear(zone.Snapshot);
			}
			auto music = m_SceneContext->GetAllEntitiesWith<MusicDirectorComponent>();
			for (auto handle : music)
				sceneChanged |= clear(music.get<MusicDirectorComponent>(handle).Event);
			auto surfaces = m_SceneContext->GetAllEntitiesWith<AudioSurfaceComponent>();
			for (auto handle : surfaces)
			{
				auto& surface = surfaces.get<AudioSurfaceComponent>(handle);
				sceneChanged |= clear(surface.FootstepOverride) | clear(surface.ImpactOverride);
			}
		}
		if (sceneChanged)
			EditorStack::Get().MarkSceneEdited("Remove FMOD data");

		std::erase_if(m_Project->GetConfig().Audio.Accessibility.Events, [](const auto& entry) { return entry.first.starts_with('{'); });
		if (Ref<EditorAssetManager> assets = Project::GetEditorAssetManager())
		{
			for (const auto& file : m_LegacyScan->Files)
			{
				const AssetHandle handle = assets->GetAssetHandleFromFilePath(file.Path);
				const AssetType type = handle != 0 ? assets->GetAssetType(handle) : AssetType::None;
				const bool reloads = type == AssetType::Prefab || type == AssetType::DialogueTable || type == AssetType::AudioSurfaceTable;
				if (reloads && AssetManager::IsAssetLoaded(handle))
					AssetManager::ReloadData(handle);
			}
		}

		LUX_CORE_INFO_TAG("Audio", "Removed {} FMOD references from {} files", m_LegacyScan->Findings.size(), m_LegacyScan->Files.size());
		m_LegacyScan = AudioLegacyCleanup::Scan(m_Project->GetProjectFilePath(), m_Project->GetAssetDirectory(), m_Project->GetAssetRegistryPath());
	}

	void ProjectSettingsWindow::RenderScriptingSettings()
	{
		if (!ImGuiEx::PropertyGridHeader("Scripting", false))
			return;

		auto& config = m_Project->GetConfig();

		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
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
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, Colors::Theme::textDarker);
				ImGui::TextWrapped("Resolved Path: %s", resolvedModulePath.generic_string().c_str());
			}
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

		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
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
				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 14.0f, ImGui::GetFontSize() * 28.0f);
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

		const ImGuiEx::PanelAction actions[] = {{0, "Reset tag filters", LUX_ICON_UNDO}};
		if (ImGuiEx::PanelActionBar("##log_settings_actions", actions) == 0)
		{
			Log::SetDefaultTagSettings();
			m_Dirty = true;
		}

		ImGui::Spacing();
		if (ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 28.0f)
		{
			const char* levels[] = {"Trace", "Info", "Warn", "Error", "Fatal"};
			for (auto& [tag, details] : Log::EnabledTags())
			{
				ImGuiEx::ScopedID id(tag.c_str());
				ImGui::TextWrapped("%s", tag.empty() ? "(Default)" : tag.c_str());
				ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 8.0f, ImGui::GetFontSize() * 20.0f);
				m_Dirty |= ImGuiEx::Property("Enabled", details.Enabled, nullptr, false);
				int32_t level = static_cast<int32_t>(details.LevelFilter);
				if (ImGuiEx::PropertyDropdown("Level", levels, static_cast<int32_t>(std::size(levels)),
											  &level))
				{
					details.LevelFilter = static_cast<Log::Level>(level);
					m_Dirty = true;
				}
				ImGuiEx::EndPropertyGrid();
			}
			ImGui::TreePop();
			return;
		}
		if (ImGui::BeginTable("##project_log_settings", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Tag");
			ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 120.0f);
			ImGui::TableHeadersRow();

			for (auto& [tag, tagDetails] : Log::EnabledTags())
			{
				ImGuiEx::ScopedID id(tag.c_str());
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

			}

			ImGui::EndTable();
		}

		ImGui::TreePop();
	}

}
