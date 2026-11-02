// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioValidation.h"
#include "Lux/Editor/EditorPanel.h"

#include <string>

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

	// In-editor view of the audio stack: playback (FMOD or native) and the built-in Soundwave acoustics.
	// Pure visualization over data those two subsystems already expose — AudioEngine::GetStats and
	// the scene's AudioAcoustics diagnostics, results and reverb. No audio instrumentation lives here.
	class AudioDebugPanel : public EditorPanel
	{
	public:
		AudioDebugPanel() = default;
		virtual ~AudioDebugPanel() = default;

		virtual void SetSceneContext(const Ref<Scene>& context) override { m_Context = context; m_Validation = {}; m_HasValidation = false; }
		virtual void OnImGuiRender(bool& isOpen) override;

		const AudioVisualisationSettings& GetVisualisationSettings() const { return m_Visualisation; }

		// The showcase preset: Soundwave paths with labels, zones and emitters. Turning it off
		// restores whatever the settings were before it was turned on.
		bool IsSeeTheSoundEnabled() const { return m_SeeTheSound; }
		void SetSeeTheSoundEnabled(bool enabled);

	private:
		// The engine snapshot is taken once per frame and shared, so the two sections that read it
		// cannot disagree about the same frame.
		void UI_Performance();
		void UI_Validation();
		void UI_Backends(const AudioEngineStats& engine);
		void UI_Playback(const AudioEngineStats& engine);
		void UI_Events();
		void UI_Acoustics();
		void UI_Sources();
		void UI_Visualisation();

	private:
		Ref<Scene> m_Context;
		AudioValidationReport m_Validation;
		bool m_HasValidation = false;
		std::string m_SourceSearch;
		std::string m_EventSearch;
		AudioVisualisationSettings m_Visualisation;
		AudioVisualisationSettings m_VisualisationBeforeSeeTheSound;
		bool m_SeeTheSound = false;
	};

}
