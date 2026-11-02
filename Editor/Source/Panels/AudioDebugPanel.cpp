// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioDebugPanel.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioPerformance.h"
#include "Lux/Audio/AudioEventInstance.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiWidgets.h"
#include "Lux/Project/Project.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Entity.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cfloat>
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

		// Shown wherever a value exists but has not been measured yet, so an unmeasured field never
		// reads as a real zero.
		constexpr const char* kNoValue = "—";

		void DrawStat(const char* label, const char* value)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextDisabled("%s", label);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(value);
		}

		void DrawStat(const char* label, int value)
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
			ImGui::TextDisabled("%s", label);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextColored(color, "%s", value);
		}

		std::string FormatBytes(int bytes)
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

		// Relative high-frequency loss Soundwave measured. Studio controls its audible treatment.
		float RelativeHighFrequencyLoss(float gainLF, float gainHF)
		{
			constexpr float minAudibleGain = 1e-4f;
			if (gainLF <= minAudibleGain)
				return 0.0f;

			return std::clamp(1.0f - (gainHF / gainLF), 0.0f, 1.0f);
		}

	}

	void AudioDebugPanel::OnImGuiRender(bool& isOpen)
	{
		if (!isOpen)
			return;

		ImGui::Begin("Audio Debugger", &isOpen);

		const AudioEngineStats engine = AudioEngine::GetStats();
		UI_Backends(engine);
		UI_Playback(engine);
		UI_Performance();
		UI_Validation();
		UI_Events();
		UI_Acoustics();
		UI_Sources();
		UI_Visualisation();

		ImGui::End();
	}

	void AudioDebugPanel::UI_Performance()
	{
		if (!ImGuiEx::PropertyGridHeader("Budgets and Bus Meters", true))
			return;
		const auto& stats = AudioPerformance::GetStats();
		const auto& settings = AudioPerformance::GetSettings();
		ImGui::Text("Real voices: %d / %u   Virtual: %d   Culled sources: %zu", stats.RealVoices, settings.RealVoices, stats.VirtualVoices, stats.CulledSources);
		if (stats.MixerAvailable)
			ImGui::Text("Audio CPU: %.2f%% / %.2f%%", stats.CPUPercent, settings.CPUPercent);
		if (stats.MemoryAvailable)
			ImGui::Text("FMOD allocated memory: %.2f / %u MiB", stats.MemoryMiB, settings.BankMemoryMiB);
		if (stats.RaytracingAvailable)
			ImGui::Text("Soundwave acoustics: %.3f / %.3f ms", stats.RaytracingMilliseconds, settings.RaytracingMilliseconds);
		if (stats.VoicesExceeded || stats.CPUExceeded || stats.MemoryExceeded || stats.RaytracingExceeded)
			ImGui::TextColored(kWarnColor, "Audio budget exceeded (details logged once per bank session).");
		for (const auto& meter : AudioPerformance::GetBuses())
		{
			ImGuiEx::ScopedID id(meter.Path.c_str());
			ImGui::Text("%s: %d / %u voices (%d real)%s", meter.Path.c_str(), meter.Voices, meter.Limit, meter.RealVoices, meter.Exceeded ? " - OVER BUDGET" : "");
			if (!meter.Available)
			{
				ImGui::TextDisabled("Meter unavailable");
				continue;
			}
			const auto decibels = [](float value) { return 20.0f * std::log10(std::max(value, 0.000001f)); };
			char label[64];
			std::snprintf(label, sizeof(label), "Input peak %.1f dBFS", decibels(meter.Peak));
			ImGui::ProgressBar(std::clamp((decibels(meter.Peak) + 60) / 60, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0), label);
			std::snprintf(label, sizeof(label), "Input RMS %.1f dBFS", decibels(meter.RMS));
			ImGui::ProgressBar(std::clamp((decibels(meter.RMS) + 60) / 60, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0), label);
		}

		ImGui::TreePop();
	}

	void AudioDebugPanel::UI_Validation()
	{
		if (!ImGuiEx::PropertyGridHeader("Audio Validation", false))
			return;
		ImGui::TextWrapped("Checks built banks against serialized scenes, prefabs and audio tables, including this scene's unsaved edits. Script-only references cannot be inferred. Results are a snapshot; rerun after edits.");
		if (ImGui::Button("Validate Project Audio"))
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
			ImGui::TreePop();
			return;
		}
		ImGui::Text("%zu banks, %.2f MiB on disk, %zu / %zu events referenced", m_Validation.Banks.size(), m_Validation.BankBytes / (1024.0 * 1024.0), m_Validation.ReferencedEvents, m_Validation.CatalogEvents);
		for (const auto& bank : m_Validation.Banks)
			ImGui::Text("%s: %.2f MiB", bank.Name.c_str(), bank.Bytes / (1024.0 * 1024.0));
		for (const auto& issue : m_Validation.Issues)
		{
			ImGui::TextColored(issue.Severity == AudioValidationSeverity::Error ? kWarnColor : kOffColor, "%s", issue.Location.c_str());
			ImGui::TextWrapped("%s", issue.Message.c_str());
		}
		if (m_Validation.Issues.empty())
			ImGui::TextUnformatted("No audio validation issues.");

		ImGui::TreePop();
	}

	void AudioDebugPanel::UI_Backends(const AudioEngineStats& engine)
	{
		if (!ImGuiEx::PropertyGridHeader("Backends", true))
			return;

		const bool acousticsReady = m_Context && m_Context->GetAudioAcoustics().GetDiagnostics().Ready;

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

		ImGui::TreePop();
	}

	void AudioDebugPanel::UI_Playback(const AudioEngineStats& engine)
	{
		if (!ImGuiEx::PropertyGridHeader("Playback", true))
			return;

		if (!engine.Initialized)
		{
			ImGui::TextDisabled("The playback backend is not initialized.");
			ImGui::TreePop();
			return;
		}

		if (!engine.HasMixerStats)
		{
			ImGui::TextDisabled("%s exposes no mixer statistics.", engine.BackendName);
			ImGui::TreePop();
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

		ImGui::Spacing();
		ImGui::TextUnformatted("FMOD Studio");

		if (ImGui::BeginTable("##audio_debugger_studio", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			DrawStatColored("Live Update", engine.LiveUpdateEnabled ? "On — Studio can attach" : "Off",
				engine.LiveUpdateEnabled ? kGoodColor : kOffColor);

			// Zero banks with a Studio project configured is the "nobody built the banks" case, and
			// it is silent otherwise: every event lookup simply fails.
			if (engine.LoadedBankCount == 0)
				DrawStatColored("Banks Loaded", "0 (no banks built or found)", kWarnColor);
			else
				DrawStat("Banks Loaded", engine.LoadedBankCount);

			DrawStat("Events Described", engine.EventDescriptionCount);
			DrawStat("Event Instances", engine.PlayingEventInstances);
			ImGui::EndTable();
		}

		const std::vector<AudioBankInfo>& banks = AudioEngine::GetLoadedBanks();
		if (!banks.empty() && ImGui::BeginTable("##audio_debugger_banks", 3,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Bank");
			ImGui::TableSetupColumn("Events", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableHeadersRow();

			for (const AudioBankInfo& bank : banks)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(bank.Name.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%d", bank.EventCount);
				ImGui::TableSetColumnIndex(2);
				ImGui::TextDisabled("%s", bank.IsStringsBank ? "strings" : "content");
			}
			ImGui::EndTable();
		}

		ImGui::TreePop();
	}

	void AudioDebugPanel::UI_Events()
	{
		if (!ImGuiEx::PropertyGridHeader("Events", false))
			return;

		const std::vector<AudioEventInfo>& events = AudioEngine::GetEvents();
		if (events.empty())
		{
			ImGui::TextDisabled("No events. Author them in FMOD Studio, then build the banks.");
			ImGui::TreePop();
			return;
		}

		ImGuiEx::Widgets::SearchWidget(m_EventSearch, "Search events...");

		constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
			| ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

		if (ImGui::BeginTable("##audio_debugger_events", 3, tableFlags, ImVec2(0.0f, 220.0f)))
		{
			ImGui::TableSetupColumn("Event");
			ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthFixed, 300.0f);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			for (const AudioEventInfo& event : events)
			{
				if (!ImGuiEx::IsMatchingSearch(event.Path, m_EventSearch, false, false, true))
					continue;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(event.Path.c_str());

				ImGui::TableSetColumnIndex(1);
				ImGui::TextDisabled("%s%s", event.Is3D ? "3D" : "2D", event.IsOneshot ? " · oneshot" : "");

				// The GUID is what scenes store, so it is shown in full and selectable: a rename in
				// Studio changes the path but not this.
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(event.Guid.c_str());
			}
			ImGui::EndTable();
		}

		ImGui::TreePop();
	}

	void AudioDebugPanel::UI_Acoustics()
	{
		if (!ImGuiEx::PropertyGridHeader("Soundwave Acoustics", true))
			return;
		const AudioAcoustics::Diagnostics stats = m_Context ? m_Context->GetAudioAcoustics().GetDiagnostics() : AudioAcoustics::Diagnostics{};
		if (!stats.Ready)
		{
			ImGui::TextDisabled(stats.GeometryFailed ? "Acoustic geometry failed to build; see the log." : "The simulation runs only in Play mode.");
			ImGui::TreePop();
			return;
		}
		if (ImGui::BeginTable("##audio_debugger_acoustics_world", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			// Zero triangles with sources present is the usual reason occlusion stays flat.
			if (stats.StaticTriangles + stats.MovingTriangles == 0)
				DrawStatColored("Acoustic Triangles", "0 (nothing to occlude)", kWarnColor);
			else
			{
				DrawStat("Static Triangles", static_cast<int>(stats.StaticTriangles));
				DrawStat("Moving Triangles", static_cast<int>(stats.MovingTriangles));
			}
			DrawStat("Static Mesh Instances", static_cast<int>(stats.StaticMeshInstances));
			DrawStat("Moving Mesh Instances", static_cast<int>(stats.MovingMeshInstances));
			DrawStat("Excluded Instances", static_cast<int>(stats.ExcludedInstances));
			DrawStat("Pending Geometry Layers", static_cast<int>(stats.PendingLayers));
			DrawStat("Geometry Generation", static_cast<int>(stats.GeometryGeneration));
			DrawStat("Source Solves Run", static_cast<int>(stats.Evaluations));
			if (stats.SolveFailures)
				DrawStatColored("Solve Failures", std::to_string(stats.SolveFailures).c_str(), kWarnColor);
			DrawStat("Reverb Measurements", static_cast<int>(stats.ReverbMeasurements));
			DrawStat("Reverb Rays On", stats.ReverbOnGpu ? "GPU" : "CPU worker");
			if (stats.ReverbStalled)
				DrawStatColored("Reverb", "Stalled (frames not rendering)", kWarnColor);
			DrawStat("Source Solves", stats.SolvesPending ? "Batch on worker" : "Idle");
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Reverb (listener)");
		const Soundwave::ReverbResult* reverb = m_Context->GetAudioAcoustics().GetReverb();
		if (!reverb)
			ImGui::TextDisabled("No reverb measurement yet.");
		else if (ImGui::BeginTable("##audio_debugger_reverb", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Decay (60 dB)", (double)reverb->DecaySeconds, " s");
			DrawStat("Reverb Send", (double)(m_Context->GetAudioAcoustics().GetReverbSend() * 100.0f), " %");
			// A high outside share is the measurement saying "this is outdoors", which explains a dry reverb.
			DrawStat("Energy Outside", (double)(reverb->EscapedFraction * 100.0f), " %");
			DrawStat("Mean Free Path", (double)reverb->MeanFreePath, " m");
			DrawStat("Rays Traced", static_cast<int>(reverb->Traces));
			ImGui::EndTable();
		}
		ImGui::Spacing();
		ImGui::TextWrapped("Studio events receive Occlusion and ReverbSend from Soundwave. Author the filters, reverb bus and send automation in FMOD Studio.");

		ImGui::TreePop();
	}

	void AudioDebugPanel::UI_Sources()
	{
		if (!ImGuiEx::PropertyGridHeader("Sources", true))
			return;

		if (!m_Context)
		{
			ImGui::TextDisabled("No scene.");
			ImGui::TreePop();
			return;
		}

		ImGuiEx::Widgets::SearchWidget(m_SourceSearch, "Search sources...");

		const AudioAcoustics& acoustics = m_Context->GetAudioAcoustics();

		constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
			| ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

		// Where the listener is, so each source's distance can be shown next to its audibility. The
		// pair is the whole diagnosis for "distance does not change the volume": if distance moves
		// and audibility does not, the backend's 3D path is inert.
		const AudioListenerState* listener = m_Context->GetPrimaryAudioListener();
		const bool haveListener = listener != nullptr;
		const glm::vec3 listenerPosition = listener
			? (listener->UseAttenuationPosition ? listener->AttenuationPosition : listener->Position) : glm::vec3(0.0f);
		if (listener)
			ImGui::TextDisabled("Distances use the dominant listener's attenuation target when enabled.");

		if (ImGui::BeginTable("##audio_debugger_sources", 9, tableFlags, ImVec2(0.0f, 260.0f)))
		{
			ImGui::TableSetupColumn("Entity");
			ImGui::TableSetupColumn("Event");
			ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Playback", ImGuiTableColumnFlags_WidthFixed, 76.0f);
			ImGui::TableSetupColumn("Occlusion", ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn("Gain LF", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Gain HF", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			// Soundwave measurement, not an applied Studio filter amount.
			ImGui::TableSetupColumn("Muffle", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			// Stored reflection/opening routes and the deepest interaction count among them.
			ImGui::TableSetupColumn("Paths", ImGuiTableColumnFlags_WidthFixed, 90.0f);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			uint32_t totalCount = 0;
			uint32_t shownCount = 0;
			auto view = m_Context->GetAllEntitiesWith<AudioSourceComponent>();
			for (entt::entity entityHandle : view)
			{
				totalCount++;

				Entity entity = { entityHandle, m_Context.Raw() };
				const std::string& name = entity.GetName();
				if (!ImGuiEx::IsMatchingSearch(name, m_SourceSearch, false, false, true))
					continue;

				shownCount++;
				const AudioSourceComponent& asc = view.get<AudioSourceComponent>(entityHandle);

				// No result yet is the "not simulated" state the columns below render as kNoValue.
				const Soundwave::SolveResult* solved = acoustics.GetResult(entity.GetUUID());
				const bool simulated = solved != nullptr;
				const float gainLF = acoustics.GetGainLF(entity.GetUUID()), gainHF = acoustics.GetGainHF(entity.GetUUID());

				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(name.c_str());

				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted((asc.Event.IsValid() ? (asc.Event.Path.empty() ? asc.Event.Guid.c_str() : asc.Event.Path.c_str()) : "<unassigned>"));

				const glm::vec3 sourcePosition = glm::vec3(m_Context->GetWorldSpaceTransformMatrix(entity)[3]);

				ImGui::TableSetColumnIndex(2);
				if (haveListener)
					ImGui::Text("%.1f m", glm::distance(sourcePosition, listenerPosition));
				else
					ImGui::TextDisabled("%s", kNoValue);

				ImGui::TableSetColumnIndex(3);
				const auto event = m_Context->GetRuntimeEventInstance(entity.GetUUID());
				ImGui::TextUnformatted(m_Context->IsAudioSourceCulled(entity.GetUUID()) ? "Culled" : !event || !event->IsValid() ? "Unavailable" : event->IsPaused() ? "Paused" : event->IsVirtual() ? "Virtual" : event->IsPlaying() ? "Playing" : "Stopped");

				ImGui::TableSetColumnIndex(4);
				if (simulated)
				{
					const float occlusion = OcclusionAmount(gainLF);
					ImGui::ProgressBar(std::clamp(occlusion, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0.0f),
						std::format("{:.1f}%", occlusion * 100.0f).c_str());
				}
				else
				{
					ImGui::TextDisabled("%s", kNoValue);
				}

				ImGui::TableSetColumnIndex(5);
				simulated ? ImGui::Text("%.4f", gainLF) : ImGui::TextDisabled("%s", kNoValue);

				ImGui::TableSetColumnIndex(6);
				simulated ? ImGui::Text("%.4f", gainHF) : ImGui::TextDisabled("%s", kNoValue);

				ImGui::TableSetColumnIndex(7);
				simulated ? ImGui::Text("%.3f", RelativeHighFrequencyLoss(gainLF, gainHF))
					: ImGui::TextDisabled("%s", kNoValue);

				ImGui::TableSetColumnIndex(8);
				if (simulated)
				{
					const auto& counts = solved->Diagnostics.PathsByInteractions;
					size_t deepest = 0;
					for (size_t i = 0; i < counts.size(); ++i)
						if (counts[i])
							deepest = i;
					ImGui::Text("%uR %uO x%zu%s", solved->Diagnostics.ReflectionPaths, solved->Diagnostics.OpeningPaths, deepest,
						solved->Diagnostics.WorkLimited || solved->Diagnostics.PathsLimited ? " !" : "");
				}
				else
					ImGui::TextDisabled("%s", kNoValue);
			}

			ImGui::EndTable();

			if (totalCount == 0)
				ImGui::TextDisabled("No audio sources in this scene.");
			else if (shownCount == 0)
				ImGui::TextDisabled("None of the %u audio sources match the search.", totalCount);
		}

		ImGui::TreePop();
	}

	void AudioDebugPanel::UI_Visualisation()
	{
		if (!ImGuiEx::PropertyGridHeader("3D Visualisation", true))
			return;

		bool seeTheSound = m_SeeTheSound;
		if (ImGui::Checkbox("See the Sound", &seeTheSound))
			SetSeeTheSoundEnabled(seeTheSound);
		ImGui::SameLine();
		ImGuiEx::HelpMarker("Showcase preset: Soundwave paths with wall labels, zones and emitters. "
			"Turning it off restores the settings below.");

		ImGui::Checkbox("Show Occlusion Paths", &m_Visualisation.ShowOcclusion);
		ImGui::SameLine();
		ImGuiEx::HelpMarker("Draws each source's listener path coloured by how much the walls on it "
			"remove, labels each wall with its material, thickness and loss, and draws the reflection "
			"and opening routes Soundwave kept. Reuses the solve's results; shown in Play.");
		// Always drawn, disabled when off, so the grid below keeps a stable ID either way.
		ImGui::BeginDisabled(!m_Visualisation.ShowOcclusion);
		ImGuiEx::BeginPropertyGrid();
		ImGuiEx::Property("Wall Labels", m_Visualisation.DrawOcclusionLabels);
		ImGuiEx::Property("Selected Source Only", m_Visualisation.OcclusionSelectedOnly);
		ImGuiEx::EndPropertyGrid();
		ImGui::EndDisabled();

		ImGuiEx::BeginPropertyGrid();
		ImGuiEx::Property("All Zones and Portals", m_Visualisation.ShowAllZones, "Every audio zone and portal in Play, brightened by its live weight and labelled with it.");
		ImGuiEx::Property("Room Acoustics Readout", m_Visualisation.ShowRoomAcoustics, "Viewport readout of the listener's room as Soundwave measures it: decay, reverb send, escaping energy and mean free path.");
		ImGuiEx::Property("Draw Emitters", m_Visualisation.DrawEmitters);
		ImGuiEx::Property("Emitter Radius", m_Visualisation.EmitterRadius, 0.01f, 0.01f, 5.0f);
		ImGuiEx::EndPropertyGrid();

		ImGui::TreePop();
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
