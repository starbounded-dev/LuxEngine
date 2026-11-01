// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioEventInstance.h"

#include "Lux/Audio/AudioAccessibility.h"
#include "Lux/Audio/AudioEngine.h"
#include "NativeInternal.h"
#include "Miniaudio.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace Lux {

	namespace {

		constexpr float kOpenCutoffHz = 20000.0f;   // at or above: the occlusion filter is bypassed
		constexpr float kMinCutoffHz = 125.0f;      // the engine's low band, where occlusion gain is measured
		constexpr float kMinPitch = 0.01f, kMaxPitch = 16.0f;
		constexpr size_t kNotificationCapacity = 4096;
		constexpr float kDenormal = 1.0e-20f;

		// The last stage of every voice, after miniaudio's spatializer: the voice's gain, ramped
		// across each block so listener movement and occlusion changes do not click, and a one-pole
		// low-pass for occlusion. One pole falls 6 dB per octave, the slope of the mass law through a
		// wall. Unoccluded at unity gain the node copies its input exactly.
		struct VoiceNode
		{
			ma_node_base Base;   // first: miniaudio passes &Base as the node
			std::atomic<float> TargetGain{ 1.0f };
			std::atomic<float> CutoffHz{ kOpenCutoffHz };
			// Audio thread only.
			float Gain = 1.0f;
			bool Primed = false;
			float SampleRate = 48000.0f;
			uint32_t Channels = 2;
			float State[MA_MAX_CHANNELS] = {};
		};
		static_assert(std::is_standard_layout_v<VoiceNode>, "VoiceNode is reached through its ma_node_base");

		void ProcessVoice(ma_node* node, const float** framesIn, ma_uint32*, float** framesOut, ma_uint32* frameCountOut)
		{
			auto& voice = *reinterpret_cast<VoiceNode*>(node);
			const ma_uint32 frames = *frameCountOut;
			const float* input = framesIn[0];
			float* output = framesOut[0];
			const uint32_t channels = voice.Channels;
			const float target = voice.TargetGain.load(std::memory_order_relaxed);
			// The first block starts at its gain: a voice set up quiet must not begin with a ramp from 1.
			const float start = voice.Primed ? voice.Gain : target;
			voice.Primed = true;
			const float step = frames > 0 ? (target - start) / static_cast<float>(frames) : 0.0f;
			const float cutoff = voice.CutoffHz.load(std::memory_order_relaxed);
			const bool filtered = cutoff < kOpenCutoffHz;
			const float pole = filtered ? std::exp(-2.0f * std::numbers::pi_v<float> * cutoff / voice.SampleRate) : 0.0f;
			for (ma_uint32 frame = 0; frame < frames; ++frame)
			{
				const float gain = start + step * static_cast<float>(frame + 1);
				for (uint32_t channel = 0; channel < channels; ++channel)
				{
					const float sample = input[frame * channels + channel];
					float& state = voice.State[channel];
					if (filtered)
					{
						state = (1.0f - pole) * sample + pole * state;
						if (std::abs(state) < kDenormal)
							state = 0.0f;
					}
					else
						state = sample; // bypassed: track the input, so switching the filter on is continuous
					output[frame * channels + channel] = state * gain;
				}
			}
			voice.Gain = target;
		}

		ma_node_vtable s_VoiceVTable = { ProcessVoice, nullptr, 1, 1, 0 };

		// Everything the engine reaches through NativeInternal. The wrapper's Impl is this, so the
		// free functions below never need the wrapper's private members.
		struct NativeVoice
		{
			ma_sound Sound;
			VoiceNode Output;
			bool NodeReady = false, SoundReady = false;

			SoundSpatialSettings Spatial;
			float BaseVolume = 1.0f, BasePitch = 1.0f;
			bool Loop = false;
			uint32_t ClipSampleRate = 0;
			ma_uint64 ClipFrames = 0;

			float InstanceVolume = 1.0f, InstancePitch = 1.0f;
			float OcclusionGain = 1.0f, ReverbSend = 0.0f;
			glm::vec3 Position{ 0.0f };
			int Priority = 128;
			bool Started = false;   // Start succeeded and the sound has not ended or been stopped
			bool Paused = false;    // script pause or scene pause
			uint64_t Token = 0;     // the playback token, once callbacks are configured

			bool Live() const { return NodeReady && SoundReady; }
		};

		struct CallbackState
		{
			uint64_t ScriptHandle = 0;
			AudioPlaybackStatus Playback;
		};
		// Main thread only: the native backend detects playback ending by polling miniaudio's atomic
		// end flag in AudioEngine::Update, so nothing here is written from the audio thread.
		std::unordered_map<uint64_t, CallbackState> s_CallbackStates;
		std::vector<AudioEventNotification> s_Notifications;
		size_t s_DroppedNotifications = 0;
		uint64_t s_NextCallbackToken = 1;
		std::vector<NativeVoice*> s_Voices;
		// Callers such as physics audio set parameters on every new instance; report each missing
		// parameter once per sound, not once per instance.
		std::unordered_set<std::string> s_ReportedParameters;

		bool ReportMissingParameter(const std::string& reference, const std::string& name)
		{
			if (s_ReportedParameters.insert(reference + '\n' + name).second)
				LUX_CORE_WARN_TAG("Audio", "Sound {0} has no parameter '{1}': Sounds have no parameters yet, so it is ignored", reference, name);
			return false;
		}

		// miniaudio has narrow and wide file APIs; this picks by the platform's path character type,
		// so Windows paths outside the ANSI code page still open.
		template<typename Char>
		ma_result InitSound(ma_engine* engine, const Char* path, ma_uint32 flags, ma_sound* sound)
		{
			if constexpr (std::is_same_v<Char, wchar_t>)
				return ma_sound_init_from_file_w(engine, path, flags, nullptr, nullptr, sound);
			else
				return ma_sound_init_from_file(engine, path, flags, nullptr, nullptr, sound);
		}

		void ReleaseVoice(NativeVoice& voice)
		{
			// Upstream first: uninitializing detaches, and waits for the audio thread to let go.
			if (voice.SoundReady)
				ma_sound_uninit(&voice.Sound);
			if (voice.NodeReady)
				ma_node_uninit(&voice.Output.Base, nullptr);
			voice.SoundReady = voice.NodeReady = false;
			voice.Started = false;
		}

		void RefreshVoice(NativeVoice& voice)
		{
			if (!voice.Live())
				return;
			float distanceGain = 1.0f;
			float occlusionGain = 1.0f;
			if (voice.Spatial.Enabled)
			{
				// Weights are normalized, so this is the FMOD rule (the closest listeners up to 100%)
				// with every active listener counted in full. Panning follows the listener that
				// contributes most, since miniaudio pans a sound to one listener.
				const auto& listeners = NativeInternal::GetListeners();
				distanceGain = 0.0f;
				float strongest = -1.0f;
				uint32_t panning = listeners.Listeners[0].Index;
				for (uint32_t i = 0; i < listeners.Count; ++i)
				{
					const auto& listener = listeners.Listeners[i];
					const float contribution = listener.Weight * NativeInternal::Attenuate(voice.Spatial, glm::distance(voice.Position, listener.AttenuationPoint));
					distanceGain += contribution;
					if (contribution > strongest)
					{
						strongest = contribution;
						panning = listener.Index;
					}
				}
				ma_sound_set_pinned_listener_index(&voice.Sound, panning);
				// Occlusion is positional, so a 2D voice is never muffled.
				occlusionGain = voice.OcclusionGain;
			}
			voice.Output.TargetGain.store(voice.BaseVolume * voice.InstanceVolume * distanceGain * occlusionGain, std::memory_order_relaxed);
			voice.Output.CutoffHz.store(NativeInternal::OcclusionCutoffHz(occlusionGain), std::memory_order_relaxed);
		}

		CallbackState* State(uint64_t token)
		{
			const auto it = token ? s_CallbackStates.find(token) : s_CallbackStates.end();
			return it != s_CallbackStates.end() ? &it->second : nullptr;
		}

		// Playback ended or was stopped: what FMOD's STOPPED callback reports.
		void MarkStopped(NativeVoice& voice)
		{
			voice.Started = false;
			auto* state = State(voice.Token);
			if (!state)
				return;
			state->Playback.Stopped = true;
			if (!state->ScriptHandle)
				return;
			if (s_Notifications.size() < kNotificationCapacity)
				s_Notifications.push_back({ state->ScriptHandle, true, {} });
			else
				++s_DroppedNotifications;
		}

		bool AtEnd(NativeVoice& voice)
		{
			return !voice.Loop && ma_sound_at_end(&voice.Sound);
		}

		ma_result ApplyPause(NativeVoice& voice)
		{
			// miniaudio's stop keeps the cursor, so it is a pause. Never resume a finished sound:
			// starting one at its end restarts it from the beginning.
			if (!voice.Live() || !voice.Started || AtEnd(voice))
				return MA_SUCCESS;
			return voice.Paused ? ma_sound_stop(&voice.Sound) : ma_sound_start(&voice.Sound);
		}
	}

	struct AudioEventInstance::Impl : NativeVoice
	{
	};

	float NativeInternal::Attenuate(const SoundSpatialSettings& spatial, float distance)
	{
		const float minimum = spatial.MinDistance;
		const float maximum = std::max(spatial.MaxDistance, minimum);
		const float clamped = std::clamp(distance, minimum, maximum);
		switch (spatial.Attenuation)
		{
			case SoundAttenuation::None:
				return 1.0f;
			case SoundAttenuation::Inverse:
				return minimum / (minimum + spatial.Rolloff * (clamped - minimum));
			case SoundAttenuation::Linear:
				return maximum > minimum ? std::clamp(1.0f - spatial.Rolloff * (clamped - minimum) / (maximum - minimum), 0.0f, 1.0f) : 1.0f;
		}
		return 1.0f;
	}

	float NativeInternal::OcclusionCutoffHz(float occlusionGainLF)
	{
		const float gain = std::isfinite(occlusionGainLF) ? std::clamp(occlusionGainLF, 0.0f, 1.0f) : 1.0f;
		return std::max(kMinCutoffHz, kOpenCutoffHz * gain);
	}

	void NativeInternal::RefreshVoices()
	{
		for (NativeVoice* voice : s_Voices)
			RefreshVoice(*voice);
	}

	void NativeInternal::PollVoices()
	{
		for (NativeVoice* voice : s_Voices)
		{
			if (voice->Live() && voice->Started && AtEnd(*voice))
				MarkStopped(*voice);
		}
	}

	int NativeInternal::CountPlayingVoices()
	{
		int playing = 0;
		for (const NativeVoice* voice : s_Voices)
			playing += voice->Live() && voice->Started;
		return playing;
	}

	void NativeInternal::ReleaseVoices()
	{
		for (NativeVoice* voice : s_Voices)
			ReleaseVoice(*voice);
	}

	AudioEventInstance::AudioEventInstance(Scope<Impl> impl, const std::string& guid, uint64_t generation)
		: m_Impl(std::move(impl)), m_Guid(guid), m_Generation(generation)
	{
	}

	Ref<AudioEventInstance> AudioEventInstance::Create(const std::string& eventGuid)
	{
		if (eventGuid.empty())
			return nullptr;
		ma_engine* engine = NativeInternal::GetEngine();
		if (!engine)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot create sound {0}: native audio is not initialized", eventGuid);
			return nullptr;
		}
		NativeInternal::ResolvedSound resolved;
		std::string error;
		if (!NativeInternal::ResolveSound(eventGuid, resolved, error))
		{
			// Names the reference: the usual cause is a scene pointing at a deleted or moved Sound.
			LUX_CORE_WARN_TAG("Audio", "Cannot play sound {0}: {1}", eventGuid, error);
			return nullptr;
		}
		const Sound& sound = *resolved.Asset;

		auto impl = CreateScope<Impl>();
		NativeVoice& voice = *impl;
		voice.Spatial = sound.Spatial;
		voice.BaseVolume = sound.Volume;
		voice.BasePitch = sound.Pitch;
		voice.Loop = sound.Loop;

		uint32_t channels = ma_engine_get_channels(engine);
		voice.Output.Channels = channels;
		voice.Output.SampleRate = static_cast<float>(ma_engine_get_sample_rate(engine));
		ma_node_config node = ma_node_config_init();
		node.vtable = &s_VoiceVTable;
		node.pInputChannels = &channels;
		node.pOutputChannels = &channels;
		ma_result result = ma_node_init(ma_engine_get_node_graph(engine), &node, nullptr, &voice.Output.Base);
		voice.NodeReady = result == MA_SUCCESS;
		if (result == MA_SUCCESS)
			result = ma_node_attach_output_bus(&voice.Output.Base, 0, ma_engine_get_endpoint(engine), 0);
		if (result == MA_SUCCESS)
		{
			// Decoded once into memory and shared by every instance of the clip while one is alive.
			const ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT
				| (sound.Spatial.Enabled ? 0u : static_cast<ma_uint32>(MA_SOUND_FLAG_NO_SPATIALIZATION));
			result = InitSound(engine, resolved.Clip.c_str(), flags, &voice.Sound);
			voice.SoundReady = result == MA_SUCCESS;
		}
		if (result == MA_SUCCESS)
			result = ma_node_attach_output_bus(&voice.Sound, 0, &voice.Output.Base, 0);
		if (result == MA_SUCCESS)
			result = ma_sound_get_data_format(&voice.Sound, nullptr, nullptr, &voice.ClipSampleRate, nullptr, 0);
		if (result == MA_SUCCESS)
			result = ma_sound_get_length_in_pcm_frames(&voice.Sound, &voice.ClipFrames);
		if (result != MA_SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Audio", "Failed to create sound {0} from '{1}': {2}", eventGuid, resolved.Clip.string(), ma_result_description(result));
			ReleaseVoice(voice);
			return nullptr;
		}

		ma_sound_set_looping(&voice.Sound, voice.Loop ? MA_TRUE : MA_FALSE);
		ma_sound_set_pitch(&voice.Sound, std::clamp(voice.BasePitch, kMinPitch, kMaxPitch));
		if (voice.Spatial.Enabled)
		{
			// The engine computes distance gain itself (attenuation targets, weighted listeners);
			// miniaudio only pans. Its model must not be "none", which skips panning too: with equal
			// minimum and maximum distances its inverse model returns exactly 1. No doppler until
			// Sounds author it.
			ma_sound_set_attenuation_model(&voice.Sound, ma_attenuation_model_inverse);
			ma_sound_set_min_distance(&voice.Sound, 1.0f);
			ma_sound_set_max_distance(&voice.Sound, 1.0f);
			ma_sound_set_doppler_factor(&voice.Sound, 0.0f);
		}
		RefreshVoice(voice);
		s_Voices.push_back(&voice);
		return Ref<AudioEventInstance>::Create(std::move(impl), std::to_string(static_cast<uint64_t>(sound.Handle)), AudioEngine::GetEventGeneration());
	}

	AudioEventInstance::~AudioEventInstance()
	{
		s_CallbackStates.erase(m_CallbackToken);
		if (!m_Impl)
			return;
		ReleaseVoice(*m_Impl);
		std::erase(s_Voices, static_cast<NativeVoice*>(m_Impl.get()));
	}

	bool AudioEventInstance::IsValid() const
	{
		// Do not touch a voice from a released engine, even to ask about it.
		return m_Impl && m_Impl->Live() && AudioEngine::HasInitializedEngine() && m_Generation == AudioEngine::GetEventGeneration();
	}

	bool AudioEventInstance::CheckResult(int result, const char* operation) const
	{
		if (result == MA_SUCCESS)
			return true;
		if (m_ReportedErrors.emplace(operation).second)
			LUX_CORE_ERROR_TAG("Audio", "Sound {0}: {1} failed: {2}", m_Guid, operation, ma_result_description(static_cast<ma_result>(result)));
		return false;
	}

	std::vector<AudioEventNotification> AudioEventInstance::DrainNotifications()
	{
		std::vector<AudioEventNotification> result;
		result.swap(s_Notifications);
		if (s_DroppedNotifications)
		{
			LUX_CORE_ERROR_TAG("Audio", "Audio notification queue overflow: {0} notifications dropped", s_DroppedNotifications);
			s_DroppedNotifications = 0;
		}
		return result;
	}

	bool AudioEventInstance::ConfigureCallbacks(uint64_t scriptHandle, bool timeline)
	{
		if (!IsValid())
			return false;
		if (timeline)
			return CheckResult(MA_NOT_IMPLEMENTED, "music timeline notifications (not supported by the native backend yet)");
		if (!m_CallbackToken)
		{
			if (!s_NextCallbackToken)
				return CheckResult(MA_OUT_OF_RANGE, "callback token space exhausted");
			m_CallbackToken = s_NextCallbackToken++;
		}
		auto& state = s_CallbackStates[m_CallbackToken];
		if (scriptHandle)
			state.ScriptHandle = scriptHandle;
		m_Impl->Token = m_CallbackToken;
		return true;
	}

	bool AudioEventInstance::SetCallbackHandle(uint64_t handle)
	{
		return ConfigureCallbacks(handle, false);
	}

	bool AudioEventInstance::EnableTimelineNotifications()
	{
		return ConfigureCallbacks(0, true);
	}

	std::vector<AudioTimelineNotification> AudioEventInstance::DrainTimelineNotifications()
	{
		return {};
	}

	uint64_t AudioEventInstance::GetTimelineSequence() const
	{
		return 0;
	}

	bool AudioEventInstance::MonitorPlayback()
	{
		return ConfigureCallbacks(0, false);
	}

	bool AudioEventInstance::SetProgrammerSound(const std::string&)
	{
		return CheckResult(MA_NOT_IMPLEMENTED, "programmer sound (dialogue clips are not supported by the native backend yet)");
	}

	AudioPlaybackStatus AudioEventInstance::GetPlaybackStatus() const
	{
		AudioPlaybackStatus result;
		if (const auto it = s_CallbackStates.find(m_CallbackToken); m_CallbackToken && it != s_CallbackStates.end())
			result = it->second.Playback;
		CheckResult(result.Error, "playback");
		return result;
	}

	bool AudioEventInstance::SetPriority(int priority)
	{
		if (priority < 0 || priority > 256)
			return CheckResult(MA_INVALID_ARGS, "priority must be in [0, 256]");
		if (!IsValid())
			return false;
		// Recorded for the voice budget; nothing is virtualized yet.
		m_Impl->Priority = priority;
		return true;
	}

	int AudioEventInstance::GetPriority() const
	{
		return IsValid() ? m_Impl->Priority : 128;
	}

	float AudioEventInstance::GetMaximumDistance() const
	{
		return IsValid() && m_Impl->Spatial.Enabled ? m_Impl->Spatial.MaxDistance : 0.0f;
	}

	bool AudioEventInstance::IsVirtual() const
	{
		return false;
	}

	bool AudioEventInstance::Is3D() const
	{
		return IsValid() && m_Impl->Spatial.Enabled;
	}

	bool AudioEventInstance::IsOneShot() const
	{
		return IsValid() && !m_Impl->Loop;
	}

	bool AudioEventInstance::IsSnapshot() const
	{
		return false;
	}

	bool AudioEventInstance::SetSnapshotIntensity(float)
	{
		return IsValid() && CheckResult(MA_NOT_IMPLEMENTED, "snapshot intensity (snapshots are not supported by the native backend yet)");
	}

	float AudioEventInstance::GetParameter(const std::string& name) const
	{
		if (IsValid())
			ReportMissingParameter(m_Guid, name);
		return 0.0f;
	}

	bool AudioEventInstance::SetParameterLabel(const std::string& name, const std::string&)
	{
		return IsValid() && ReportMissingParameter(m_Guid, name);
	}

	bool AudioEventInstance::SetParameter(const std::string& name, float)
	{
		return IsValid() && ReportMissingParameter(m_Guid, name);
	}

	int AudioEventInstance::GetTimelinePosition() const
	{
		if (!IsValid() || !m_Impl->ClipSampleRate)
			return 0;
		ma_uint64 cursor = 0;
		if (!CheckResult(ma_sound_get_cursor_in_pcm_frames(&m_Impl->Sound, &cursor), "get timeline position"))
			return 0;
		return static_cast<int>(cursor * 1000 / m_Impl->ClipSampleRate);
	}

	int AudioEventInstance::GetLength() const
	{
		return IsValid() && m_Impl->ClipSampleRate ? static_cast<int>(m_Impl->ClipFrames * 1000 / m_Impl->ClipSampleRate) : 0;
	}

	void AudioEventInstance::SetTimelinePosition(int milliseconds)
	{
		if (!IsValid() || !m_Impl->ClipSampleRate)
			return;
		ma_uint64 frame = static_cast<ma_uint64>(std::max(0, milliseconds)) * m_Impl->ClipSampleRate / 1000;
		if (m_Impl->ClipFrames)
			frame = m_Impl->Loop ? frame % m_Impl->ClipFrames : std::min(frame, m_Impl->ClipFrames);
		CheckResult(ma_sound_seek_to_pcm_frame(&m_Impl->Sound, frame), "set timeline position");
	}

	bool AudioEventInstance::Start()
	{
		if (!IsValid())
			return false;
		AudioAccessibility::Track(this);
		NativeVoice& voice = *m_Impl;
		auto* state = State(m_CallbackToken);
		if (state)
			state->Playback = {};
		// Like FMOD, starting again restarts from the beginning. A fresh voice keeps any position set
		// before it started.
		ma_result result = MA_SUCCESS;
		if (voice.Started || ma_sound_at_end(&voice.Sound))
			result = ma_sound_seek_to_pcm_frame(&voice.Sound, 0);
		if (result == MA_SUCCESS && !voice.Paused)
			result = ma_sound_start(&voice.Sound);
		if (result != MA_SUCCESS)
		{
			voice.Started = false;
			if (state)
				state->Playback.Error = result;
			return CheckResult(result, "start");
		}
		voice.Started = true;
		if (state)
		{
			state->Playback.Started = state->Playback.SoundStarted = true;
			state->Playback.Duration = voice.ClipSampleRate ? static_cast<float>(voice.ClipFrames) / static_cast<float>(voice.ClipSampleRate) : 0.0f;
		}
		return true;
	}

	void AudioEventInstance::Stop(bool)
	{
		// Sounds author no fade yet, so a fade-out stop is immediate too.
		if (!IsValid() || !m_Impl->Started)
			return;
		CheckResult(ma_sound_stop(&m_Impl->Sound), "stop");
		CheckResult(ma_sound_seek_to_pcm_frame(&m_Impl->Sound, 0), "rewind");
		MarkStopped(*m_Impl);
	}

	void AudioEventInstance::SetPaused(bool paused)
	{
		if (m_Paused == paused)
			return;
		m_Paused = paused;
		if (!m_Impl)
			return;
		m_Impl->Paused = m_Paused || m_ScenePaused;
		if (IsValid())
			CheckResult(ApplyPause(*m_Impl), "set pause");
	}

	void AudioEventInstance::SetScenePaused(bool paused)
	{
		if (m_ScenePaused == paused)
			return;
		m_ScenePaused = paused;
		if (!m_Impl)
			return;
		m_Impl->Paused = m_Paused || m_ScenePaused;
		if (IsValid())
			CheckResult(ApplyPause(*m_Impl), "set scene pause");
	}

	bool AudioEventInstance::IsPlaying() const
	{
		// A paused voice still counts, as a paused FMOD event does.
		return IsValid() && m_Impl->Started && !AtEnd(*m_Impl);
	}

	void AudioEventInstance::Set3DAttributes(const glm::vec3& position, const glm::vec3& velocity,
		const glm::vec3& forward, const glm::vec3& up)
	{
		if (!IsValid())
			return;
		const auto finite = [](const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
		if (!finite(position) || !finite(velocity) || !finite(forward) || !finite(up))
		{
			CheckResult(MA_INVALID_ARGS, "set 3D attributes");
			return;
		}
		NativeVoice& voice = *m_Impl;
		voice.Position = position;
		ma_sound_set_position(&voice.Sound, position.x, position.y, position.z);
		ma_sound_set_velocity(&voice.Sound, velocity.x, velocity.y, velocity.z);
		// Only cones read the direction, and Sounds author none yet; keep it valid regardless.
		const glm::vec3 direction = glm::dot(forward, forward) > 1.0e-8f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
		ma_sound_set_direction(&voice.Sound, direction.x, direction.y, direction.z);
		m_Position = position;
		RefreshVoice(voice);
	}

	void AudioEventInstance::SetVolume(float volume)
	{
		if (!IsValid())
			return;
		if (!std::isfinite(volume))
		{
			CheckResult(MA_INVALID_ARGS, "set volume");
			return;
		}
		m_Volume = std::max(volume, 0.0f);
		m_Impl->InstanceVolume = m_Volume;
		RefreshVoice(*m_Impl);
	}

	void AudioEventInstance::SetPitch(float pitch)
	{
		if (!IsValid())
			return;
		if (!std::isfinite(pitch))
		{
			CheckResult(MA_INVALID_ARGS, "set pitch");
			return;
		}
		m_Impl->InstancePitch = std::max(pitch, 0.0f);
		ma_sound_set_pitch(&m_Impl->Sound, std::clamp(m_Impl->BasePitch * m_Impl->InstancePitch, kMinPitch, kMaxPitch));
	}

	void AudioEventInstance::SetAcoustics(const AudioEventAcoustics& acoustics)
	{
		if (!IsValid())
			return;
		if (!std::isfinite(acoustics.OcclusionGainLF) || !std::isfinite(acoustics.ReverbSend))
		{
			CheckResult(MA_INVALID_ARGS, "set acoustics");
			return;
		}
		m_Impl->OcclusionGain = std::clamp(acoustics.OcclusionGainLF, 0.0f, 1.0f);
		// Kept for the reverb return the native mixer adds; nothing reads it yet.
		m_Impl->ReverbSend = std::clamp(acoustics.ReverbSend, 0.0f, 1.0f);
		RefreshVoice(*m_Impl);
	}
}
