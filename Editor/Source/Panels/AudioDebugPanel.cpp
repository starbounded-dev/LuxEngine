// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioDebugPanel.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioPerformance.h"
#include "Lux/Audio/AudioEventInstance.h"
#include "Lux/Audio/RaytracedAudioScene.h"
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

		std::string FormatVec3(const glm::vec3& v)
		{
			return std::format("{:.2f}, {:.2f}, {:.2f}", v.x, v.y, v.z);
		}

		// How occluded the source is overall, for the at-a-glance bar. The low band carries the
		// broadband transmission, so it is the honest single number for "how much is getting
		// through" — the high band's extra loss is reported separately as muffling.
		float OcclusionAmount(const RaytracedAudioResult& result)
		{
			return 1.0f - std::clamp(result.OcclusionGainLF, 0.0f, 1.0f);
		}

		// Relative high-frequency loss measured by VA. Studio controls its audible treatment.
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
		UI_Reverb();
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
			ImGui::Text("VA raytracing: %.3f / %.3f ms", stats.RaytracingMilliseconds, settings.RaytracingMilliseconds);
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

		const RaytracedAudioStats acoustics = m_Context && m_Context->GetRaytracedAudioScene()
			? m_Context->GetRaytracedAudioScene()->GetStats()
			: RaytracedAudioStats{ .Available = RaytracedAudioScene::IsAvailable() };

		if (ImGui::BeginTable("##audio_debugger_backends", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Playback Backend", engine.BackendName);
			DrawStatColored("Playback State", engine.Initialized ? "Initialized" : "Not initialized",
				engine.Initialized ? kGoodColor : kWarnColor);
			if (engine.VersionMajor || engine.VersionMinor || engine.VersionPatch)
				DrawStat("Playback Version", std::format("{}.{:02}.{:02}", engine.VersionMajor, engine.VersionMinor, engine.VersionPatch).c_str());
			DrawStat("Sample Rate", engine.SampleRate > 0 ? std::format("{} Hz", engine.SampleRate).c_str() : kNoValue);

			DrawStat("Acoustics Backend", "Vercidium Audio");
			if (acoustics.Available)
			{
				DrawStatColored("Acoustics State", acoustics.Running ? "Running" : "Idle (enter Play)",
					acoustics.Running ? kGoodColor : kOffColor);
				DrawStat("Acoustics Version", std::format("{}.{}.{} ({})", acoustics.VersionMajor, acoustics.VersionMinor,
					acoustics.VersionPatch, acoustics.ProductionBuild ? "production" : "development").c_str());
			}
			else
			{
				// Runtime availability check; VA is a required build dependency.
				DrawStatColored("Acoustics State", "Not compiled in", kOffColor);
			}

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
		if (!ImGuiEx::PropertyGridHeader("Ray-Traced Acoustics", true))
			return;

		Ref<RaytracedAudioScene> raytraced = m_Context ? m_Context->GetRaytracedAudioScene() : nullptr;
		if (!raytraced)
		{
			if (!RaytracedAudioScene::IsAvailable())
				ImGui::TextDisabled("The required Vercidium Audio SDK is unavailable. Check the installed runtime libraries.");
			else
				ImGui::TextDisabled("The simulation runs only in Play mode.");

			ImGui::TreePop();
			return;
		}

		const RaytracedAudioStats stats = raytraced->GetStats();

		if (ImGui::BeginTable("##audio_debugger_acoustics_world", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			DrawStatColored("Worker Threads", stats.ThreadsRunning ? "Running" : "Idle",
				stats.ThreadsRunning ? kGoodColor : kOffColor);
			DrawStat("Emitters (SDK)", stats.EmitterCount);
			DrawStat("Source Emitters", stats.SourceEmitterCount);
			DrawStat("Static Primitives", stats.StaticPrimitiveCount);
			DrawStat("Dynamic Primitives", stats.DynamicPrimitiveCount);
			DrawStat("Dynamic Triangles", stats.DynamicTriangleCount);
			DrawStat("Pending Geometry Updates", static_cast<int>(m_Context->GetPendingAudioGeometryCount()));

			// Zero triangles with sources present is the usual reason occlusion stays flat: the
			// scene has no MeshColliderComponent for OnRaytracedAudioStart to mirror.
			if (stats.StaticTriangleCount + stats.DynamicTriangleCount == 0)
				DrawStatColored("Acoustic Triangles", "0 (nothing to occlude)", kWarnColor);
			else
				DrawStat("Static Triangles", stats.StaticTriangleCount);

			DrawStat("Rays This Frame", stats.RaysCastThisFrame);
			DrawStat("Grouped EAX Zones", stats.GroupedEAXCount);
			DrawStat("Work Items", stats.WorkItemCount);
			DrawStat("Max Concurrency", stats.MaximumConcurrencyLevel);
			DrawStat("World Min", FormatVec3(stats.WorldMin).c_str());
			DrawStat("World Size", FormatVec3(stats.WorldSize).c_str());
			DrawStat("Listener", FormatVec3(stats.ListenerPosition).c_str());
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Ambience (listener)");

		const RaytracedAudioAmbience ambience = raytraced->GetAmbience();
		if (!ambience.Valid)
		{
			ImGui::TextDisabled("No ambience result yet — the listener has not completed a pass.");
		}
		else if (ImGui::BeginTable("##audio_debugger_ambience", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Ambient Gain LF", (double)ambience.AmbientGainLF, "");
			DrawStat("Ambient Gain HF", (double)ambience.AmbientGainHF, "");
			DrawStat("Energy Returned", (double)(ambience.ReturnedPercent * 100.0f), " %");

			// A high outside percentage is the simulation saying "this is outdoors" — worth calling
			// out, because it explains an otherwise surprisingly dry reverb.
			DrawStat("Energy Outside", (double)(ambience.OutsidePercent * 100.0f), " %");
			DrawStat("Measured Decay LF", (double)ambience.MeasuredDecayTimeLF, " s");
			DrawStat("Measured Decay HF", (double)ambience.MeasuredDecayTimeHF, " s");
			DrawStat("Material Roughness", (double)ambience.MaterialRoughness, "");
			DrawStat("Material Absorption LF", (double)ambience.MaterialAbsorptionLF, "");
			DrawStat("Material Absorption HF", (double)ambience.MaterialAbsorptionHF, "");
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Timings (SDK averages)");
		if (ImGui::BeginTable("##audio_debugger_acoustics_timings", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Main Thread", stats.MainThreadTimeMs, " ms");
			DrawStat("Raytracing", stats.RaytracingTimeMs, " ms");
			DrawStat("Preparation", stats.PreparationTimeMs, " ms");
			DrawStat("Analysis", stats.AnalysisTimeMs, " ms");
			DrawStat("Latency", stats.LatencyMs, " ms");
			ImGui::EndTable();
		}

		ImGui::TreePop();
	}

	void AudioDebugPanel::UI_Reverb()
	{
		if (!ImGuiEx::PropertyGridHeader("Reverb", true))
			return;

		Ref<RaytracedAudioScene> raytraced = m_Context ? m_Context->GetRaytracedAudioScene() : nullptr;
		const RaytracedAudioReverb reverb = raytraced ? raytraced->GetAmbience().Reverb : RaytracedAudioReverb{};

		if (!reverb.Valid)
		{
			ImGui::TextDisabled("No reverb result yet.");
			ImGui::TreePop();
			return;
		}

		ImGui::TextUnformatted("Simulation output (Vercidium EAX)");
		if (ImGui::BeginTable("##audio_debugger_reverb_va", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			DrawStat("Gain", (double)reverb.Gain, "");
			DrawStat("Gain LF", (double)reverb.GainLF, "");
			DrawStat("Gain HF", (double)reverb.GainHF, "");
			DrawStat("Decay Time", (double)reverb.DecayTime, " s");
			DrawStat("Decay LF Ratio", (double)reverb.DecayLFRatio, "");
			DrawStat("Decay HF Ratio", (double)reverb.DecayHFRatio, "");
			DrawStat("Reflections Gain", (double)reverb.ReflectionsGain, "");
			DrawStat("Reflections Delay", (double)reverb.ReflectionsDelay, " s");
			DrawStat("Late Reverb Gain", (double)reverb.LateReverbGain, "");
			DrawStat("Late Reverb Delay", (double)reverb.LateReverbDelay, " s");
			DrawStat("Density", (double)reverb.Density, "");
			DrawStat("Diffusion", (double)reverb.Diffusion, "");
			DrawStat("Echo Time", (double)reverb.EchoTime, " s");
			DrawStat("Echo Depth", (double)reverb.EchoDepth, "");
			DrawStat("Modulation Time", (double)reverb.ModulationTime, " s");
			DrawStat("Modulation Depth", (double)reverb.ModulationDepth, "");
			DrawStat("Air Absorption HF", (double)reverb.AirAbsorptionGainHF, "");
			DrawStat("HF Reference", (double)reverb.HFReference, " Hz");
			DrawStat("LF Reference", (double)reverb.LFReference, " Hz");
			DrawStat("Room Rolloff", (double)reverb.RoomRolloffFactor, "");
			DrawStat("Decay HF Limit", reverb.DecayHFLimit ? "Yes" : "No");
			ImGui::EndTable();
		}

		ImGui::Spacing();
		ImGui::TextWrapped("Studio events receive ReverbSend from VA's returned energy. Author the reverb bus and send automation in FMOD Studio; these EAX measurements are diagnostic only.");

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

		Ref<RaytracedAudioScene> raytraced = m_Context->GetRaytracedAudioScene();

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

		if (ImGui::BeginTable("##audio_debugger_sources", 8, tableFlags, ImVec2(0.0f, 260.0f)))
		{
			ImGui::TableSetupColumn("Entity");
			ImGui::TableSetupColumn("Event");
			ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 64.0f);
			ImGui::TableSetupColumn("Playback", ImGuiTableColumnFlags_WidthFixed, 76.0f);
			ImGui::TableSetupColumn("Occlusion", ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn("Gain LF", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Gain HF", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			// VA measurement, not an applied Studio filter amount.
			ImGui::TableSetupColumn("Muffle", ImGuiTableColumnFlags_WidthFixed, 70.0f);
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

				// GetResult answers an all-defaults (Valid = false) result for an emitter that does
				// not exist yet or is still initialising, which is exactly the "not simulated"
				// state the columns below render as kNoValue.
				const RaytracedAudioResult result = raytraced ? raytraced->GetResult(entity.GetUUID()) : RaytracedAudioResult{};

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
				if (result.Valid)
				{
					const float occlusion = OcclusionAmount(result);
					ImGui::ProgressBar(std::clamp(occlusion, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0.0f),
						std::format("{:.1f}%", occlusion * 100.0f).c_str());
				}
				else
				{
					ImGui::TextDisabled("%s", kNoValue);
				}

				ImGui::TableSetColumnIndex(5);
				result.Valid ? ImGui::Text("%.4f", result.OcclusionGainLF) : ImGui::TextDisabled("%s", kNoValue);

				ImGui::TableSetColumnIndex(6);
				result.Valid ? ImGui::Text("%.4f", result.OcclusionGainHF) : ImGui::TextDisabled("%s", kNoValue);

				ImGui::TableSetColumnIndex(7);
				result.Valid ? ImGui::Text("%.3f", RelativeHighFrequencyLoss(result.OcclusionGainLF, result.OcclusionGainHF))
					: ImGui::TextDisabled("%s", kNoValue);
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
		ImGuiEx::HelpMarker("Showcase preset: occlusion paths with wall labels, VA rays and emitters. "
			"Turning it off restores the settings below.");

		ImGui::Checkbox("Show Occlusion Paths", &m_Visualisation.ShowOcclusion);
		ImGui::SameLine();
		ImGuiEx::HelpMarker("Draws each source's listener path coloured by how much the walls on it "
			"remove, and labels each wall with its material, thickness and loss. Uses the engine "
			"occlusion results, so it costs no extra raycasts; shown in Play with Engine occlusion.");
		// Always drawn, disabled when off, so the grid below keeps a stable ID either way.
		ImGui::BeginDisabled(!m_Visualisation.ShowOcclusion);
		ImGuiEx::BeginPropertyGrid();
		ImGuiEx::Property("Wall Labels", m_Visualisation.DrawOcclusionLabels);
		ImGuiEx::Property("Selected Source Only", m_Visualisation.OcclusionSelectedOnly);
		ImGuiEx::EndPropertyGrid();
		ImGui::EndDisabled();

		ImGui::Checkbox("Show VA Rays", &m_Visualisation.Enabled);
		ImGui::SameLine();
		ImGuiEx::HelpMarker("Casts extra rays purely for display. They cost real raytracing work and "
			"feed nothing back into the audio, so leave this off unless you are looking at it.");

		Ref<RaytracedAudioScene> raytraced = m_Context ? m_Context->GetRaytracedAudioScene() : nullptr;
		if (m_Visualisation.Enabled && !raytraced)
			ImGui::TextDisabled("Rays are drawn in Play mode, where the simulation runs.");

		ImGui::BeginDisabled(!m_Visualisation.Enabled);

		ImGuiEx::BeginPropertyGrid();
		ImGuiEx::Property("Ray Count", m_Visualisation.RayCount, 1, 4096);
		ImGuiEx::Property("Bounce Count", m_Visualisation.BounceCount, 1, 64);
		ImGuiEx::Property("Update Interval (ms)", m_Visualisation.UpdateIntervalMs, 1, 1000);
		ImGuiEx::Property("Draw Ray Paths", m_Visualisation.DrawRayPaths);
		ImGuiEx::Property("Draw Bounce Points", m_Visualisation.DrawBouncePoints);
		ImGuiEx::Property("Draw Surface Normals", m_Visualisation.DrawNormals);
		ImGuiEx::Property("Draw Emitters", m_Visualisation.DrawEmitters);
		ImGuiEx::Property("Draw World Bounds", m_Visualisation.DrawWorldBounds);
		ImGuiEx::Property("Normal Length", m_Visualisation.NormalLength, 0.01f, 0.01f, 5.0f);
		ImGuiEx::Property("Emitter Radius", m_Visualisation.EmitterRadius, 0.01f, 0.01f, 5.0f);
		ImGuiEx::Property("Path Fade", m_Visualisation.PathFadeStrength, 0.01f, 0.0f, 1.0f);
		ImGuiEx::EndPropertyGrid();

		ImGui::EndDisabled();

		if (raytraced)
		{
			const RaytracedAudioStats stats = raytraced->GetStats();

			RaytracedAudioVisualisation snapshot;
			raytraced->GetVisualisation(snapshot);

			ImGui::Spacing();
			if (ImGui::BeginTable("##audio_debugger_visualisation", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				DrawStatColored("SDK State", stats.VisualisationEnabled ? "Casting" : "Off",
					stats.VisualisationEnabled ? kGoodColor : kOffColor);
				DrawStat("SDK Ray Count", stats.VisualisationRayCount);
				DrawStat("SDK Bounce Count", stats.VisualisationBounceCount);
				DrawStat("Bounces Received", (int)snapshot.Bounces.size());
				DrawStat("Rays In Snapshot", snapshot.RayCount);

				// Rays arriving but nothing being hit is its own diagnosis — the simulation is
				// working and the geometry mirror is empty — so the two counts are separate.
				int hitCount = 0;
				for (const RaytracedAudioBounce& bounce : snapshot.Bounces)
					hitCount += bounce.Hit ? 1 : 0;

				if (!snapshot.Bounces.empty() && hitCount == 0)
					DrawStatColored("Bounces That Hit", "0 (rays hit no geometry)", kWarnColor);
				else
					DrawStat("Bounces That Hit", hitCount);

				ImGui::EndTable();
			}

			// The SDK batches visualisation rays on its own schedule, so an enabled overlay with no
			// bounces yet is normal for the first interval — but a persistent zero is not.
			if (m_Visualisation.Enabled && stats.VisualisationEnabled && snapshot.Bounces.empty())
				ImGui::TextDisabled("Waiting for the first batch of visualisation rays...");
		}

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
		m_Visualisation.Enabled = true;
		m_Visualisation.DrawRayPaths = true;
		m_Visualisation.DrawBouncePoints = true;
		m_Visualisation.DrawEmitters = true;
	}

}
