// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioAccessibility.h"
#include "Lux/Audio/AudioListener.h"
#include "Lux/Audio/AudioPerformance.h"
#include "Lux/Core/Thread.h"
#include "NativeInternal.h"
#include "Miniaudio.h"

#include <MiniaudioCpp/MiniaudioWrappers.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Lux {

	static_assert(MA_ENGINE_MAX_LISTENERS == AudioListener::MaxListeners, "one miniaudio listener per engine slot");

	namespace {

		ma_resource_manager s_ResourceManager;
		JPL::Engine s_Engine; // empty until Init; the wrapper's nodes reach it through JPL::GetMiniaudioEngine
		bool s_EngineReady = false;
		// Runs the resource manager's jobs, which only streamed Sounds post: reading the next pages of
		// a stream from disk, and opening and closing streams. Stopped by Shutdown, or at exit when the
		// process ends without it, since destroying a running thread terminates the program.
		struct StreamingThread
		{
			Scope<Thread> Worker;

			void Start()
			{
				Worker = CreateScope<Thread>("AudioStreaming");
				Worker->Dispatch([]
				{
					LUX_PROFILE_THREAD("Audio Streaming");
					// Blocks until a job is posted; the quit job Stop posts stays queued and ends the loop.
					while (ma_resource_manager_process_next_job(&s_ResourceManager) != MA_CANCELLED)
					{
					}
				});
			}

			void Stop()
			{
				if (!Worker)
					return;
				ma_resource_manager_post_job_quit(&s_ResourceManager);
				Worker->Join();
				Worker.reset();
			}

			~StreamingThread() { Stop(); }
		};
		StreamingThread s_Streaming;
		NativeInternal::ListenerSet s_Listeners;

		// The endpoint volume combines the master bus controls with the focus mute, so neither
		// overrides the other.
		float s_MasterVolume = 1.0f;
		bool s_MasterMuted = false;
		bool s_FocusMuted = false;
		std::unordered_set<std::string> s_ReportedBuses;
		bool s_ReportedBanks = false;

		const std::vector<AudioBankInfo> s_NoBanks;
		const std::vector<AudioEventInfo> s_NoEvents;

		void ApplyEndpointVolume()
		{
			if (!s_EngineReady)
				return;
			const ma_result result = ma_engine_set_volume(s_Engine.get(), s_MasterMuted || s_FocusMuted ? 0.0f : s_MasterVolume);
			if (result != MA_SUCCESS)
				LUX_CORE_ERROR_TAG("Audio", "Cannot set the master output volume: {}", ma_result_description(result));
		}

		// Phase 2 mixes everything through the master bus; authored buses arrive with the native mixer.
		bool IsMasterBus(const std::string& path)
		{
			if (path == "bus:/")
				return true;
			if (s_ReportedBuses.insert(path).second)
				LUX_CORE_WARN_TAG("Audio", "Bus '{}' does not exist: the native backend mixes every sound through bus:/ until it has a mixer", path);
			return false;
		}

		void ReportBanks()
		{
			if (!s_ReportedBanks)
			{
				s_ReportedBanks = true;
				LUX_CORE_WARN_TAG("Audio", "FMOD banks are not used by the native audio backend; assign Sound assets instead");
			}
		}

		bool Finite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}
	}

	ma_engine* NativeInternal::GetEngine()
	{
		return s_EngineReady ? s_Engine.get() : nullptr;
	}

	const NativeInternal::ListenerSet& NativeInternal::GetListeners()
	{
		return s_Listeners;
	}

	void AudioEngine::Init()
	{
		LUX_PROFILE_FUNCTION("AudioEngine::Init");

		if (s_HasInitializedAudioEngine)
			return;

		s_ShuttingDown = false;
		const auto config = NativeInternal::GetEngineConfig();
		if (!AudioPerformance::Configure(config.Performance))
			return;

		// No miniaudio job threads: a clip decodes synchronously on the calling (main) thread the first
		// time it plays, and stays registered until shutdown so instances and restarts share it.
		// Streamed Sounds post jobs that the engine's own streaming thread runs.
		ma_resource_manager_config resources = ma_resource_manager_config_init();
		resources.jobThreadCount = 0;
		// Decode to float, as ma_engine's own resource manager does: with clips kept in their file's
		// format (16-bit PCM, say) miniaudio 0.11.25 drops the last 192 frames of every clip.
		resources.decodedFormat = ma_format_f32;
		ma_result result = ma_resource_manager_init(&resources, &s_ResourceManager);
		if (result != MA_SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Audio", "Failed to initialize the native audio resource manager: {}", ma_result_description(result));
			return;
		}

		ma_engine_config engine = ma_engine_config_init();
		engine.pResourceManager = &s_ResourceManager;
		engine.listenerCount = AudioListener::MaxListeners;
		engine.noDevice = config.NoDevice ? MA_TRUE : MA_FALSE;
		engine.channels = config.Channels;
		engine.sampleRate = config.SampleRate;
		result = s_Engine.emplace(&engine);
		if (result != MA_SUCCESS)
		{
			Scope<ma_engine>(s_Engine.release()); // never initialized, so it must not be uninitialized
			LUX_CORE_ERROR_TAG("Audio", "Failed to initialize the native audio engine: {}", ma_result_description(result));
			ma_resource_manager_uninit(&s_ResourceManager);
			return;
		}
		JPL::GetMiniaudioEngine = [](void*) -> JPL::Engine& { return s_Engine; };
		// The wrapper allocates its nodes through these; empty callbacks make every allocation fail.
		JPL::gEngineAllocationCallbacks = s_Engine->allocationCallbacks;
		JPL::Trace = [](const char* message) { LUX_CORE_INFO_TAG("Audio", "MiniaudioCpp: {}", message); };
#if defined(JPL_ENABLE_ASSERTS) || defined(JPL_ENABLE_ENSURE)
		// Logged, never a breakpoint: the wrapper's checks report failures the caller then handles.
		JPL::McppAssertFailed = [](const char* expression, const char* message, const char* file, JPL::uint line)
		{
			LUX_CORE_ERROR_TAG("Audio", "MiniaudioCpp check failed: {} {} ({}:{})", expression, message ? message : "", file, line);
			return false;
		};
#endif
		s_Streaming.Start();

		s_EngineReady = true;
		s_HasInitializedAudioEngine = true;
		s_MasterVolume = 1.0f;
		s_MasterMuted = s_FocusMuted = false;
		s_ReportedBuses.clear();
		s_ReportedBanks = false;
		s_Listeners = {};
		for (uint32_t index = 0; index < AudioListener::MaxListeners; ++index)
			ma_engine_listener_set_enabled(s_Engine.get(), index, index == 0 ? MA_TRUE : MA_FALSE);
		ApplyEndpointVolume();

		// Logged on success too: "did audio come up, and with what" is the first question when a
		// project is silent.
		LUX_CORE_INFO_TAG("Audio", "Native audio (miniaudio {}) initialized ({} Hz, {} channels{})", MA_VERSION_STRING,
			ma_engine_get_sample_rate(s_Engine.get()), ma_engine_get_channels(s_Engine.get()), config.NoDevice ? ", no device" : "");
	}

	void AudioEngine::Shutdown()
	{
		LUX_PROFILE_FUNCTION("AudioEngine::Shutdown");

		s_ShuttingDown = true;
		s_HasInitializedAudioEngine = false;
		AudioPerformance::Reset();
		AudioAccessibility::ReleaseMixer();
		// Advance before freeing, so a wrapper someone still holds can never reach a freed voice.
		++s_EventGeneration;
		++s_BankRevision;
		// Closing a stream waits for the streaming thread to run its close job, so voices go first.
		NativeInternal::ReleaseVoices();
		if (s_EngineReady)
		{
			s_Streaming.Stop();
			s_Engine.clear();
			ma_resource_manager_uninit(&s_ResourceManager);
			s_EngineReady = false;
		}
	}

	AudioEngineStats AudioEngine::GetStats()
	{
		AudioEngineStats stats;
		stats.BackendName = "miniaudio";
		stats.Initialized = s_HasInitializedAudioEngine;
		if (!s_HasInitializedAudioEngine)
			return stats;
		stats.VersionMajor = MA_VERSION_MAJOR;
		stats.VersionMinor = MA_VERSION_MINOR;
		stats.VersionPatch = MA_VERSION_REVISION;
		stats.SampleRate = static_cast<int>(ma_engine_get_sample_rate(s_Engine.get()));
		// Mixer CPU and memory are not measured yet, so report them as unavailable rather than zero.
		stats.HasMixerStats = false;
		stats.PlayingEventInstances = NativeInternal::CountPlayingVoices();
		return stats;
	}

	void AudioEngine::Update()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!s_HasInitializedAudioEngine)
			return;
		NativeInternal::PollVoices();
		AudioPerformance::Update();
	}

	void AudioEngine::SetApplicationFocused(bool focused)
	{
		const bool muted = !focused && AudioPerformance::GetSettings().MuteWhenUnfocused;
		if (muted == s_FocusMuted)
			return;
		s_FocusMuted = muted;
		ApplyEndpointVolume();
	}

	bool AudioEngine::LoadBank(const std::filesystem::path&)
	{
		ReportBanks();
		return false;
	}

	bool AudioEngine::LoadBanks(const std::filesystem::path&)
	{
		ReportBanks();
		return false;
	}

	bool AudioEngine::LoadRuntimeBanks(const std::filesystem::path&, const AudioBankManifest& manifest)
	{
		if (!manifest.Validate())
			return false;
		if (!HasInitializedEngine())
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot start runtime audio: native audio initialization failed");
			return false;
		}
		// An FMOD export's banks are skipped, not fatal: the game runs, and its Sound assets play.
		if (!manifest.Banks.empty())
			ReportBanks();
		return true;
	}

	void AudioEngine::UnloadAllBanks()
	{
		// No banks to unload; keep the reset side effects callers rely on, and retry failed lookups.
		AudioPerformance::Reset();
		AudioAccessibility::ReleaseMixer();
		++s_BankRevision;
	}

	const std::vector<AudioBankInfo>& AudioEngine::GetLoadedBanks()
	{
		return s_NoBanks;
	}

	const std::vector<AudioEventInfo>& AudioEngine::GetEvents()
	{
		return s_NoEvents;
	}

	bool AudioEngine::SetBusMuted(const std::string& path, bool muted)
	{
		if (!IsMasterBus(path))
			return false;
		s_MasterMuted = muted;
		ApplyEndpointVolume();
		return true;
	}

	bool AudioEngine::SetVCAVolume(const std::string& path, float)
	{
		if (s_ReportedBuses.insert(path).second)
			LUX_CORE_WARN_TAG("Audio", "VCA '{}' does not exist: the native backend has no VCAs until it has a mixer", path);
		return false;
	}

	bool AudioEngine::SetGlobalParameter(const std::string& name, float)
	{
		if (s_ReportedBuses.insert("parameter:" + name).second)
			LUX_CORE_WARN_TAG("Audio", "Global parameter '{}' does not exist: the native backend has no global parameters until it has a mixer", name);
		return false;
	}

	float AudioEngine::GetGlobalParameter(const std::string& name)
	{
		if (s_ReportedBuses.insert("parameter:" + name).second)
			LUX_CORE_WARN_TAG("Audio", "Global parameter '{}' does not exist: the native backend has no global parameters until it has a mixer", name);
		return 0.0f;
	}

	bool AudioEngine::SetBusVolume(const std::string& busPath, float volume)
	{
		if (!std::isfinite(volume))
		{
			LUX_CORE_ERROR_TAG("Audio", "Bus volume must be finite");
			return false;
		}
		if (!IsMasterBus(busPath))
			return false;
		s_MasterVolume = std::max(volume, 0.0f);
		ApplyEndpointVolume();
		return true;
	}

	float AudioEngine::GetBusVolume(const std::string& busPath)
	{
		return busPath == "bus:/" ? s_MasterVolume : 0.0f;
	}

	bool AudioEngine::GetEventParameter(const std::string& eventGuid, const std::string& name, AudioEventParameterInfo& outInfo)
	{
		Ref<Sound> sound;
		std::string error;
		if (!NativeInternal::ResolveSound(eventGuid, sound, error))
			return false;
		const SoundParameter* parameter = sound->FindParameter(name);
		if (!parameter)
			return false;
		outInfo.Name = parameter->Name;
		outInfo.Minimum = parameter->Minimum;
		outInfo.Maximum = parameter->Maximum;
		outInfo.Default = parameter->Default;
		outInfo.Discrete = outInfo.Labeled = parameter->IsLabeled();
		return true;
	}

	bool AudioEngine::GetEventParameterLabel(const std::string& eventGuid, const std::string& name, int index,
		std::string& outLabel, std::string& outError)
	{
		Ref<Sound> sound;
		if (!NativeInternal::ResolveSound(eventGuid, sound, outError))
			return false;
		const SoundParameter* parameter = sound->FindParameter(name);
		if (!parameter || !parameter->IsLabeled())
		{
			outError = "the sound has no labelled parameter '" + name + "'";
			return false;
		}
		if (index < 0 || static_cast<size_t>(index) >= parameter->Labels.size())
		{
			outError = "label index " + std::to_string(index) + " is out of range";
			return false;
		}
		outLabel = parameter->Labels[static_cast<size_t>(index)];
		return true;
	}

	void AudioListener::Apply(const States& listeners)
	{
		LUX_PROFILE_FUNCTION("AudioListener::Apply");
		if (!s_EngineReady)
			return;

		const auto isActive = [](const AudioListenerState& state)
		{
			return std::isfinite(state.Weight) && state.Weight > 0.0f && Finite(state.Position) && Finite(state.Forward)
				&& Finite(state.Up) && Finite(state.Velocity) && (!state.UseAttenuationPosition || Finite(state.AttenuationPosition));
		};
		float totalWeight = 0.0f;
		for (const auto& listener : listeners)
		{
			if (isActive(listener))
				totalWeight += std::clamp(listener.Weight, 0.0f, 1.0f);
		}

		s_Listeners.Count = 0;
		for (uint32_t index = 0; index < MaxListeners; ++index)
		{
			const auto& state = listeners[index];
			const bool active = isActive(state);
			ma_engine_listener_set_enabled(s_Engine.get(), index, active ? MA_TRUE : MA_FALSE);
			if (!active)
				continue;
			ma_engine_listener_set_position(s_Engine.get(), index, state.Position.x, state.Position.y, state.Position.z);
			ma_engine_listener_set_direction(s_Engine.get(), index, state.Forward.x, state.Forward.y, state.Forward.z);
			ma_engine_listener_set_world_up(s_Engine.get(), index, state.Up.x, state.Up.y, state.Up.z);
			ma_engine_listener_set_velocity(s_Engine.get(), index, state.Velocity.x, state.Velocity.y, state.Velocity.z);
			auto& listener = s_Listeners.Listeners[s_Listeners.Count++];
			listener.Index = index;
			// Normalized like the FMOD bridge, so weights sum to 1 and a lone listener counts in full.
			listener.Weight = std::clamp(state.Weight, 0.0f, 1.0f) / totalWeight;
			listener.AttenuationPoint = state.UseAttenuationPosition ? state.AttenuationPosition : state.Position;
		}

		// No active listener: a neutral listener at the origin, so a deleted camera cannot leave a
		// stale position behind.
		if (s_Listeners.Count == 0)
		{
			ma_engine_listener_set_enabled(s_Engine.get(), 0, MA_TRUE);
			ma_engine_listener_set_position(s_Engine.get(), 0, 0.0f, 0.0f, 0.0f);
			ma_engine_listener_set_direction(s_Engine.get(), 0, 0.0f, 0.0f, -1.0f);
			ma_engine_listener_set_world_up(s_Engine.get(), 0, 0.0f, 1.0f, 0.0f);
			ma_engine_listener_set_velocity(s_Engine.get(), 0, 0.0f, 0.0f, 0.0f);
			s_Listeners.Listeners[0] = {};
			s_Listeners.Count = 1;
		}
		NativeInternal::RefreshVoices();
	}
}
