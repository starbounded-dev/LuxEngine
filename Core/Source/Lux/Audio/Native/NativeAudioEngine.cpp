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
#include <extras/nodes/ma_reverb_node/ma_reverb_node.h>

#include <map>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Lux {

	static_assert(MA_ENGINE_MAX_LISTENERS == AudioListener::MaxListeners, "one miniaudio listener per engine slot");

	namespace {

		ma_resource_manager s_ResourceManager;
		JPL::Engine s_Engine; // empty until Init; the wrapper's nodes reach it through JPL::GetMiniaudioEngine
		bool s_EngineReady = false;

		// The mixer: one sound group per bus below the master (which is the engine endpoint), and the
		// reverb return. Built by Init from the project's mixer; voices are released before them.
		using GroupResource = JPL::Internal::CResource<ma_sound_group, ma_sound_group_init, JPL::Internal::impl::uninit<ma_sound_group_uninit>>;
		using ReverbResource = JPL::Internal::CResource<ma_reverb_node, ma_reverb_node_init, JPL::Internal::impl::uninit<ma_reverb_node_uninit>>;
		using PeakResource = JPL::Internal::CResource<ma_peak_node, ma_peak_node_init, JPL::Internal::impl::uninit<ma_peak_node_uninit>>;

		// A feed-forward compressor: a peak envelope (attack/release) above the threshold is reduced by
		// the ratio, then the makeup gain applies. Settings are fixed when the mixer is built.
		struct CompressorNode
		{
			ma_node_base Base;   // first: miniaudio passes &Base as the node
			float ThresholdDb = -18.0f, Slope = 0.75f, MakeupDb = 0.0f;
			float AttackCoefficient = 0.0f, ReleaseCoefficient = 0.0f;
			float Envelope = 0.0f; // audio thread only
			static constexpr ma_uint32 FLAGS = 0;

			void Process(JPL::ProcessCallbackData& data)
			{
				const ma_uint32 frames = data.GetOutputFrameCount();
				const auto input = data.GetInputBuffer(0);
				const float* in = input.data.data;
				float* out = data.GetOutputBuffer(0).data.data;
				const uint32_t channels = input.getNumChannels();
				for (ma_uint32 frame = 0; frame < frames; ++frame)
				{
					float level = 0.0f;
					for (uint32_t channel = 0; channel < channels; ++channel)
						level = std::max(level, std::abs(in[frame * channels + channel]));
					const float coefficient = level > Envelope ? AttackCoefficient : ReleaseCoefficient;
					Envelope = coefficient * Envelope + (1.0f - coefficient) * level;
					const float levelDb = 20.0f * std::log10(std::max(Envelope, 1.0e-9f));
					const float reductionDb = levelDb > ThresholdDb ? (ThresholdDb - levelDb) * Slope : 0.0f;
					const float gain = std::pow(10.0f, (reductionDb + MakeupDb) / 20.0f);
					for (uint32_t channel = 0; channel < channels; ++channel)
						out[frame * channels + channel] = in[frame * channels + channel] * gain;
				}
			}
		};

		// An echo: the dry signal passes at unity, and each repeat comes back DelaySeconds later at Wet,
		// fading by Feedback. (miniaudio's ma_delay either drops the dry signal or, started at once,
		// echoes nothing without feedback.) The buffer is sized before the node is attached.
		struct DelayNode
		{
			ma_node_base Base;   // first: miniaudio passes &Base as the node
			std::vector<float> Buffer; // frames x channels; audio thread only once attached
			size_t Cursor = 0;
			float Wet = 0.5f, Feedback = 0.3f;
			static constexpr ma_uint32 FLAGS = MA_NODE_FLAG_CONTINUOUS_PROCESSING | MA_NODE_FLAG_ALLOW_NULL_INPUT; // the tail outlives the input

			void Process(JPL::ProcessCallbackData& data)
			{
				const ma_uint32 frames = data.GetOutputFrameCount();
				const auto output = data.GetOutputBuffer(0);
				float* out = output.data.data;
				const uint32_t channels = output.getNumChannels();
				const float* in = data.IsNullInput() ? nullptr : data.GetInputBuffer(0).data.data;
				const size_t length = Buffer.size() / channels;
				for (ma_uint32 frame = 0; frame < frames; ++frame)
				{
					for (uint32_t channel = 0; channel < channels; ++channel)
					{
						const float sample = in ? in[frame * channels + channel] : 0.0f;
						float& stored = Buffer[Cursor * channels + channel];
						const float delayed = stored;
						out[frame * channels + channel] = sample + delayed * Wet;
						stored = sample + delayed * Feedback;
					}
					Cursor = (Cursor + 1) % length;
				}
			}
		};

		// One built effect stage; exactly one of its resources is set.
		struct BusEffect
		{
			JPL::Internal::LPFNode LowPass;
			JPL::Internal::HPFNode HighPass;
			PeakResource Peak;
			JPL::TBaseNode<DelayNode> Delay;
			JPL::TBaseNode<CompressorNode> Compressor;

			ma_node* Node()
			{
				if (LowPass) return LowPass.get();
				if (HighPass) return HighPass.get();
				if (Peak) return Peak.get();
				if (Delay) return Delay.get();
				return Compressor ? Compressor.get() : nullptr;
			}
		};

		struct MixerBus
		{
			GroupResource Group;
			std::vector<Scope<BusEffect>> Effects; // in chain order, between the group and its parent
			float Volume = 1.0f;
			bool Muted = false;
			bool ScriptVolume = false; // set by SetBusVolume: snapshots leave its volume alone until ClearBusVolume
		};

		// A resource whose init failed was never initialized, so it is freed without its uninit.
		template<typename Resource>
		ma_result Emplaced(Resource& resource, ma_result result)
		{
			if (result != MA_SUCCESS)
				Scope<std::remove_pointer_t<decltype(resource.get())>>(resource.release());
			return result;
		}

		Scope<BusEffect> CreateEffect(ma_engine* engine, const AudioMixerEffect& settings)
		{
			const ma_uint32 channels = ma_engine_get_channels(engine), sampleRate = ma_engine_get_sample_rate(engine);
			ma_node_graph* graph = ma_engine_get_node_graph(engine);
			const ma_allocation_callbacks* allocation = &JPL::gEngineAllocationCallbacks;
			auto effect = CreateScope<BusEffect>();
			ma_result result = MA_SUCCESS;
			switch (settings.Type)
			{
				case AudioMixerEffectType::LowPass:
				{
					const ma_lpf_node_config config = ma_lpf_node_config_init(channels, sampleRate, settings.FrequencyHz, 2);
					result = Emplaced(effect->LowPass, effect->LowPass.emplace(graph, &config, allocation));
					break;
				}
				case AudioMixerEffectType::HighPass:
				{
					const ma_hpf_node_config config = ma_hpf_node_config_init(channels, sampleRate, settings.FrequencyHz, 2);
					result = Emplaced(effect->HighPass, effect->HighPass.emplace(graph, &config, allocation));
					break;
				}
				case AudioMixerEffectType::PeakingEQ:
				{
					const ma_peak_node_config config = ma_peak_node_config_init(channels, sampleRate, settings.GainDb, settings.Q, settings.FrequencyHz);
					result = Emplaced(effect->Peak, effect->Peak.emplace(graph, &config, allocation));
					break;
				}
				case AudioMixerEffectType::Delay:
				{
					if (!effect->Delay.Init(JPL::NodeLayout().WithInputs(channels).WithOutputs(channels)))
					{
						result = MA_ERROR;
						break;
					}
					DelayNode& node = *effect->Delay;
					const auto frames = std::max<size_t>(static_cast<size_t>(settings.DelaySeconds * static_cast<float>(sampleRate)), 1);
					node.Buffer.assign(frames * channels, 0.0f);
					node.Wet = settings.Wet;
					node.Feedback = settings.Feedback;
					break;
				}
				case AudioMixerEffectType::Compressor:
				{
					if (!effect->Compressor.Init(JPL::NodeLayout().WithInputs(channels).WithOutputs(channels)))
					{
						result = MA_ERROR;
						break;
					}
					CompressorNode& node = *effect->Compressor;
					node.ThresholdDb = settings.ThresholdDb;
					node.Slope = 1.0f - 1.0f / settings.Ratio;
					node.MakeupDb = settings.MakeupDb;
					node.AttackCoefficient = std::exp(-1.0f / (settings.AttackMs * 0.001f * static_cast<float>(sampleRate)));
					node.ReleaseCoefficient = std::exp(-1.0f / (settings.ReleaseMs * 0.001f * static_cast<float>(sampleRate)));
					break;
				}
			}
			if (result != MA_SUCCESS)
			{
				LUX_CORE_ERROR_TAG("Audio", "Cannot create a {} effect: {}", AudioMixerSettings::EffectName(settings.Type), ma_result_description(result));
				return nullptr;
			}
			return effect;
		}
		std::map<std::string, MixerBus> s_Buses;
		ReverbResource s_Reverb;
		std::vector<AudioMixerSnapshot> s_SnapshotDefinitions;
		struct ActiveSnapshot
		{
			const void* Instance = nullptr;
			const AudioMixerSnapshot* Definition = nullptr;
			float Intensity = 0.0f;
			uint64_t Order = 0; // start order, which breaks priority ties
		};
		std::vector<ActiveSnapshot> s_ActiveSnapshots;
		struct VCA
		{
			std::vector<std::string> Buses;
			float Volume = 1.0f;
		};
		std::map<std::string, VCA> s_VCAs;
		uint64_t s_SnapshotOrder = 0;
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

		// A bus below the master, or null (reported once) when the mixer has no such bus.
		MixerBus* FindBus(const std::string& path)
		{
			if (const auto it = s_Buses.find(path); it != s_Buses.end() && it->second.Group)
				return &it->second;
			if (path != "bus:/" && s_ReportedBuses.insert(path).second)
				LUX_CORE_WARN_TAG("Audio", "Bus '{}' is not in the project's mixer; its sounds play through bus:/", path);
			return nullptr;
		}

		// The bus's own volume, moved toward the winning active snapshot's override by its intensity.
		void ApplyBusVolume(const std::string& path, MixerBus& bus)
		{
			float volume = bus.Volume;
			if (bus.ScriptVolume)
			{
				for (const auto& [vcaPath, vca] : s_VCAs)
					if (std::find(vca.Buses.begin(), vca.Buses.end(), path) != vca.Buses.end())
						volume *= vca.Volume;
				ma_sound_group_set_volume(bus.Group.get(), bus.Muted ? 0.0f : volume);
				return;
			}
			const ActiveSnapshot* winner = nullptr;
			const AudioMixerBus* winning = nullptr;
			for (const ActiveSnapshot& active : s_ActiveSnapshots)
			{
				if (active.Intensity <= 0.0f)
					continue;
				const auto& overrides = active.Definition->Buses;
				const auto it = std::find_if(overrides.begin(), overrides.end(), [&](const AudioMixerBus& entry) { return entry.Path == path; });
				if (it == overrides.end())
					continue;
				if (!winner || active.Definition->Priority > winner->Definition->Priority
					|| (active.Definition->Priority == winner->Definition->Priority && active.Order > winner->Order))
				{
					winner = &active;
					winning = &*it;
				}
			}
			if (winner)
				volume += (winning->Volume - volume) * winner->Intensity;
			for (const auto& [vcaPath, vca] : s_VCAs)
				if (std::find(vca.Buses.begin(), vca.Buses.end(), path) != vca.Buses.end())
					volume *= vca.Volume;
			ma_sound_group_set_volume(bus.Group.get(), bus.Muted ? 0.0f : volume);
		}

		void ApplyBusVolumes()
		{
			for (auto& [path, bus] : s_Buses)
				if (bus.Group)
					ApplyBusVolume(path, bus);
		}

		// Parents are listed before children (AudioMixerSettings::Validate), so each parent exists. A bus
		// that fails to build is reported, and its sounds and children play through the master instead.
		void BuildMixer(ma_engine* engine, const AudioMixerSettings& mixer)
		{
			std::string error;
			if (!mixer.Validate(&error))
			{
				LUX_CORE_ERROR_TAG("Audio", "Invalid mixer, using the master bus only: {}", error);
				return;
			}
			for (const AudioMixerBus& settings : mixer.Buses)
			{
				MixerBus& bus = s_Buses[settings.Path];
				const auto parent = s_Buses.find(AudioMixerSettings::ParentPath(settings.Path));
				ma_sound_group* parentGroup = parent != s_Buses.end() ? parent->second.Group.get() : nullptr;
				const ma_result result = bus.Group.emplace(engine, 0u, parentGroup);
				if (result != MA_SUCCESS)
				{
					Scope<ma_sound_group>(bus.Group.release()); // never initialized, so it must not be uninitialized
					LUX_CORE_ERROR_TAG("Audio", "Cannot create bus '{}': {}", settings.Path, ma_result_description(result));
					continue;
				}
				bus.Volume = settings.Volume;
				bus.Muted = settings.Muted;
				ApplyBusVolume(settings.Path, bus);
				// The chain sits between the bus and where it plays: group -> effects -> parent. Attaching
				// the group's output to the first stage replaces its attachment to the parent.
				ma_node* output = bus.Group.get();
				for (const AudioMixerEffect& effectSettings : settings.Effects)
				{
					if (effectSettings.Bypass)
						continue;
					Scope<BusEffect> effect = CreateEffect(engine, effectSettings);
					if (!effect)
						continue;
					if (ma_node_attach_output_bus(output, 0, effect->Node(), 0) == MA_SUCCESS)
					{
						output = effect->Node();
						bus.Effects.push_back(std::move(effect));
					}
				}
				if (output != bus.Group.get())
				{
					ma_node* destination = parentGroup ? static_cast<ma_node*>(parentGroup) : ma_engine_get_endpoint(engine);
					if (const ma_result attached = ma_node_attach_output_bus(output, 0, destination, 0); attached != MA_SUCCESS)
						LUX_CORE_ERROR_TAG("Audio", "Cannot connect bus '{}' effects: {}", settings.Path, ma_result_description(attached));
				}
			}
			s_SnapshotDefinitions = mixer.Snapshots;
			for (const AudioMixerVCA& vca : mixer.VCAs)
				s_VCAs[vca.Path].Buses = vca.Buses;
			// miniaudio's reverb (verblib, Freeverb-based) takes mono or stereo only.
			const ma_uint32 channels = ma_engine_get_channels(engine);
			if (channels > 2)
			{
				LUX_CORE_WARN_TAG("Audio", "No reverb return: it supports 1-2 output channels, the device has {}", channels);
				return;
			}
			ma_reverb_node_config config = ma_reverb_node_config_init(channels, ma_engine_get_sample_rate(engine));
			config.roomSize = mixer.Reverb.RoomSize;
			config.damping = mixer.Reverb.Damping;
			config.width = mixer.Reverb.Width;
			config.wetVolume = 1.0f;
			config.dryVolume = 0.0f; // a return: voices keep their dry signal on their own bus
			ma_result result = s_Reverb.emplace(ma_engine_get_node_graph(engine), &config, &JPL::gEngineAllocationCallbacks);
			if (result != MA_SUCCESS)
			{
				Scope<ma_reverb_node>(s_Reverb.release());
				LUX_CORE_ERROR_TAG("Audio", "Cannot create the reverb return: {}", ma_result_description(result));
				return;
			}
			result = ma_node_attach_output_bus(s_Reverb.get(), 0, ma_engine_get_endpoint(engine), 0);
			if (result == MA_SUCCESS)
				result = ma_node_set_output_bus_volume(s_Reverb.get(), 0, mixer.Reverb.Volume);
			if (result != MA_SUCCESS)
			{
				LUX_CORE_ERROR_TAG("Audio", "Cannot connect the reverb return: {}", ma_result_description(result));
				s_Reverb.clear();
			}
		}

		void ReleaseMixer()
		{
			s_ActiveSnapshots.clear();
			s_SnapshotDefinitions.clear();
			s_VCAs.clear();
			NativeInternal::ClearGlobalParameters();
			s_Reverb.clear();
			// Children before parents: a group detaches from its parent when it is uninitialized.
			for (auto it = s_Buses.rbegin(); it != s_Buses.rend(); ++it)
			{
				it->second.Group.clear();
				it->second.Effects.clear();
			}
			s_Buses.clear();
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

	ma_node* NativeInternal::GetBusInput(const std::string& path)
	{
		if (!s_EngineReady)
			return nullptr;
		if (MixerBus* bus = FindBus(path))
			return bus->Group.get();
		return ma_engine_get_endpoint(s_Engine.get());
	}

	ma_node* NativeInternal::GetReverbReturn()
	{
		return s_EngineReady ? s_Reverb.get() : nullptr;
	}

	bool NativeInternal::IsMixerSnapshot(uint64_t id)
	{
		return s_EngineReady && std::any_of(s_SnapshotDefinitions.begin(), s_SnapshotDefinitions.end(),
			[&](const AudioMixerSnapshot& snapshot) { return static_cast<uint64_t>(snapshot.ID) == id; });
	}

	void NativeInternal::SetSnapshotState(const void* instance, uint64_t id, bool active, float intensity)
	{
		const auto it = std::find_if(s_ActiveSnapshots.begin(), s_ActiveSnapshots.end(), [&](const ActiveSnapshot& entry) { return entry.Instance == instance; });
		if (!active)
		{
			if (it != s_ActiveSnapshots.end())
				s_ActiveSnapshots.erase(it);
		}
		else if (it != s_ActiveSnapshots.end())
			it->Intensity = intensity;
		else
		{
			const auto definition = std::find_if(s_SnapshotDefinitions.begin(), s_SnapshotDefinitions.end(),
				[&](const AudioMixerSnapshot& snapshot) { return static_cast<uint64_t>(snapshot.ID) == id; });
			if (definition == s_SnapshotDefinitions.end())
				return;
			s_ActiveSnapshots.push_back({ instance, &*definition, intensity, ++s_SnapshotOrder });
		}
		ApplyBusVolumes();
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
		BuildMixer(s_Engine.get(), config.Mixer);
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
			ReleaseMixer();
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
		if (path == "bus:/")
		{
			s_MasterMuted = muted;
			ApplyEndpointVolume();
			return true;
		}
		MixerBus* bus = FindBus(path);
		if (!bus)
			return false;
		bus->Muted = muted;
		ApplyBusVolume(path, *bus);
		return true;
	}

	bool AudioEngine::SetVCAVolume(const std::string& path, float volume)
	{
		if (!std::isfinite(volume))
		{
			LUX_CORE_ERROR_TAG("Audio", "VCA volume must be finite");
			return false;
		}
		const auto vca = s_VCAs.find(path);
		if (vca == s_VCAs.end())
		{
			if (s_ReportedBuses.insert(path).second)
				LUX_CORE_WARN_TAG("Audio", "VCA '{}' is not in the project's mixer", path);
			return false;
		}
		vca->second.Volume = std::max(volume, 0.0f);
		ApplyBusVolumes();
		return true;
	}

	// Global parameters need no declaration: any Sound parameter marked Global with this name reads it.
	bool AudioEngine::SetGlobalParameter(const std::string& name, float value)
	{
		if (!s_EngineReady || name.empty() || !std::isfinite(value))
			return false;
		NativeInternal::SetGlobalParameterValue(name, value);
		NativeInternal::RefreshVoices();
		return true;
	}

	float AudioEngine::GetGlobalParameter(const std::string& name)
	{
		return NativeInternal::GetGlobalParameterValue(name, 0.0f);
	}

	bool AudioEngine::SetBusVolume(const std::string& busPath, float volume)
	{
		if (!std::isfinite(volume))
		{
			LUX_CORE_ERROR_TAG("Audio", "Bus volume must be finite");
			return false;
		}
		if (busPath == "bus:/")
		{
			s_MasterVolume = std::max(volume, 0.0f);
			ApplyEndpointVolume();
			return true;
		}
		MixerBus* bus = FindBus(busPath);
		if (!bus)
			return false;
		bus->Volume = std::max(volume, 0.0f);
		bus->ScriptVolume = true;
		ApplyBusVolume(busPath, *bus);
		return true;
	}

	bool AudioEngine::ClearBusVolume(const std::string& busPath)
	{
		if (busPath == "bus:/")
		{
			s_MasterVolume = 1.0f;
			ApplyEndpointVolume();
			return true;
		}
		MixerBus* bus = FindBus(busPath);
		if (!bus)
			return false;
		// The project mixer's current volume, so an edit made since Init is the one restored.
		const auto mixer = NativeInternal::GetEngineConfig().Mixer;
		const auto settings = std::find_if(mixer.Buses.begin(), mixer.Buses.end(), [&](const AudioMixerBus& entry) { return entry.Path == busPath; });
		bus->Volume = settings != mixer.Buses.end() ? settings->Volume : 1.0f;
		bus->ScriptVolume = false;
		ApplyBusVolume(busPath, *bus);
		return true;
	}

	float AudioEngine::GetBusVolume(const std::string& busPath)
	{
		if (busPath == "bus:/")
			return s_MasterVolume;
		const MixerBus* bus = FindBus(busPath);
		return bus ? bus->Volume : 0.0f;
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
