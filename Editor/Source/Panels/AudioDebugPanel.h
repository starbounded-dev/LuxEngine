// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioValidation.h"
#include "Lux/Audio/AudioAcoustics.h"
#include "Lux/Audio/AudioPerformance.h"
#include "Lux/Editor/EditorPanel.h"

#include <string>
#include <optional>

namespace Lux {

	// How the ray-traced acoustics simulation is drawn into the viewport. Owned by the panel
	// because that is where it is edited, and read by EditorLayer::OnOverlayRender, which owns the
	// only Renderer2D with a camera already set up for the frame.
	struct AudioVisualisationSettings
	{
		bool DrawEmitters = false;
		float EmitterRadius = 0.35f;

		// Soundwave paths: listener->source, coloured by loss, with the walls they cross and the
		// reflection/opening routes the solve kept. They reuse results the audio already computes.
		bool ShowOcclusion = false;
		bool DrawOcclusionLabels = true;
		bool OcclusionSelectedOnly = false;

		// Every zone and portal with its live weight (not just the selection), and a viewport readout
		// of the listener's measured room acoustics.
		bool ShowAllZones = false;
		bool ShowRoomAcoustics = false;
	};

	// In-editor view of the audio stack: playback, loaded Sound Banks and clips, and the built-in Soundwave acoustics.
	// Pure visualization over data those two subsystems already expose — AudioEngine::GetStats and
	// the scene's AudioAcoustics diagnostics, results and reverb. No audio instrumentation lives here.
	class AudioDebugPanel : public EditorPanel
	{
	public:
		AudioDebugPanel() = default;
		virtual ~AudioDebugPanel() = default;

		void SetSceneContext(const Ref<Scene>& context) override;
		void OnProjectChanged(const Ref<Project>& project) override;
		void OnEvent(Event& event) override;
		void OnClose() override
		{
			m_IsFocused = false;
		}
		void SaveState(Yaml::Writer& out) const override;
		void LoadState(const Yaml::Node& state) override;
		virtual void OnImGuiRender(bool& isOpen) override;
#ifndef LUX_DIST
		static bool RunSelfTests(std::vector<std::string>& failures);
#endif

		const AudioVisualisationSettings& GetVisualisationSettings() const { return m_Visualisation; }

		// The showcase preset: Soundwave paths with labels, zones and emitters. Turning it off
		// restores whatever the settings were before it was turned on.
		bool IsSeeTheSoundEnabled() const { return m_SeeTheSound; }
		void SetSeeTheSoundEnabled(bool enabled);

	private:
		void CaptureReadings();
		void ResetReadings();
		void FilterSources();
		struct SourceRow
		{
			UUID Entity = 0;
			std::string Name, Sound, Playback, Paths;
			std::optional<float> Distance;
			bool Simulated = false;
			float GainLF = 1.0f, GainHF = 1.0f;
		};
		// All pages share readings captured together on main; paused data owns no scene pointers.
		void UI_Performance();
		void UI_Validation();
		void UI_Backends(const AudioEngineStats& engine);
		void UI_Playback(const AudioEngineStats& engine);
		void UI_SoundBanks();
		void UI_Acoustics();
		void UI_Sources();
		void UI_Visualisation();

	private:
		Ref<Scene> m_Context;
		AudioValidationReport m_Validation;
		bool m_HasValidation = false;
		std::string m_SourceSearch;
		AudioVisualisationSettings m_Visualisation;
		AudioVisualisationSettings m_VisualisationBeforeSeeTheSound;
		bool m_SeeTheSound = false;
		uint32_t m_Page = 0;
		bool m_IsFocused = false, m_Paused = false, m_HasReadings = false, m_ResetScroll = true;
		double m_CapturedAt = 0.0;
		AudioEngineStats m_Engine;
		AudioPerformanceStats m_Performance;
		AudioPerformanceSettings m_Budgets;
		std::vector<AudioBusMeter> m_Meters;
		std::vector<std::pair<std::string, AudioLoadedBank>> m_Banks;
		std::vector<AudioLoadedClip> m_Clips;
		AudioAcoustics::Diagnostics m_Acoustics;
		std::optional<Soundwave::ReverbResult> m_Reverb;
		float m_ReverbSend = 0.0f;
		std::vector<SourceRow> m_Sources;
		std::vector<size_t> m_FilteredSources;
		UUID m_SelectedSource = 0;
		UUID m_SelectedBank = 0;
		std::filesystem::path m_SelectedClip;
		bool m_HaveListener = false;
	};

}
