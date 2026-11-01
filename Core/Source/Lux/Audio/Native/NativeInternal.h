// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

// Private to the native backend (Audio/Native/). Engine code outside this folder talks to
// AudioEngine and AudioEventInstance; nothing outside it and the native tests may include this.

#include "Lux/Audio/AudioPerformanceSettings.h"
#include "Lux/Audio/Sound.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

struct ma_engine;

namespace Lux::NativeInternal {

	struct EngineConfig
	{
		bool NoDevice = false;     // render on demand with ma_engine_read_pcm_frames (the tests)
		uint32_t Channels = 0;     // 0 = the output device's; required with NoDevice
		uint32_t SampleRate = 0;   // 0 = the output device's; required with NoDevice
		AudioPerformanceSettings Performance;
	};
	// What AudioEngine::Init starts with: the active project's settings and the default output device.
	// Defined in NativeAudioEngineConfig.cpp; the native tests define their own (no device).
	EngineConfig GetEngineConfig();

	// Null before AudioEngine::Init succeeds and after Shutdown.
	ma_engine* GetEngine();

	// Resolves a playback reference: a Sound asset handle in decimal (AudioEventRef::GetReference) or
	// a Sound's path relative to the project's Assets directory. False with the reason in outError.
	// Defined in NativeSoundResolver.cpp against the asset manager; the native tests define their own.
	bool ResolveSound(const std::string& reference, Ref<Sound>& outSound, std::string& outError);
	// The absolute path of a clip (an Audio asset). Defined beside ResolveSound.
	bool ResolveClip(AssetHandle clip, std::filesystem::path& outPath, std::string& outError);

	// Seeds the generator Random containers and variations draw from (the tests make choices repeatable).
	void SeedRandom(uint32_t seed);

	// A decimal asset handle: digits only and nonzero.
	bool ParseHandle(const std::string& reference, uint64_t& outHandle);

	// The listeners AudioListener::Apply last set, with weights normalized to sum to 1. With no
	// active listener it holds one neutral listener at the origin, as the FMOD bridge does.
	struct Listener
	{
		uint32_t Index = 0;                 // the engine slot, which is also the miniaudio listener index
		float Weight = 1.0f;
		glm::vec3 AttenuationPoint{ 0.0f }; // the listener position, or its attenuation target
	};
	struct ListenerSet
	{
		std::array<Listener, 8> Listeners{};
		uint32_t Count = 1;
	};
	const ListenerSet& GetListeners();

	// Voices (AudioEventInstance) live on the main thread; the engine drives them through these.
	// Recomputes every voice's gain, pitch, filters and panning listener after a listener change.
	void RefreshVoices();
	// Publishes end-of-playback to playback status and the script mailbox.
	void PollVoices();
	int CountPlayingVoices();
	// Stops and frees every voice's miniaudio objects, before the engine is torn down. The
	// wrappers stay alive but invalid, exactly as FMOD's generation check leaves them.
	void ReleaseVoices();

	// Distance gain for one listener, as documented on SoundAttenuation.
	float Attenuate(const SoundSpatialSettings& spatial, float distance);
	// The occlusion low-pass cutoff for a low-band gain: 20 kHz at gain 1 (bypassed), falling one
	// octave per 6 dB of loss, floored at 125 Hz.
	float OcclusionCutoffHz(float occlusionGainLF);
}
