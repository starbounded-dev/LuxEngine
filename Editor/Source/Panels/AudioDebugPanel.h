#pragma once

#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioValidation.h"
#include "Lux/Audio/RaytracedAudioScene.h"
#include "Lux/Editor/EditorPanel.h"

#include <string>

namespace Lux {

	// How the ray-traced acoustics simulation is drawn into the viewport. Owned by the panel
	// because that is where it is edited, and read by EditorLayer::OnOverlayRender, which owns the
	// only Renderer2D with a camera already set up for the frame.
	struct AudioVisualisationSettings
	{
		bool Enabled = false;

		// Passed straight to the SDK. These cost real raytracing work and feed nothing back into
		// the audio, which is why they are off by default and kept modest when on.
		int RayCount = 64;
		int BounceCount = 4;
		int UpdateIntervalMs = 50;

		bool DrawRayPaths = true;
		bool DrawBouncePoints = true;
		bool DrawNormals = false;
		bool DrawEmitters = true;
		bool DrawWorldBounds = false;

		float NormalLength = 0.25f;
		float EmitterRadius = 0.35f;

		// Ray paths fade along their length so the listener end reads as the origin and later
		// bounces recede, the way Vercidium's own debug view draws them.
		float PathFadeStrength = 0.75f;
	};

	// In-editor view of the audio stack: FMOD Studio playback and the
	// ray-traced acoustics simulation (Vercidium Audio). Pure visualization over data those two
	// subsystems already expose — AudioEngine::GetStats / GetReverbSnapshot, and
	// RaytracedAudioScene::GetStats / GetResult / GetAmbience / GetVisualisation. No audio
	// instrumentation lives here.
	//
	// Deliberately free of LUX_ENABLE_FMOD / LUX_ENABLE_RAYTRACED_AUDIO: both are defined only for
	// the Core project (Core/premake5.lua), so the Editor cannot branch on them. Each backend
	// identifies itself through its stats struct instead.
	class AudioDebugPanel : public EditorPanel
	{
	public:
		AudioDebugPanel() = default;
		virtual ~AudioDebugPanel() = default;

		virtual void SetSceneContext(const Ref<Scene>& context) override { m_Context = context; m_Validation = {}; m_HasValidation = false; }
		virtual void OnImGuiRender(bool& isOpen) override;

		const AudioVisualisationSettings& GetVisualisationSettings() const { return m_Visualisation; }

	private:
		// The engine snapshot is taken once per frame and shared, so the two sections that read it
		// cannot disagree about the same frame.
		void UI_Performance();
		void UI_Validation();
		void UI_Backends(const AudioEngineStats& engine);
		void UI_Playback(const AudioEngineStats& engine);
		void UI_Events();
		void UI_Acoustics();
		void UI_Reverb();
		void UI_Sources();
		void UI_Visualisation();

		// Pushes m_Visualisation to the simulation whenever it changes. Called every frame because
		// entering Play builds a new RaytracedAudioScene that has never seen these settings.
		void SyncVisualisationSettings();

	private:
		Ref<Scene> m_Context;
		AudioValidationReport m_Validation;
		bool m_HasValidation = false;
		std::string m_SourceSearch;
		std::string m_EventSearch;
		AudioVisualisationSettings m_Visualisation;
	};

}
