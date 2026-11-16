// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioDebugPanel.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioPerformance.h"
#include "Lux/Audio/AudioEventInstance.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Utilities/StringUtils.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/ImGui/ImGuiWidgets.h"
#include "Lux/Project/Project.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <format>
#include <string>

namespace Lux {

	namespace {

		// Data colours for simulation state, not theme UI colours.
		constexpr ImVec4 kGoodColor{ 0.51f, 0.83f, 0.44f, 1.0f };
		constexpr ImVec4 kWarnColor{ 0.95f, 0.72f, 0.31f, 1.0f };
		constexpr ImVec4 kOffColor{ 0.60f, 0.60f, 0.60f, 1.0f };
		constexpr const char* k_Pages[] = {"Overview",	 "Sound Banks and clips", "Acoustics", "Sources",
										   "Validation", "Visualisation"};
		constexpr double k_CaptureInterval = 0.25;

		// Shown wherever a value exists but has not been measured yet, so an unmeasured field never
		// reads as a real zero.
		constexpr const char* kNoValue = "—";

		void DrawStat(const char* label, const char* value)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, kOffColor);
				ImGui::TextWrapped("%s", label);
			}
			ImGui::TableSetColumnIndex(1);
			ImGui::TextWrapped("%s", value);
		}

		void DrawStat(const char* label, int value)
		{
			DrawStat(label, std::to_string(value).c_str());
		}

		void DrawStat(const char* label, uint64_t value)
		{
			DrawStat(label, std::to_string(value).c_str());
		}

		void DrawStat(const char* label, double value, const char* suffix)
		{
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.3f%s", value, suffix);
			DrawStat(label, buffer);
		}

		void DrawStatColored(const char* label, const char* value, const ImVec4& color)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			{
				ImGuiEx::ScopedColour muted(ImGuiCol_Text, kOffColor);
				ImGui::TextWrapped("%s", label);
			}
			ImGui::TableSetColumnIndex(1);
			ImGuiEx::ScopedColour status(ImGuiCol_Text, color);
			ImGui::TextWrapped("%s", value);
		}

		std::string FormatBytes(int64_t bytes)
		{
			if (bytes <= 0)
				return "0 B";

			constexpr double kKiB = 1024.0;
			constexpr double kMiB = 1024.0 * 1024.0;
			if (bytes >= (int)kMiB)
				return std::format("{:.2f} MiB", bytes / kMiB);
			if (bytes >= (int)kKiB)
				return std::format("{:.2f} KiB", bytes / kKiB);
			return std::format("{} B", bytes);
		}

		// How occluded the source is overall, for the at-a-glance bar. The low band carries the
		// broadband transmission, so it is the honest single number for "how much is getting
		// through" — the high band's extra loss is reported separately as muffling.
		float OcclusionAmount(float gainLF)
		{
			return 1.0f - std::clamp(gainLF, 0.0f, 1.0f);
		}

		// Relative high-frequency loss Soundwave measured; the voice's low-pass is its audible treatment.
		float RelativeHighFrequencyLoss(float gainLF, float gainHF)
		{
			constexpr float minAudibleGain = 1e-4f;
			if (gainLF <= minAudibleGain)
				return 0.0f;

			return std::clamp(1.0f - (gainHF / gainLF), 0.0f, 1.0f);
		}

	}

	void AudioDebugPanel::ResetReadings()
	{
		m_HasReadings = false;
		m_Paused = false;
		m_Sources.clear();
		m_FilteredSources.clear();
		m_Banks.clear();
		m_Clips.clear();
		m_Meters.clear();
		m_SelectedSource = m_SelectedBank = 0;
		m_SelectedClip.clear();
		m_Reverb.reset();
		m_Acoustics = {};
		m_IsFocused = false;
		m_SourceSearch.clear();
		m_Validation = {};
		m_HasValidation = false;
	}

	void AudioDebugPanel::SetSceneContext(const Ref<Scene>& context)
	{
		m_Context = context;
		ResetReadings();
	}

	void AudioDebugPanel::OnProjectChanged(const Ref<Project>& project)
	{
		if (!project)
			m_Context = nullptr;
		ResetReadings();
	}

	void AudioDebugPanel::OnEvent(Event& event)
	{
		if (!m_IsFocused)
			return;
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>(
			[](KeyPressedEvent&) { return ImGui::GetIO().WantTextInput || GImGui->OpenPopupStack.Size > 0; });
	}

	void AudioDebugPanel::SaveState(Yaml::Writer& out) const
	{
		out << Yaml::Key << "Page" << Yaml::Value << k_Pages[m_Page];
	}

	void AudioDebugPanel::LoadState(const Yaml::Node& state)
	{
		const auto page = state["Page"].as<std::string>("");
		for (uint32_t index = 0; index < std::size(k_Pages); ++index)
			if (page == k_Pages[index])
				m_Page = index;
		m_ResetScroll = true;
	}

	void AudioDebugPanel::FilterSources()
	{
		m_FilteredSources.clear();
		for (size_t index = 0; index < m_Sources.size(); ++index)
		{
			const auto& row = m_Sources[index];
			if (ImGuiEx::IsMatchingSearch(row.Name, m_SourceSearch, false, false, true) ||
				ImGuiEx::IsMatchingSearch(row.Sound, m_SourceSearch, false, false, true) ||
				ImGuiEx::IsMatchingSearch(std::to_string(static_cast<uint64_t>(row.Entity)), m_SourceSearch,
										  false, false, true))
				m_FilteredSources.push_back(index);
		}
		if (m_SelectedSource && std::ranges::none_of(m_FilteredSources, [&](size_t index)
													 { return m_Sources[index].Entity == m_SelectedSource; }))
			m_SelectedSource = 0;
	}

	void AudioDebugPanel::CaptureReadings()
	{
		m_Engine = AudioEngine::GetStats();
		m_Performance = AudioPerformance::GetStats();
		m_Budgets = AudioPerformance::GetSettings();
		m_Meters = AudioPerformance::GetBuses();
		m_Banks.clear();
		const auto manager = Project::GetEditorAssetManager();
		for (const auto& bank : AudioEngine::GetLoadedSoundBanks())
		{
			const auto path = manager ? Utils::PathToUTF8(manager->GetMetadata(bank.Bank).FilePath) : "";
			m_Banks.emplace_back(path.empty() ? std::to_string(static_cast<uint64_t>(bank.Bank)) : path,
								 bank);
		}
		std::ranges::sort(m_Banks,
						  [](const auto& first, const auto& second) { return first.first < second.first; });
		m_Clips = AudioEngine::GetLoadedClips();
		std::ranges::sort(m_Clips, {}, &AudioLoadedClip::File);
		m_Acoustics =
			m_Context ? m_Context->GetAudioAcoustics().GetDiagnostics() : AudioAcoustics::Diagnostics{};
		m_Reverb.reset();
		m_ReverbSend = 0.0f;
		m_Sources.clear();
		m_HaveListener = false;
		if (m_Context)
		{
			const auto& acoustics = m_Context->GetAudioAcoustics();
			if (const auto* reverb = acoustics.GetReverb())
				m_Reverb = *reverb;
			m_ReverbSend = acoustics.GetReverbSend();
			const auto* listener = m_Context->GetPrimaryAudioListener();
			m_HaveListener = listener != nullptr;
			const auto listenerPosition =
				listener
					? (listener->UseAttenuationPosition ? listener->AttenuationPosition : listener->Position)
					: glm::vec3{};
			auto view = m_Context->GetAllEntitiesWith<AudioSourceComponent>();
			for (auto entityHandle : view)
			{
				Entity entity{entityHandle, m_Context.Raw()};
				const auto& source = view.get<AudioSourceComponent>(entityHandle);
				SourceRow row;
				row.Entity = entity.GetUUID();
				row.Name = entity.GetName();
				row.Sound =
					source.Event.IsValid()
						? (manager ? Utils::PathToUTF8(manager->GetMetadata(source.Event.Sound).FilePath)
								   : std::to_string(static_cast<uint64_t>(source.Event.Sound)))
						: "<unassigned>";
				if (row.Sound.empty())
					row.Sound = std::to_string(static_cast<uint64_t>(source.Event.Sound));
				if (listener)
					row.Distance = glm::distance(
						glm::vec3(m_Context->GetWorldSpaceTransformMatrix(entity)[3]), listenerPosition);
				const auto event = m_Context->GetRuntimeEventInstance(row.Entity);
				row.Playback = m_Context->IsAudioSourceCulled(row.Entity) ? "Culled"
							   : !event || !event->IsValid()			  ? "Unavailable"
							   : event->IsPaused()						  ? "Paused"
							   : event->IsVirtual()						  ? "Virtual"
							   : event->IsPlaying()						  ? "Playing"
																		  : "Stopped";
				if (const auto* result = acoustics.GetResult(row.Entity))
				{
					row.Simulated = true;
					row.GainLF = acoustics.GetGainLF(row.Entity);
					row.GainHF = acoustics.GetGainHF(row.Entity);
					size_t deepest = 0;
					const auto& diagnostics = result->Diagnostics;
					for (size_t index = 0; index < diagnostics.PathsByInteractions.size(); ++index)
						if (diagnostics.PathsByInteractions[index])
							deepest = index;
					row.Paths =
						std::format("{}R {}O x{}{}", diagnostics.ReflectionPaths, diagnostics.OpeningPaths,
									deepest, diagnostics.WorkLimited || diagnostics.PathsLimited ? " !" : "");
				}
				m_Sources.push_back(std::move(row));
			}
		}
		std::ranges::sort(m_Sources,
						  [](const SourceRow& first, const SourceRow& second)
						  {
							  return first.Name == second.Name ? static_cast<uint64_t>(first.Entity) <
																	 static_cast<uint64_t>(second.Entity)
															   : first.Name < second.Name;
						  });
		if (std::ranges::none_of(m_Sources, [&](const auto& row) { return row.Entity == m_SelectedSource; }))
			m_SelectedSource = 0;
		FilterSources();
		m_CapturedAt = ImGui::GetTime();
		m_HasReadings = true;
	}

	void AudioDebugPanel::OnImGuiRender(bool& isOpen)
	{
		m_IsFocused = false;
		if (!isOpen)
			return;
		ImGui::SetNextWindowSize({760.0f, 640.0f}, ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Audio Debugger", &isOpen))
		{
			ImGui::End();
			return;
		}
		m_IsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		const ImGuiEx::PanelAction actions[] = {{0, m_Paused ? "Resume" : "Pause",
												 m_Paused ? LUX_ICON_PLAY : LUX_ICON_PAUSE, nullptr, true,
												 m_Paused},
												{1, "Refresh readings", LUX_ICON_REFRESH}};
		const int action = ImGuiEx::PanelActionBar("##audio_debug_actions", actions);
		if (action == 0)
			m_Paused = !m_Paused;
		if (!m_HasReadings || action == 1 ||
			(!m_Paused && ImGui::GetTime() - m_CapturedAt >= k_CaptureInterval))
			CaptureReadings();
		ImGui::TextWrapped("%s | readings captured %.1f s ago. Audio continues while paused.",
						   m_Paused ? "Paused" : "Live (4 Hz)",
						   std::max(0.0, ImGui::GetTime() - m_CapturedAt));
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##audio_debug_page", k_Pages[m_Page]))
		{
			for (uint32_t index = 0; index < std::size(k_Pages); ++index)
			{
				if (ImGui::Selectable(k_Pages[index], index == m_Page))
				{
					m_Page = index;
					m_ResetScroll = true;
				}
				if (index == m_Page)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (ImGui::BeginChild("##audio_debug_content"))
		{
			if (m_ResetScroll)
			{
				ImGui::SetScrollY(0.0f);
				m_ResetScroll = false;
			}
			switch (m_Page)
			{
			case 0:
				UI_Backends(m_Engine);
				UI_Playback(m_Engine);
				UI_Performance();
				break;
			case 1:
				UI_SoundBanks();
				break;
			case 2:
				UI_Acoustics();
				break;
			case 3:
				UI_Sources();
				break;
			case 4:
				UI_Validation();
				break;
			case 5:
				UI_Visualisation();
				break;
			}
		}
		ImGui::EndChild();
		ImGui::End();
	}

	void AudioDebugPanel::UI_Performance()
	{
		if (!ImGuiEx::BoundedCollapsingHeader("Budgets and Bus Meters", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		const auto& stats = m_Performance;
		const auto& settings = m_Budgets;
		ImGui::TextWrapped("Real voices: %d / %u   Virtual: %d   Culled sources: %zu", stats.RealVoices,
						   settings.RealVoices, stats.VirtualVoices, stats.CulledSources);
		if (stats.MixerAvailable)
			ImGui::TextWrapped("Audio CPU: %.2f%% / %.2f%%", stats.CPUPercent, settings.CPUPercent);
		if (stats.MemoryAvailable)
			ImGui::TextWrapped("Clip memory: %.2f / %u MiB", stats.MemoryMiB, settings.BankMemoryMiB);
		if (stats.RaytracingAvailable)
			ImGui::TextWrapped("Soundwave acoustics: %.3f / %.3f ms", stats.RaytracingMilliseconds,
							   settings.RaytracingMilliseconds);
		if (stats.VoicesExceeded || stats.CPUExceeded || stats.MemoryExceeded || stats.RaytracingExceeded)
			ImGuiEx::PanelMessage("Audio budget exceeded", "Details are logged once per engine run.", true);
		for (const auto& meter : m_Meters)
		{
			ImGuiEx::ScopedID id(meter.Path.c_str());
			ImGui::TextWrapped("%s: %d / %u voices (%d real)%s", meter.Path.c_str(), meter.Voices,
							   meter.Limit, meter.RealVoices, meter.Exceeded ? " - OVER BUDGET" : "");
			if (!meter.Available)
			{
				ImGui::TextWrapped("Meter unavailable");
				continue;
			}
			const auto decibels = [](float value) { return 20.0f * std::log10(std::max(value, 0.000001f)); };
			char label[64];
			if (meter.Peak > 0.0f)
				std::snprintf(label, sizeof(label), "Input peak %.1f dBFS", decibels(meter.Peak));
			else
				std::snprintf(label, sizeof(label), "Input peak -inf dBFS");
			const bool compact = ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 24.0f;
			if (compact)
				ImGui::TextWrapped("%s", label);
			ImGui::ProgressBar(std::clamp((decibels(meter.Peak) + 60) / 60, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0),
							   compact ? "" : label);
			if (meter.RMS > 0.0f)
				std::snprintf(label, sizeof(label), "Input RMS %.1f dBFS", decibels(meter.RMS));
			else
				std::snprintf(label, sizeof(label), "Input RMS -inf dBFS");
			if (compact)
				ImGui::TextWrapped("%s", label);
			ImGui::ProgressBar(std::clamp((decibels(meter.RMS) + 60) / 60, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0),
							   compact ? "" : label);
		}

	}

	void AudioDebugPanel::UI_Validation()
	{
		if (!ImGuiEx::BoundedCollapsingHeader("Audio Validation", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		ImGui::TextWrapped(
			"Checks authored Sounds, scenes, prefabs and audio tables, including this scene's unsaved edits. "
			"Script-only references cannot be inferred. Results are a snapshot; rerun after edits.");
		const ImGuiEx::PanelAction actions[] = {
			{0, "Validate project audio", LUX_ICON_CHECK, nullptr, Project::GetActive() != nullptr}};
		if (ImGuiEx::PanelActionBar("##audio_validate", actions) == 0)
		{
			if (auto project = Project::GetActive())
			{
				m_Validation = AudioValidation::ValidateProject(*project, m_Context.Raw());
				m_Validation.Log();
				m_HasValidation = true;
			}
		}
		if (!m_HasValidation)
		{

			return;
		}
		ImGui::TextWrapped("%.2f MiB of audio, %zu / %zu Sounds referenced",
						   m_Validation.BankBytes / (1024.0 * 1024.0), m_Validation.ReferencedEvents,
						   m_Validation.CatalogEvents);
		for (const auto& issue : m_Validation.Issues)
		{
			ImGuiEx::ScopedColour severity(
				ImGuiCol_Text, issue.Severity == AudioValidationSeverity::Error ? kWarnColor : kOffColor);
			ImGui::TextWrapped("%s", issue.Location.c_str());
			ImGui::TextWrapped("%s", issue.Message.c_str());
		}
		if (m_Validation.Issues.empty())
			ImGui::TextWrapped("No audio validation issues.");
	}

	void AudioDebugPanel::UI_Backends(const AudioEngineStats& engine)
	{
		if (!ImGuiEx::BoundedCollapsingHeader("Backends", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		const bool acousticsReady = m_Acoustics.Ready;

		if (ImGui::BeginTable("##audio_debugger_backends", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Playback Backend", engine.BackendName);
			DrawStatColored("Playback State", engine.Initialized ? "Initialized" : "Not initialized",
				engine.Initialized ? kGoodColor : kWarnColor);
			if (engine.VersionMajor || engine.VersionMinor || engine.VersionPatch)
				DrawStat("Playback Version", std::format("{}.{:02}.{:02}", engine.VersionMajor, engine.VersionMinor, engine.VersionPatch).c_str());
			DrawStat("Sample Rate", engine.SampleRate > 0 ? std::format("{} Hz", engine.SampleRate).c_str() : kNoValue);

			DrawStat("Acoustics Backend", "Soundwave (built in)");
			DrawStatColored("Acoustics State", acousticsReady ? "Running" : "Idle (enter Play)", acousticsReady ? kGoodColor : kOffColor);

			ImGui::EndTable();
		}

	}

	void AudioDebugPanel::UI_Playback(const AudioEngineStats& engine)
	{
		if (!ImGuiEx::BoundedCollapsingHeader("Playback", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		if (!engine.Initialized)
		{
			ImGui::TextWrapped("The playback backend is not initialized.");

			return;
		}

		if (!engine.HasMixerStats)
		{
			ImGui::TextWrapped("Sound instances playing: %d", engine.PlayingEventInstances);
			ImGui::TextWrapped("%s exposes no mixer statistics.", engine.BackendName);

			return;
		}

		if (ImGui::BeginTable("##audio_debugger_playback", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			// "Real" channels are the ones actually being mixed; the difference is what the backend
			// virtualized after running out of audible voices.
			DrawStat("Channels Playing", engine.ChannelsPlaying);
			DrawStat("Real Channels", engine.RealChannelsPlaying);
			DrawStat("Virtualized", engine.ChannelsPlaying - engine.RealChannelsPlaying);
			DrawStat("DSP CPU", (double)engine.DSPCPUPercent, " %");
			DrawStat("Stream CPU", (double)engine.StreamCPUPercent, " %");
			DrawStat("Update CPU", (double)engine.UpdateCPUPercent, " %");
			DrawStat("Memory", FormatBytes(engine.MemoryCurrentBytes).c_str());
			DrawStat("Memory Peak", FormatBytes(engine.MemoryPeakBytes).c_str());
			ImGui::EndTable();
		}

	}

	void AudioDebugPanel::UI_SoundBanks()
	{
		if (!ImGuiEx::BoundedCollapsingHeader("Sound Banks and Clips", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		const bool compact = ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 42.0f;
		if (m_Banks.empty())
			ImGuiEx::PanelMessage("No Sound Bank loaded", "Scenes load their banks when they play.");
		if (compact && !m_Banks.empty())
		{
			if (ImGui::BeginChild("##bank_list", {0, ImGui::GetFontSize() * 8.0f}, ImGuiChildFlags_Borders))
			{
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(m_Banks.size()));
				while (clipper.Step())
					for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
					{
						const auto& [path, bank] = m_Banks[index];
						ImGuiEx::ScopedID id(bank.Bank);
						if (ImGuiEx::PanelListItem("##bank", path, bank.Bank == m_SelectedBank))
							m_SelectedBank = bank.Bank;
					}
			}
			ImGui::EndChild();
			const auto selected = std::ranges::find_if(m_Banks, [&](const auto& row)
													   { return row.second.Bank == m_SelectedBank; });
			if (selected != m_Banks.end())
				ImGui::TextWrapped("%s\n%u loads | %s%s", selected->first.c_str(), selected->second.Loads,
								   selected->second.Ready ? "Ready" : "Decoding",
								   selected->second.Resident ? ", resident" : "");
			else
				ImGui::TextWrapped("Select a bank to inspect loads, readiness and residency.");
		}
		else if (!m_Banks.empty() &&
				 ImGui::BeginTable("##audio_debugger_banks", 3,
								   ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Bank");
			ImGui::TableSetupColumn("Loads", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.0f);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 11.0f);
			ImGui::TableHeadersRow();
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(m_Banks.size()));
			while (clipper.Step())
				for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
				{
					const auto& [path, bank] = m_Banks[index];
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGuiEx::ScopedID id(bank.Bank);
					if (ImGuiEx::PanelListItem("##bank", path, bank.Bank == m_SelectedBank))
						m_SelectedBank = bank.Bank;
					ImGui::TableNextColumn();
					ImGui::Text("%u", bank.Loads);
					ImGui::TableNextColumn();
					ImGui::Text("%s%s", bank.Ready ? "Ready" : "Decoding", bank.Resident ? ", resident" : "");
				}
			ImGui::EndTable();
		}
		if (m_Clips.empty())
			ImGui::TextWrapped("No decoded or streamed clips in this reading.");
		if (compact && !m_Clips.empty())
		{
			if (ImGui::BeginChild("##clip_list", {0, ImGui::GetFontSize() * 8.0f}, ImGuiChildFlags_Borders))
			{
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(m_Clips.size()));
				while (clipper.Step())
					for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
					{
						const auto& clip = m_Clips[index];
						ImGuiEx::ScopedID id(Utils::PathToUTF8(clip.File).c_str());
						if (ImGuiEx::PanelListItem("##clip", Utils::PathToUTF8(clip.File.filename()),
												   clip.File == m_SelectedClip, Utils::PathToUTF8(clip.File)))
							m_SelectedClip = clip.File;
					}
			}
			ImGui::EndChild();
			const auto selected = std::ranges::find(m_Clips, m_SelectedClip, &AudioLoadedClip::File);
			if (selected != m_Clips.end())
				ImGui::TextWrapped(
					"%s\n%s | %u banks | %u voices%s", Utils::PathToUTF8(selected->File).c_str(),
					selected->Bytes		? FormatBytes(static_cast<int64_t>(selected->Bytes)).c_str()
					: selected->Streams ? "streamed"
										: "not decoded",
					selected->BankRefs, selected->LiveRefs, selected->Streams ? " (stream)" : "");
			else
				ImGui::TextWrapped("Select a clip to inspect memory, banks and voices.");
		}
		else if (!m_Clips.empty() &&
				 ImGui::BeginTable("##audio_debugger_clips", 4,
								   ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Clip");
			ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.0f);
			ImGui::TableSetupColumn("Banks", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.0f);
			ImGui::TableSetupColumn("Voices", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.0f);
			ImGui::TableHeadersRow();
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(m_Clips.size()));
			while (clipper.Step())
				for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
				{
					const auto& clip = m_Clips[index];
					ImGuiEx::ScopedID id(Utils::PathToUTF8(clip.File).c_str());
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					if (ImGuiEx::PanelListItem("##clip", Utils::PathToUTF8(clip.File.filename()),
											   clip.File == m_SelectedClip, Utils::PathToUTF8(clip.File)))
						m_SelectedClip = clip.File;
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(clip.Bytes ? FormatBytes(static_cast<int64_t>(clip.Bytes)).c_str()
										   : clip.Streams ? "streamed"
														  : "not decoded");
					ImGui::TableNextColumn();
					ImGui::Text("%u", clip.BankRefs);
					ImGui::TableNextColumn();
					ImGuiEx::PropertyLabel(
						std::format("{}{}", clip.LiveRefs, clip.Streams ? " (stream)" : "").c_str());
				}
			ImGui::EndTable();
		}
	}

	void AudioDebugPanel::UI_Acoustics()
	{
		if (!ImGuiEx::BoundedCollapsingHeader("Soundwave Acoustics", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		const auto& stats = m_Acoustics;
		if (!stats.Ready)
		{
			ImGuiEx::PanelMessage(stats.GeometryFailed ? "Acoustic geometry failed" : "No acoustic readings",
								  stats.GeometryFailed ? "Geometry could not be built. See Log."
													   : "The simulation runs only in Play mode.",
								  stats.GeometryFailed);

			return;
		}
		if (ImGui::BeginTable("##audio_debugger_acoustics_world", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			// Zero triangles with sources present is the usual reason occlusion stays flat.
			if (stats.StaticTriangles + stats.MovingTriangles == 0)
				DrawStatColored("Acoustic Triangles", "0 (nothing to occlude)", kWarnColor);
			else
			{
				DrawStat("Static Triangles", static_cast<uint64_t>(stats.StaticTriangles));
				DrawStat("Moving Triangles", static_cast<uint64_t>(stats.MovingTriangles));
			}
			DrawStat("Static Mesh Instances", static_cast<uint64_t>(stats.StaticMeshInstances));
			DrawStat("Moving Mesh Instances", static_cast<uint64_t>(stats.MovingMeshInstances));
			DrawStat("Excluded Instances", static_cast<uint64_t>(stats.ExcludedInstances));
			DrawStat("Pending Geometry Layers", static_cast<uint64_t>(stats.PendingLayers));
			DrawStat("Geometry Generation", static_cast<uint64_t>(stats.GeometryGeneration));
			DrawStat("Source Solves Run", static_cast<uint64_t>(stats.Evaluations));
			if (stats.SolveFailures)
				DrawStatColored("Solve Failures", std::to_string(stats.SolveFailures).c_str(), kWarnColor);
			DrawStat("Reverb Measurements", static_cast<uint64_t>(stats.ReverbMeasurements));
			DrawStat("Reverb Rays On", stats.ReverbOnGpu ? "GPU" : "CPU worker");
			if (stats.ReverbStalled)
				DrawStatColored("Reverb", "Stalled (frames not rendering)", kWarnColor);
			DrawStat("Source Solves", stats.SolvesPending ? "Batch on worker" : "Idle");
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::TextWrapped("Reverb (listener)");
		const Soundwave::ReverbResult* reverb = m_Reverb ? &*m_Reverb : nullptr;
		if (!reverb)
			ImGui::TextWrapped("No reverb measurement yet.");
		else if (ImGui::BeginTable("##audio_debugger_reverb", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Decay (60 dB)", (double)reverb->DecaySeconds, " s");
			DrawStat("Reverb Send", (double)(m_ReverbSend * 100.0f), " %");
			// A high outside share is the measurement saying "this is outdoors", which explains a dry reverb.
			DrawStat("Energy Outside", (double)(reverb->EscapedFraction * 100.0f), " %");
			DrawStat("Mean Free Path", (double)reverb->MeanFreePath, " m");
			DrawStat("Rays Traced", static_cast<uint64_t>(reverb->Traces));
			ImGui::EndTable();
		}
		ImGui::Spacing();
		ImGui::TextWrapped("Each source's voice applies Soundwave's occlusion as a low-pass and gain, and its reverb send feeds the mixer's reverb return (View > Audio Mixer).");

	}

	void AudioDebugPanel::UI_Sources()
	{
		if (!ImGuiEx::BoundedCollapsingHeader("Sources", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		if (!m_Context)
		{
			ImGuiEx::PanelEmptyState("##audio_no_scene", "No scene open",
									 "Open a scene to inspect audio sources.");
			return;
		}
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::InputTextWithHint("##audio_source_search", "Search name, Sound or UUID...",
									 &m_SourceSearch))
			FilterSources();
		ImGui::TextWrapped("%zu of %zu sources", m_FilteredSources.size(), m_Sources.size());
		if (m_HaveListener)
			ImGui::TextWrapped("Distances use the dominant listener's attenuation target when enabled.");
		if (m_FilteredSources.empty())
		{
			ImGuiEx::PanelMessage("No matching audio sources",
								  m_Sources.empty() ? "This scene has no audio sources."
													: "Change the search to see other captured sources.");
			return;
		}
		const bool compact = ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 85.0f;
		if (compact)
		{
			if (ImGui::BeginChild(
					"##source_list",
					{0, std::min(ImGui::GetContentRegionAvail().y * 0.45f, ImGui::GetFontSize() * 12.0f)},
					ImGuiChildFlags_Borders))
			{
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(m_FilteredSources.size()));
				while (clipper.Step())
					for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
					{
						const auto& row = m_Sources[m_FilteredSources[index]];
						ImGuiEx::ScopedID id(row.Entity);
						if (ImGuiEx::PanelListItem("##source", row.Name, row.Entity == m_SelectedSource,
												   row.Name + "\n" + row.Sound))
							m_SelectedSource = row.Entity;
					}
			}
			ImGui::EndChild();
		}
		else if (ImGui::BeginTable("##audio_debugger_sources", 9,
								   ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
									   ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
								   {0, ImGui::GetFontSize() * 18.0f}))
		{
			ImGui::TableSetupColumn("Entity");
			ImGui::TableSetupColumn("Sound");
			for (const auto* label :
				 {"Dist", "Playback", "Occlusion", "Gain LF", "Gain HF", "Muffle", "Paths"})
				ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed,
										ImGui::GetFontSize() *
											(std::strcmp(label, "Paths") == 0 ? 8.0f : 6.0f));
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(m_FilteredSources.size()));
			while (clipper.Step())
				for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
				{
					const auto& row = m_Sources[m_FilteredSources[index]];
					ImGuiEx::ScopedID id(row.Entity);
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					if (ImGuiEx::PanelListItem("##source", row.Name, row.Entity == m_SelectedSource))
						m_SelectedSource = row.Entity;
					ImGui::TableNextColumn();
					ImGuiEx::PropertyLabel(row.Sound.c_str());
					ImGui::TableNextColumn();
					ImGuiEx::PropertyLabel(row.Distance ? std::format("{:.1f} m", *row.Distance).c_str()
														: kNoValue);
					ImGui::TableNextColumn();
					ImGuiEx::PropertyLabel(row.Playback.c_str());
					ImGui::TableNextColumn();
					if (row.Simulated)
						ImGui::ProgressBar(
							OcclusionAmount(row.GainLF), {-FLT_MIN, 0},
							std::format("{:.1f}%", OcclusionAmount(row.GainLF) * 100.0f).c_str());
					else
						ImGuiEx::PropertyLabel(kNoValue);
					ImGui::TableNextColumn();
					ImGuiEx::PropertyLabel(row.Simulated ? std::format("{:.4f}", row.GainLF).c_str()
														 : kNoValue);
					ImGui::TableNextColumn();
					ImGuiEx::PropertyLabel(row.Simulated ? std::format("{:.4f}", row.GainHF).c_str()
														 : kNoValue);
					ImGui::TableNextColumn();
					ImGuiEx::PropertyLabel(
						row.Simulated
							? std::format("{:.3f}", RelativeHighFrequencyLoss(row.GainLF, row.GainHF)).c_str()
							: kNoValue);
					ImGui::TableNextColumn();
					ImGuiEx::PropertyLabel(row.Simulated ? row.Paths.c_str() : kNoValue);
				}
			ImGui::EndTable();
		}
		const auto selected = std::ranges::find(m_Sources, m_SelectedSource, &SourceRow::Entity);
		if (selected == m_Sources.end())
			ImGui::TextWrapped("Select a source to inspect its complete reading.");
		else
		{
			ImGui::Separator();
			ImGui::TextWrapped("%s\nUUID: %llu\nSound: %s\nPlayback: %s", selected->Name.c_str(),
							   static_cast<unsigned long long>(selected->Entity), selected->Sound.c_str(),
							   selected->Playback.c_str());
			ImGui::TextWrapped("Distance: %s", selected->Distance
												   ? std::format("{:.1f} m", *selected->Distance).c_str()
												   : "Unavailable (no listener)");
			if (selected->Simulated)
				ImGui::TextWrapped(
					"Occlusion: %.1f%%\nGain LF: %.4f\nGain HF: %.4f\nMuffle (relative high-frequency loss): "
					"%.3f\nPaths: %s",
					OcclusionAmount(selected->GainLF) * 100.0f, selected->GainLF, selected->GainHF,
					RelativeHighFrequencyLoss(selected->GainLF, selected->GainHF), selected->Paths.c_str());
			else
				ImGui::TextWrapped("Acoustic reading unavailable: this source has not been simulated.");
		}
	}

	void AudioDebugPanel::UI_Visualisation()
	{
		if (!ImGuiEx::BoundedCollapsingHeader("3D Visualisation", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		ImGuiEx::BeginPropertyGrid(2, ImGui::GetFontSize() * 13.0f, ImGui::GetFontSize() * 24.0f);
		bool see = m_SeeTheSound;
		if (ImGuiEx::Property("See the Sound", see,
							  "Show paths, wall labels, zones and emitters. Turning off restores the "
							  "settings from before this preset.",
							  false))
			SetSeeTheSoundEnabled(see);
		ImGuiEx::Property("Occlusion paths", m_Visualisation.ShowOcclusion,
						  "Shows published Soundwave solve results in Play.", false);
		{
			ImGuiEx::ScopedDisable disabled(!m_Visualisation.ShowOcclusion);
			ImGuiEx::Property("Wall labels", m_Visualisation.DrawOcclusionLabels,
							  "Show each wall's acoustic material and losses.", false);
			ImGuiEx::Property("Selected source only", m_Visualisation.OcclusionSelectedOnly,
							  "Limit paths to the selected source.", false);
		}
		ImGuiEx::Property("All zones and portals", m_Visualisation.ShowAllZones,
						  "Show live zone and portal weights in Play.", false);
		ImGuiEx::Property("Room acoustics readout", m_Visualisation.ShowRoomAcoustics,
						  "Listener decay, reverb send, escaping energy and mean free path.", false);
		ImGuiEx::Property("Draw emitters", m_Visualisation.DrawEmitters,
						  "Draw emitter locations in the scene.", false);
		if (ImGuiEx::Property("Emitter radius", m_Visualisation.EmitterRadius, 0.01f, 0.01f, 5.0f, "Metres.",
							  false, AudioVisualisationSettings{}.EmitterRadius))
			m_Visualisation.EmitterRadius = std::isfinite(m_Visualisation.EmitterRadius)
												? std::clamp(m_Visualisation.EmitterRadius, 0.01f, 5.0f)
												: AudioVisualisationSettings{}.EmitterRadius;
		ImGuiEx::EndPropertyGrid();
	}

	void AudioDebugPanel::SetSeeTheSoundEnabled(bool enabled)
	{
		if (enabled == m_SeeTheSound)
			return;

		m_SeeTheSound = enabled;
		if (!enabled)
		{
			m_Visualisation = m_VisualisationBeforeSeeTheSound;
			return;
		}

		m_VisualisationBeforeSeeTheSound = m_Visualisation;
		m_Visualisation.ShowOcclusion = true;
		m_Visualisation.DrawOcclusionLabels = true;
		m_Visualisation.OcclusionSelectedOnly = false;
		m_Visualisation.DrawEmitters = true;
		m_Visualisation.ShowAllZones = true;
		m_Visualisation.ShowRoomAcoustics = true;
	}

}
