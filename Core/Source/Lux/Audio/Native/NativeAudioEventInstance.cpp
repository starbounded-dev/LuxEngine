// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioEventInstance.h"

#include "Lux/Audio/AudioAccessibility.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioFileUtils.h"
#include "NativeInternal.h"
#include "Miniaudio.h"

#include <MiniaudioCpp/MiniaudioWrappers.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <numbers>
#include <random>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace Lux {

	namespace {

		constexpr float kOpenCutoffHz = 20000.0f;   // a low-pass at or above this is bypassed
		constexpr float kMinCutoffHz = 125.0f;      // the engine's low band, where occlusion gain is measured
		constexpr float kMinPitch = 0.01f, kMaxPitch = 16.0f;
		constexpr size_t kNotificationCapacity = 4096;
		constexpr float kDenormal = 1.0e-20f;
		constexpr uint32_t kMaxLeaves = 256;        // clips one start may play (Layers multiply)

		// The last stage of every clip a voice plays, after miniaudio's spatializer: the clip's gain,
		// ramped across each block so listener movement and parameter changes do not click, then a
		// one-pole low-pass (occlusion and LowPass curves) and a one-pole high-pass (HighPass curves).
		// One pole falls 6 dB per octave, the slope of the mass law through a wall. With both filters
		// off at unity gain the node copies its input exactly.
		struct VoiceNode
		{
			ma_node_base Base;   // first: miniaudio passes &Base as the node
			std::atomic<float> TargetGain{ 1.0f };
			std::atomic<float> LowPassHz{ kOpenCutoffHz };
			std::atomic<float> HighPassHz{ 0.0f };
			// Audio thread only.
			float Gain = 1.0f;
			bool Primed = false;
			float SampleRate = 48000.0f;
			uint32_t Channels = 2;
			float LowState[MA_MAX_CHANNELS] = {};
			float HighInput[MA_MAX_CHANNELS] = {};
			float HighState[MA_MAX_CHANNELS] = {};

			static constexpr ma_uint32 FLAGS = 0;
			void Process(JPL::ProcessCallbackData& data);
		};
		static_assert(std::is_standard_layout_v<VoiceNode>, "VoiceNode is reached through its ma_node_base");

		void VoiceNode::Process(JPL::ProcessCallbackData& data)
		{
			VoiceNode& voice = *this;
			const ma_uint32 frames = data.GetOutputFrameCount();
			const float* input = data.GetInputBuffer(0).data.data;
			float* output = data.GetOutputBuffer(0).data.data;
			const uint32_t channels = voice.Channels;
			const float target = voice.TargetGain.load(std::memory_order_relaxed);
			// The first block starts at its gain: a voice set up quiet must not begin with a ramp from 1.
			const float start = voice.Primed ? voice.Gain : target;
			voice.Primed = true;
			const float step = frames > 0 ? (target - start) / static_cast<float>(frames) : 0.0f;
			const float lowCutoff = voice.LowPassHz.load(std::memory_order_relaxed);
			const float highCutoff = voice.HighPassHz.load(std::memory_order_relaxed);
			const bool lowPass = lowCutoff < kOpenCutoffHz;
			const bool highPass = highCutoff > 0.0f;
			const float lowPole = lowPass ? std::exp(-2.0f * std::numbers::pi_v<float> * lowCutoff / voice.SampleRate) : 0.0f;
			const float highPole = highPass ? std::exp(-2.0f * std::numbers::pi_v<float> * highCutoff / voice.SampleRate) : 0.0f;
			for (ma_uint32 frame = 0; frame < frames; ++frame)
			{
				const float gain = start + step * static_cast<float>(frame + 1);
				for (uint32_t channel = 0; channel < channels; ++channel)
				{
					float sample = input[frame * channels + channel];
					float& low = voice.LowState[channel];
					if (lowPass)
					{
						low = (1.0f - lowPole) * sample + lowPole * low;
						if (std::abs(low) < kDenormal)
							low = 0.0f;
						sample = low;
					}
					else
						low = sample; // bypassed: track the input, so switching the filter on is continuous
					float& previous = voice.HighInput[channel];
					float& high = voice.HighState[channel];
					if (highPass)
					{
						high = highPole * (high + sample - previous);
						if (std::abs(high) < kDenormal)
							high = 0.0f;
						previous = sample;
						sample = high;
					}
					else
					{
						previous = sample;
						high = 0.0f;
					}
					output[frame * channels + channel] = sample * gain;
				}
			}
			voice.Gain = target;
		}

#ifdef LUX_PLATFORM_WINDOWS
		constexpr auto kInitSoundFromFile = ma_sound_init_from_file_w; // paths outside the ANSI code page
#else
		constexpr auto kInitSoundFromFile = ma_sound_init_from_file;
#endif
		using SoundResource = JPL::Internal::CResource<ma_sound, kInitSoundFromFile, JPL::Internal::impl::uninit<ma_sound_uninit>>;

		// One clip a voice is playing: its own sound and output stage, and the path of nodes from the
		// root to it, whose volumes, pitches and curves combine into its gain, pitch and filters.
		struct Leaf
		{
			// Members destroy in reverse: the sound (upstream) detaches before its output node.
			JPL::TBaseNode<VoiceNode> Output;
			SoundResource Sound;
			std::vector<uint32_t> Path;
			float VariationGain = 1.0f, VariationPitch = 1.0f;
			bool Loop = false;
			float StartOffset = 0.0f;
			uint32_t ClipSampleRate = 0;
			ma_uint64 ClipFrames = 0;
			float CurrentGain = 0.0f;   // last target gain, for the quietest-instance steal policy
		};

		// Everything the engine reaches through NativeInternal. The wrapper's Impl is this, so the
		// free functions below never need the wrapper's private members.
		struct NativeVoice
		{
			Ref<Sound> Definition;               // a private copy: edits to the asset apply to the next instance
			uint64_t SoundHandle = 0;            // the asset's handle; 0 for an in-memory Sound
			std::vector<float> ParameterValues;  // indexed like Definition->Parameters
			std::vector<Scope<Leaf>> Leaves;     // what the last start resolved to
			bool Ready = false;                  // created against a live engine and not yet released

			float InstanceVolume = 1.0f, InstancePitch = 1.0f;
			float OcclusionGain = 1.0f, ReverbSend = 0.0f, CurveSend = 0.0f;
			glm::vec3 Position{ 0.0f }, Velocity{ 0.0f }, Direction{ 0.0f, 0.0f, -1.0f };
			int Priority = 128;
			int PendingTimeline = -1;            // milliseconds to apply at the next start
			int CachedLength = -1;               // milliseconds; -1 until known
			uint64_t StartOrder = 0;             // increases with every start, for the oldest-instance steal policy
			bool Started = false;                // Start succeeded and the sound has not ended or been stopped
			bool Paused = false;                 // script pause or scene pause
			uint64_t Token = 0;                  // the playback token, once callbacks are configured

			bool Live() const { return Ready; }
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
		std::unordered_set<std::string> s_ReportedProblems;
		std::mt19937 s_Random{ 0x4C75584Du };
		// Per Sound and node: the child a Random chose last (avoid-repeat) and a Sequence's next step.
		std::map<std::pair<uint64_t, uint32_t>, uint32_t> s_NodeState;
		// Per Sound: when it last started, for its cooldown. A steady clock, not miniaudio's engine time,
		// which stops while nothing is playing.
		std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> s_LastStart;
		uint64_t s_StartOrder = 0;
		// Clips decoded into memory stay registered with the resource manager until the engine shuts
		// down, so restarting a voice or creating another instance never decodes the file again.
		std::set<std::filesystem::path> s_DecodedClips;

		bool ReportOnce(const std::string& key, const std::string& message)
		{
			if (s_ReportedProblems.insert(key).second)
				LUX_CORE_WARN_TAG("Audio", "{}", message);
			return false;
		}

		float Uniform()
		{
			return std::uniform_real_distribution<float>(0.0f, 1.0f)(s_Random);
		}

		template<typename Char>
		ma_result RegisterClip(ma_resource_manager* resources, const Char* path)
		{
			if constexpr (std::is_same_v<Char, wchar_t>)
				return ma_resource_manager_register_file_w(resources, path, MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
			else
				return ma_resource_manager_register_file(resources, path, MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
		}

		template<typename Char>
		void UnregisterClip(ma_resource_manager* resources, const Char* path)
		{
			if constexpr (std::is_same_v<Char, wchar_t>)
				ma_resource_manager_unregister_file_w(resources, path);
			else
				ma_resource_manager_unregister_file(resources, path);
		}

		void ReleaseLeaf(Leaf& leaf)
		{
			// Upstream first: uninitializing detaches, and waits for the audio thread to let go.
			leaf.Sound.clear();
			leaf.Output.clear();
		}

		void ReleaseLeaves(NativeVoice& voice)
		{
			for (auto& leaf : voice.Leaves)
				ReleaseLeaf(*leaf);
			voice.Leaves.clear();
		}

		bool LeafAtEnd(Leaf& leaf)
		{
			return !leaf.Loop && ma_sound_at_end(leaf.Sound.get());
		}

		bool AllAtEnd(NativeVoice& voice)
		{
			return std::all_of(voice.Leaves.begin(), voice.Leaves.end(), [](const Scope<Leaf>& leaf) { return LeafAtEnd(*leaf); });
		}

		float ParameterValue(const NativeVoice& voice, const std::string& name)
		{
			const SoundParameter* parameter = voice.Definition->FindParameter(name);
			return parameter ? voice.ParameterValues[static_cast<size_t>(parameter - voice.Definition->Parameters.data())] : 0.0f;
		}

		void RefreshLeaf(NativeVoice& voice, Leaf& leaf, float distanceGain, uint32_t panning)
		{
			const Sound& sound = *voice.Definition;
			float gain = sound.Volume * leaf.VariationGain;
			float pitch = sound.Pitch * leaf.VariationPitch;
			float lowPass = kOpenCutoffHz, highPass = 0.0f, send = 0.0f;
			for (const uint32_t index : leaf.Path)
			{
				const SoundNode& node = sound.Nodes[index];
				gain *= node.Volume;
				pitch *= node.Pitch;
				for (const auto& curve : node.Curves)
				{
					const float value = curve.Evaluate(ParameterValue(voice, curve.Parameter));
					switch (curve.Target)
					{
						case SoundCurveTarget::Volume:   gain *= value; break;
						case SoundCurveTarget::Pitch:    pitch *= value; break;
						case SoundCurveTarget::LowPass:  lowPass = std::min(lowPass, value); break;
						case SoundCurveTarget::HighPass: highPass = std::max(highPass, value); break;
						case SoundCurveTarget::Send:     send = std::max(send, value); break;
					}
				}
			}
			voice.CurveSend = std::max(voice.CurveSend, send);
			// Occlusion is positional, so a 2D voice is never muffled.
			const float occlusion = sound.Spatial.Enabled ? voice.OcclusionGain : 1.0f;
			if (sound.Spatial.Enabled)
			{
				lowPass = std::min(lowPass, NativeInternal::OcclusionCutoffHz(occlusion));
				ma_sound_set_pinned_listener_index(leaf.Sound.get(), panning);
			}
			leaf.CurrentGain = gain * voice.InstanceVolume * distanceGain * occlusion;
			leaf.Output->TargetGain.store(leaf.CurrentGain, std::memory_order_relaxed);
			leaf.Output->LowPassHz.store(lowPass, std::memory_order_relaxed);
			leaf.Output->HighPassHz.store(highPass, std::memory_order_relaxed);
			ma_sound_set_pitch(leaf.Sound.get(), std::clamp(pitch * voice.InstancePitch, kMinPitch, kMaxPitch));
		}

		void RefreshVoice(NativeVoice& voice)
		{
			if (!voice.Live() || voice.Leaves.empty())
				return;
			float distanceGain = 1.0f;
			uint32_t panning = 0;
			if (voice.Definition->Spatial.Enabled)
			{
				// Weights are normalized, so this is the FMOD rule (the closest listeners up to 100%)
				// with every active listener counted in full. Panning follows the listener that
				// contributes most, since miniaudio pans a sound to one listener.
				const auto& listeners = NativeInternal::GetListeners();
				distanceGain = 0.0f;
				float strongest = -1.0f;
				panning = listeners.Listeners[0].Index;
				for (uint32_t i = 0; i < listeners.Count; ++i)
				{
					const auto& listener = listeners.Listeners[i];
					const float contribution = listener.Weight * NativeInternal::Attenuate(voice.Definition->Spatial, glm::distance(voice.Position, listener.AttenuationPoint));
					distanceGain += contribution;
					if (contribution > strongest)
					{
						strongest = contribution;
						panning = listener.Index;
					}
				}
			}
			voice.CurveSend = 0.0f;
			for (auto& leaf : voice.Leaves)
				RefreshLeaf(voice, *leaf, distanceGain, panning);
		}

		void PlaceLeaf(const NativeVoice& voice, Leaf& leaf)
		{
			ma_sound_set_position(leaf.Sound.get(), voice.Position.x, voice.Position.y, voice.Position.z);
			ma_sound_set_velocity(leaf.Sound.get(), voice.Velocity.x, voice.Velocity.y, voice.Velocity.z);
			ma_sound_set_direction(leaf.Sound.get(), voice.Direction.x, voice.Direction.y, voice.Direction.z);
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

		ma_result ApplyPause(NativeVoice& voice)
		{
			// miniaudio's stop keeps the cursor, so it is a pause. Never resume a finished clip:
			// starting one at its end restarts it from the beginning.
			if (!voice.Live() || !voice.Started)
				return MA_SUCCESS;
			ma_result result = MA_SUCCESS;
			for (auto& leaf : voice.Leaves)
			{
				if (LeafAtEnd(*leaf))
					continue;
				const ma_result applied = voice.Paused ? ma_sound_stop(leaf->Sound.get()) : ma_sound_start(leaf->Sound.get());
				if (applied != MA_SUCCESS)
					result = applied;
			}
			return result;
		}

		// Resolves the tree below a node into the clips one start plays, appending to outPaths. Random,
		// Sequence and Switch choose one child; Layer plays them all.
		bool Choose(NativeVoice& voice, uint32_t index, std::vector<uint32_t>& path, std::vector<std::vector<uint32_t>>& outPaths, std::string& outError)
		{
			const Sound& sound = *voice.Definition;
			const SoundNode& node = sound.Nodes[index];
			if (path.size() >= Sound::MaxDepth)
			{
				outError = "its tree is deeper than 64 levels";
				return false;
			}
			path.push_back(index);
			bool chosen = true;
			const auto stateKey = std::make_pair(voice.SoundHandle, index);
			switch (node.Type)
			{
				case SoundNodeType::Clip:
				{
					if (!node.Clip)
					{
						outError = "node " + std::to_string(index) + " has no audio clip assigned";
						chosen = false;
					}
					else if (outPaths.size() >= kMaxLeaves)
					{
						outError = "one start would play more than 256 clips";
						chosen = false;
					}
					else
						outPaths.push_back(path);
					break;
				}
				case SoundNodeType::Layer:
				{
					if (node.Children.empty())
					{
						outError = "layer node " + std::to_string(index) + " has no children";
						chosen = false;
					}
					for (const uint32_t child : node.Children)
						chosen = chosen && Choose(voice, child, path, outPaths, outError);
					break;
				}
				case SoundNodeType::Random:
				{
					std::vector<uint32_t> candidates;
					float total = 0.0f;
					const auto last = s_NodeState.find(stateKey);
					for (const uint32_t child : node.Children)
					{
						if (sound.Nodes[child].Weight <= 0.0f)
							continue;
						candidates.push_back(child);
					}
					if (node.AvoidRepeat && candidates.size() > 1 && last != s_NodeState.end())
						std::erase(candidates, last->second);
					for (const uint32_t child : candidates)
						total += sound.Nodes[child].Weight;
					if (candidates.empty())
					{
						outError = "random node " + std::to_string(index) + " has no child with a weight above 0";
						chosen = false;
						break;
					}
					float pick = Uniform() * total;
					uint32_t picked = candidates.back();
					for (const uint32_t child : candidates)
					{
						pick -= sound.Nodes[child].Weight;
						if (pick < 0.0f)
						{
							picked = child;
							break;
						}
					}
					s_NodeState[stateKey] = picked;
					chosen = Choose(voice, picked, path, outPaths, outError);
					break;
				}
				case SoundNodeType::Sequence:
				{
					if (node.Children.empty())
					{
						outError = "sequence node " + std::to_string(index) + " has no children";
						chosen = false;
						break;
					}
					uint32_t& step = s_NodeState[stateKey];
					const uint32_t child = node.Children[step % node.Children.size()];
					step = static_cast<uint32_t>((step + 1) % node.Children.size());
					chosen = Choose(voice, child, path, outPaths, outError);
					break;
				}
				case SoundNodeType::Switch:
				{
					if (node.Children.empty() || node.SwitchParameter.empty())
					{
						outError = "switch node " + std::to_string(index) + " needs children and a labelled parameter";
						chosen = false;
						break;
					}
					const float value = ParameterValue(voice, node.SwitchParameter);
					const auto slot = static_cast<size_t>(std::clamp(std::lround(value), 0l, static_cast<long>(node.Children.size() - 1)));
					chosen = Choose(voice, node.Children[slot], path, outPaths, outError);
					break;
				}
			}
			path.pop_back();
			return chosen;
		}

		float VariationGain(float decibels)
		{
			// Each start lowers the volume by up to the variation, never raises it.
			return decibels > 0.0f ? std::pow(10.0f, -Uniform() * decibels / 20.0f) : 1.0f;
		}

		float VariationPitch(float semitones)
		{
			return semitones > 0.0f ? std::exp2((Uniform() * 2.0f - 1.0f) * semitones / 12.0f) : 1.0f;
		}

		ma_result CreateLeaf(ma_engine* engine, const NativeVoice& voice, const std::vector<uint32_t>& path, Leaf& leaf, std::string& outError)
		{
			const Sound& sound = *voice.Definition;
			const SoundNode& clip = sound.Nodes[path.back()];
			leaf.Path = path;
			leaf.Loop = clip.Loop;
			leaf.StartOffset = clip.StartOffset;
			for (const uint32_t index : path)
			{
				leaf.VariationGain *= VariationGain(sound.Nodes[index].VolumeVariation);
				leaf.VariationPitch *= VariationPitch(sound.Nodes[index].PitchVariation);
			}
			std::filesystem::path file;
			if (!NativeInternal::ResolveClip(clip.Clip, file, outError))
				return MA_DOES_NOT_EXIST;
			if (!sound.Stream && !s_DecodedClips.contains(file))
			{
				const ma_result registered = RegisterClip(ma_engine_get_resource_manager(engine), file.c_str());
				if (registered != MA_SUCCESS)
				{
					outError = "cannot decode '" + file.string() + "': " + ma_result_description(registered);
					return registered;
				}
				s_DecodedClips.insert(file);
			}

			const uint32_t channels = ma_engine_get_channels(engine);
			ma_result result = leaf.Output.Init(JPL::NodeLayout().WithInputs(channels).WithOutputs(channels)) ? MA_SUCCESS : MA_ERROR;
			if (result == MA_SUCCESS)
			{
				// Set before attaching: the audio thread cannot reach the node until then.
				leaf.Output->Channels = channels;
				leaf.Output->SampleRate = static_cast<float>(ma_engine_get_sample_rate(engine));
				result = ma_node_attach_output_bus(leaf.Output.get(), 0, ma_engine_get_endpoint(engine), 0);
			}
			if (result == MA_SUCCESS)
			{
				// Decoded into memory and shared while any instance of the clip lives, or streamed from
				// disk in pages by the engine's streaming thread.
				const ma_uint32 flags = (sound.Stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE) | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT
					| (sound.Spatial.Enabled ? 0u : static_cast<ma_uint32>(MA_SOUND_FLAG_NO_SPATIALIZATION));
				result = leaf.Sound.emplace(engine, file.c_str(), flags, nullptr, nullptr);
				if (result != MA_SUCCESS)
					Scope<ma_sound>(leaf.Sound.release()); // never initialized, so it must not be uninitialized
			}
			if (result == MA_SUCCESS)
				result = ma_node_attach_output_bus(leaf.Sound.get(), 0, leaf.Output.get(), 0);
			if (result == MA_SUCCESS)
				result = ma_sound_get_data_format(leaf.Sound.get(), nullptr, nullptr, &leaf.ClipSampleRate, nullptr, 0);
			if (result == MA_SUCCESS)
				result = ma_sound_get_length_in_pcm_frames(leaf.Sound.get(), &leaf.ClipFrames);
			if (result != MA_SUCCESS)
			{
				outError = "cannot open '" + file.string() + "': " + ma_result_description(result);
				return result;
			}
			const auto frameAt = [&](float seconds)
			{
				return std::min(static_cast<ma_uint64>(static_cast<double>(seconds) * leaf.ClipSampleRate), leaf.ClipFrames);
			};
			if (clip.Loop && clip.HasLoopRegion)
			{
				const ma_uint64 begin = frameAt(clip.LoopStart), end = frameAt(clip.LoopEnd);
				if (begin < end)
					result = ma_data_source_set_loop_point_in_pcm_frames(ma_sound_get_data_source(leaf.Sound.get()), begin, end);
			}
			if (result == MA_SUCCESS && clip.StartOffset > 0.0f)
				result = ma_sound_seek_to_pcm_frame(leaf.Sound.get(), frameAt(clip.StartOffset));
			if (result != MA_SUCCESS)
			{
				outError = std::string("cannot set the clip's loop region or start offset: ") + ma_result_description(result);
				return result;
			}
			ma_sound_set_looping(leaf.Sound.get(), clip.Loop ? MA_TRUE : MA_FALSE);
			if (sound.Spatial.Enabled)
			{
				// The engine computes distance gain itself (attenuation targets, weighted listeners);
				// miniaudio only pans. Its model must not be "none", which skips panning too: with equal
				// minimum and maximum distances its inverse model returns exactly 1. No doppler until
				// Sounds author it.
				ma_sound_set_attenuation_model(leaf.Sound.get(), ma_attenuation_model_inverse);
				ma_sound_set_min_distance(leaf.Sound.get(), 1.0f);
				ma_sound_set_max_distance(leaf.Sound.get(), 1.0f);
				ma_sound_set_doppler_factor(leaf.Sound.get(), 0.0f);
			}
			return MA_SUCCESS;
		}

		int LeafMilliseconds(const Leaf& leaf, ma_uint64 frames)
		{
			return leaf.ClipSampleRate ? static_cast<int>(frames * 1000 / leaf.ClipSampleRate) : 0;
		}

		void SeekLeaf(Leaf& leaf, int milliseconds)
		{
			if (!leaf.ClipSampleRate)
				return;
			ma_uint64 frame = static_cast<ma_uint64>(std::max(0, milliseconds)) * leaf.ClipSampleRate / 1000;
			if (leaf.ClipFrames)
				frame = leaf.Loop ? frame % leaf.ClipFrames : std::min(frame, leaf.ClipFrames);
			ma_sound_seek_to_pcm_frame(leaf.Sound.get(), frame);
		}

		// The longest clip the sound can play, in milliseconds, from the files' headers.
		int ClipLengthMilliseconds(const Sound& sound)
		{
			double longest = 0.0;
			for (const AssetHandle clip : sound.GetClips())
			{
				std::filesystem::path file;
				std::string error;
				if (!NativeInternal::ResolveClip(clip, file, error))
					continue;
				if (const auto info = AudioFileUtils::GetFileInfo(file))
					longest = std::max(longest, info->Duration);
			}
			return static_cast<int>(longest * 1000.0);
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

	void NativeInternal::SeedRandom(uint32_t seed)
	{
		s_Random.seed(seed);
		s_NodeState.clear();
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
			if (voice->Live() && voice->Started && AllAtEnd(*voice))
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
		{
			ReleaseLeaves(*voice);
			voice->Ready = voice->Started = false;
		}
		if (ma_engine* engine = NativeInternal::GetEngine())
		{
			for (const auto& clip : s_DecodedClips)
				UnregisterClip(ma_engine_get_resource_manager(engine), clip.c_str());
		}
		s_DecodedClips.clear();
		s_LastStart.clear();
	}

	AudioEventInstance::AudioEventInstance(Scope<Impl> impl, const std::string& guid, uint64_t generation)
		: m_Impl(std::move(impl)), m_Guid(guid), m_Generation(generation)
	{
	}

	Ref<AudioEventInstance> AudioEventInstance::Create(const std::string& eventGuid)
	{
		if (eventGuid.empty())
			return nullptr;
		if (!NativeInternal::GetEngine())
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot create sound {0}: native audio is not initialized", eventGuid);
			return nullptr;
		}
		Ref<Sound> sound;
		std::string error;
		if (!NativeInternal::ResolveSound(eventGuid, sound, error))
		{
			// Names the reference: the usual cause is a scene pointing at a deleted or moved Sound.
			LUX_CORE_WARN_TAG("Audio", "Cannot play sound {0}: {1}", eventGuid, error);
			return nullptr;
		}
		return Create(sound);
	}

	Ref<AudioEventInstance> AudioEventInstance::Create(const Ref<Sound>& sound)
	{
		if (!sound)
			return nullptr;
		if (!NativeInternal::GetEngine())
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot create a sound: native audio is not initialized");
			return nullptr;
		}
		std::string error;
		if (!sound->Validate(&error))
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot play sound {0}: {1}", static_cast<uint64_t>(sound->Handle), error);
			return nullptr;
		}
		auto impl = CreateScope<Impl>();
		NativeVoice& voice = *impl;
		voice.Definition = Ref<Sound>::Create(*sound);
		voice.SoundHandle = static_cast<uint64_t>(sound->Handle);
		for (const auto& parameter : sound->Parameters)
			voice.ParameterValues.push_back(parameter.Default);
		voice.Ready = true;
		s_Voices.push_back(&voice);
		return Ref<AudioEventInstance>::Create(std::move(impl), std::to_string(voice.SoundHandle), AudioEngine::GetEventGeneration());
	}

	AudioEventInstance::~AudioEventInstance()
	{
		s_CallbackStates.erase(m_CallbackToken);
		if (!m_Impl)
			return;
		ReleaseLeaves(*m_Impl);
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
		return IsValid() && m_Impl->Definition->Spatial.Enabled ? m_Impl->Definition->Spatial.MaxDistance : 0.0f;
	}

	bool AudioEventInstance::IsVirtual() const
	{
		return false;
	}

	bool AudioEventInstance::Is3D() const
	{
		return IsValid() && m_Impl->Definition->Spatial.Enabled;
	}

	bool AudioEventInstance::IsOneShot() const
	{
		return IsValid() && !m_Impl->Definition->Loops();
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
		if (!IsValid())
			return 0.0f;
		if (!m_Impl->Definition->FindParameter(name))
		{
			ReportOnce(m_Guid + '\n' + name, "Sound " + m_Guid + " has no parameter '" + name + "'");
			return 0.0f;
		}
		return ParameterValue(*m_Impl, name);
	}

	bool AudioEventInstance::SetParameterLabel(const std::string& name, const std::string& label)
	{
		if (!IsValid())
			return false;
		const auto* parameter = m_Impl->Definition->FindParameter(name);
		if (!parameter || !parameter->IsLabeled())
			return ReportOnce(m_Guid + '\n' + name, "Sound " + m_Guid + " has no labelled parameter '" + name + "'");
		const auto it = std::find(parameter->Labels.begin(), parameter->Labels.end(), label);
		if (it == parameter->Labels.end())
			return ReportOnce(m_Guid + '\n' + name + '\n' + label, "Sound " + m_Guid + " parameter '" + name + "' has no label '" + label + "'");
		return SetParameter(name, static_cast<float>(it - parameter->Labels.begin()));
	}

	bool AudioEventInstance::SetParameter(const std::string& name, float value)
	{
		if (!IsValid())
			return false;
		const SoundParameter* parameter = m_Impl->Definition->FindParameter(name);
		if (!parameter)
			return ReportOnce(m_Guid + '\n' + name, "Sound " + m_Guid + " has no parameter '" + name + "', so setting it does nothing");
		if (!std::isfinite(value))
			return CheckResult(MA_INVALID_ARGS, "set a parameter to a value that is not finite");
		value = std::clamp(value, parameter->Minimum, parameter->Maximum);
		if (parameter->IsLabeled())
			value = std::round(value);
		m_Impl->ParameterValues[static_cast<size_t>(parameter - m_Impl->Definition->Parameters.data())] = value;
		RefreshVoice(*m_Impl);
		return true;
	}

	int AudioEventInstance::GetTimelinePosition() const
	{
		if (!IsValid())
			return 0;
		if (m_Impl->Leaves.empty())
			return std::max(m_Impl->PendingTimeline, 0);
		Leaf& leaf = *m_Impl->Leaves.front();
		ma_uint64 cursor = 0;
		if (!CheckResult(ma_sound_get_cursor_in_pcm_frames(leaf.Sound.get(), &cursor), "get timeline position"))
			return 0;
		return LeafMilliseconds(leaf, cursor);
	}

	int AudioEventInstance::GetLength() const
	{
		if (!IsValid())
			return 0;
		if (m_Impl->CachedLength < 0)
			m_Impl->CachedLength = ClipLengthMilliseconds(*m_Impl->Definition);
		return m_Impl->CachedLength;
	}

	void AudioEventInstance::SetTimelinePosition(int milliseconds)
	{
		if (!IsValid())
			return;
		if (!m_Impl->Started)
		{
			// Applied when it starts, like a position set on an FMOD event before it plays. Each clip
			// wraps or clamps it again against its own length then.
			const int length = GetLength();
			milliseconds = std::max(0, milliseconds);
			if (length > 0)
				milliseconds = m_Impl->Definition->Loops() ? milliseconds % length : std::min(milliseconds, length);
			m_Impl->PendingTimeline = milliseconds;
			return;
		}
		for (auto& leaf : m_Impl->Leaves)
			SeekLeaf(*leaf, milliseconds);
	}

	bool AudioEventInstance::Start()
	{
		if (!IsValid())
			return false;
		NativeVoice& voice = *m_Impl;
		const Sound& sound = *voice.Definition;
		ma_engine* engine = NativeInternal::GetEngine();
		const auto now = std::chrono::steady_clock::now();

		// Cooldown and the instance limit are budgets, so a refused start is expected, not an error.
		if (sound.Cooldown > 0.0f && voice.SoundHandle)
		{
			const auto last = s_LastStart.find(voice.SoundHandle);
			if (last != s_LastStart.end() && now - last->second < std::chrono::duration<float>(sound.Cooldown))
				return false;
		}
		if (sound.InstanceLimit > 0 && voice.SoundHandle)
		{
			std::vector<NativeVoice*> playing;
			for (NativeVoice* other : s_Voices)
			{
				if (other != &voice && other->SoundHandle == voice.SoundHandle && other->Live() && other->Started && !AllAtEnd(*other))
					playing.push_back(other);
			}
			if (playing.size() >= sound.InstanceLimit)
			{
				if (sound.Steal == SoundStealPolicy::None)
					return false;
				const auto loudness = [](const NativeVoice* candidate)
				{
					float loudest = 0.0f;
					for (const auto& leaf : candidate->Leaves)
						loudest = std::max(loudest, leaf->CurrentGain);
					return loudest;
				};
				const auto victim = sound.Steal == SoundStealPolicy::Oldest
					? *std::min_element(playing.begin(), playing.end(), [](const NativeVoice* a, const NativeVoice* b) { return a->StartOrder < b->StartOrder; })
					: *std::min_element(playing.begin(), playing.end(), [&](const NativeVoice* a, const NativeVoice* b) { return loudness(a) < loudness(b); });
				ReleaseLeaves(*victim);
				MarkStopped(*victim);
			}
		}

		AudioAccessibility::Track(this);
		auto* state = State(m_CallbackToken);
		if (state)
			state->Playback = {};
		// Like FMOD, starting again restarts: a new choice through the tree, from the clips' starts.
		ReleaseLeaves(voice);
		voice.Started = false;
		std::vector<std::vector<uint32_t>> paths;
		std::vector<uint32_t> path;
		std::string error;
		ma_result result = MA_SUCCESS;
		if (!Choose(voice, 0, path, paths, error))
			result = MA_INVALID_DATA;
		for (const auto& clipPath : paths)
		{
			if (result != MA_SUCCESS)
				break;
			auto leaf = CreateScope<Leaf>();
			result = CreateLeaf(engine, voice, clipPath, *leaf, error);
			voice.Leaves.push_back(std::move(leaf));
		}
		if (result == MA_SUCCESS)
		{
			for (auto& leaf : voice.Leaves)
			{
				PlaceLeaf(voice, *leaf);
				if (voice.PendingTimeline >= 0)
					SeekLeaf(*leaf, voice.PendingTimeline);
			}
			voice.PendingTimeline = -1;
			RefreshVoice(voice);
			for (auto& leaf : voice.Leaves)
			{
				if (result == MA_SUCCESS && !voice.Paused)
					result = ma_sound_start(leaf->Sound.get());
			}
		}
		if (result != MA_SUCCESS)
		{
			ReleaseLeaves(voice);
			if (state)
				state->Playback.Error = result;
			if (!error.empty() && m_ReportedErrors.emplace("start: " + error).second)
				LUX_CORE_ERROR_TAG("Audio", "Sound {0} cannot start: {1}", m_Guid, error);
			return error.empty() ? CheckResult(result, "start") : false;
		}
		voice.Started = true;
		voice.StartOrder = ++s_StartOrder;
		if (voice.SoundHandle)
			s_LastStart[voice.SoundHandle] = now;
		if (state)
		{
			float duration = 0.0f;
			for (const auto& leaf : voice.Leaves)
			{
				if (leaf->ClipSampleRate)
					duration = std::max(duration, static_cast<float>(leaf->ClipFrames) / static_cast<float>(leaf->ClipSampleRate));
			}
			state->Playback.Started = state->Playback.SoundStarted = true;
			state->Playback.Duration = duration;
		}
		return true;
	}

	void AudioEventInstance::Stop(bool)
	{
		// Sounds author no fade yet, so a fade-out stop is immediate too.
		if (!IsValid() || !m_Impl->Started)
			return;
		ReleaseLeaves(*m_Impl);
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
		return IsValid() && m_Impl->Started && !AllAtEnd(*m_Impl);
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
		voice.Velocity = velocity;
		// Only cones read the direction, and Sounds author none yet; keep it valid regardless.
		voice.Direction = glm::dot(forward, forward) > 1.0e-8f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
		for (auto& leaf : voice.Leaves)
			PlaceLeaf(voice, *leaf);
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
		RefreshVoice(*m_Impl);
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
		// Kept for the reverb return the native mixer adds.
		m_Impl->ReverbSend = std::clamp(acoustics.ReverbSend, 0.0f, 1.0f);
		RefreshVoice(*m_Impl);
	}
}
