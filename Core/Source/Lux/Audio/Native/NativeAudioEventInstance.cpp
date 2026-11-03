// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioEventInstance.h"

#include "Lux/Audio/AudioAccessibility.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioFileUtils.h"
#include "NativeInternal.h"
#include "Miniaudio.h"
#include "Lux/Utilities/StringUtils.h"

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
		// off at unity gain output 0 copies its input exactly. Output 1 is the post-fader send to the
		// reverb return (Soundwave's ReverbSend plus Send curves), ramped the same way.
		struct VoiceNode
		{
			ma_node_base Base;   // first: miniaudio passes &Base as the node
			std::atomic<float> TargetGain{ 1.0f };
			std::atomic<float> TargetSend{ 0.0f };
			std::atomic<float> LowPassHz{ kOpenCutoffHz };
			std::atomic<float> HighPassHz{ 0.0f };
			// Audio thread only.
			float Gain = 1.0f, Send = 0.0f;
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
			float* sendOutput = data.GetOutputBuffer(1).data.data;
			const uint32_t channels = voice.Channels;
			const float target = voice.TargetGain.load(std::memory_order_relaxed);
			// The first block starts at its gain: a voice set up quiet must not begin with a ramp from 1.
			const float start = voice.Primed ? voice.Gain : target;
			const float sendTarget = voice.TargetSend.load(std::memory_order_relaxed);
			const float sendStart = voice.Primed ? voice.Send : sendTarget;
			voice.Primed = true;
			const float step = frames > 0 ? (target - start) / static_cast<float>(frames) : 0.0f;
			const float sendStep = frames > 0 ? (sendTarget - sendStart) / static_cast<float>(frames) : 0.0f;
			const float lowCutoff = voice.LowPassHz.load(std::memory_order_relaxed);
			const float highCutoff = voice.HighPassHz.load(std::memory_order_relaxed);
			const bool lowPass = lowCutoff < kOpenCutoffHz;
			const bool highPass = highCutoff > 0.0f;
			const float lowPole = lowPass ? std::exp(-2.0f * std::numbers::pi_v<float> * lowCutoff / voice.SampleRate) : 0.0f;
			const float highPole = highPass ? std::exp(-2.0f * std::numbers::pi_v<float> * highCutoff / voice.SampleRate) : 0.0f;
			for (ma_uint32 frame = 0; frame < frames; ++frame)
			{
				const float gain = start + step * static_cast<float>(frame + 1);
				const float send = gain * (sendStart + sendStep * static_cast<float>(frame + 1));
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
					sendOutput[frame * channels + channel] = sample * send;
				}
			}
			voice.Gain = target;
			voice.Send = sendTarget;
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
			ma_uint64 LoopBegin = 0, LoopEnd = 0;   // the frames a loop region wraps between; End 0 is the whole clip
			uint32_t ClipSampleRate = 0;
			ma_uint64 ClipFrames = 0;
			float CurrentGain = 0.0f;   // last target gain, for the quietest-instance steal policy
			std::filesystem::path ClipFile;   // counted in the clip cache while set (NativeInternal::UseClip)
		};

		// Everything the engine reaches through NativeInternal. The wrapper's Impl is this, so the
		// free functions below never need the wrapper's private members.
		struct NativeVoice
		{
			Ref<Sound> Definition;               // a private copy: edits to the asset apply to the next instance
			uint64_t SoundHandle = 0;            // the asset's handle; 0 for an in-memory Sound
			uint64_t SnapshotId = 0;             // a mixer snapshot instead of a Sound: no leaves, never ends
			uint64_t ClipOverride = 0;           // an audio asset every clip node plays instead (SetClipOverride)
			bool Virtual = false;                // over the voice budget: stopped, its time still passing
			ma_uint64 ScheduledStart = 0;        // StartAt: the engine time the next Start begins playing at
			ma_uint64 ScheduledStop = 0;         // StopAt: the engine time playback ends; 0 is none
			ma_uint64 StartEngineTime = 0;       // the engine time its first sample plays at
			ma_uint64 VirtualSince = 0;          // engine time (frames) when it went virtual
			// Music timeline (EnableTimelineNotifications): beats and markers from the Sound's tempo map,
			// found from the first clip's cursor on the main thread.
			bool Timeline = false;
			uint64_t TimelineSequence = 0;
			double TimelineSeconds = -1.0;       // the cursor last reported; below 0 before the first event
			std::vector<AudioTimelineNotification> TimelineQueue;
			float SnapshotIntensity = 1.0f;
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

		void ReleaseLeaf(Leaf& leaf)
		{
			// Upstream first: uninitializing detaches, and waits for the audio thread to let go.
			leaf.Sound.clear();
			leaf.Output.clear();
			if (!leaf.ClipFile.empty())
				NativeInternal::DropClip(leaf.ClipFile);
			leaf.ClipFile.clear();
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
			if (voice.SnapshotId || voice.Virtual)
				return false; // a virtual voice ends when ApplyVoiceBudget finds its time has run out
			if (voice.ScheduledStop && ma_engine_get_time_in_pcm_frames(NativeInternal::GetEngine()) >= voice.ScheduledStop)
				return true;
			return std::all_of(voice.Leaves.begin(), voice.Leaves.end(), [](const Scope<Leaf>& leaf) { return LeafAtEnd(*leaf); });
		}

		// Keyed by the lower-case name: parameter names compare case-insensitively.
		std::unordered_map<std::string, float> s_GlobalParameters;

		float ParameterValue(const NativeVoice& voice, const std::string& name)
		{
			const SoundParameter* parameter = voice.Definition->FindParameter(name);
			if (!parameter)
				return 0.0f;
			if (parameter->Global)
			{
				// One value for all Sounds, held to this Sound's range.
				const float value = std::clamp(NativeInternal::GetGlobalParameterValue(parameter->Name, parameter->Default), parameter->Minimum, parameter->Maximum);
				return parameter->IsLabeled() ? std::round(value) : value;
			}
			return voice.ParameterValues[static_cast<size_t>(parameter - voice.Definition->Parameters.data())];
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
			leaf.Output->TargetSend.store(std::clamp(voice.ReverbSend + send, 0.0f, 1.0f), std::memory_order_relaxed);
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
				// Weights are normalized, so the closest listeners count up to 100%
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

		// Queues the beats and markers with from < time <= to, in time order (a beat before a marker at
		// the same time).
		void EmitTimeline(NativeVoice& voice, double from, double to)
		{
			const Sound& sound = *voice.Definition;
			struct Event
			{
				double Time;
				int64_t Beat;              // the beat's index from the first, or -1 for a marker
				const SoundMarker* Marker;
			};
			std::vector<Event> events;
			if (sound.Tempo.BPM > 0.0f)
			{
				const double spacing = 60.0 / sound.Tempo.BPM, offset = sound.Tempo.Offset;
				int64_t beat = std::max<int64_t>(0, static_cast<int64_t>(std::floor((from - offset) / spacing)) + 1);
				// A long gap (a hitch) reports at most a bar's worth of the latest beats.
				for (int count = 0; offset + static_cast<double>(beat) * spacing <= to && count < 1024; ++beat, ++count)
				{
					const double time = offset + static_cast<double>(beat) * spacing;
					if (time > from)
						events.push_back({ time, beat, nullptr });
				}
			}
			for (const SoundMarker& marker : sound.Markers)
				if (marker.Seconds > from && marker.Seconds <= to)
					events.push_back({ marker.Seconds, -1, &marker });
			std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b)
			{
				return a.Time < b.Time || (a.Time == b.Time && a.Beat >= 0 && b.Beat < 0);
			});
			for (const Event& event : events)
			{
				if (voice.TimelineQueue.size() >= kNotificationCapacity)
					break;
				AudioTimelineNotification notification;
				notification.Sequence = ++voice.TimelineSequence;
				notification.Position = static_cast<int>(std::lround(event.Time * 1000.0));
				if (event.Beat >= 0)
				{
					const auto perBar = static_cast<int64_t>(sound.Tempo.BeatsPerBar);
					notification.IsBeat = true;
					notification.Bar = static_cast<int>(event.Beat / perBar) + 1;
					notification.Beat = static_cast<int>(event.Beat % perBar) + 1;
					notification.Tempo = sound.Tempo.BPM;
					notification.TimeSignatureUpper = static_cast<int>(sound.Tempo.BeatsPerBar);
					notification.TimeSignatureLower = static_cast<int>(sound.Tempo.BeatUnit);
				}
				else
					notification.Marker = event.Marker->Name;
				voice.TimelineQueue.push_back(std::move(notification));
			}
		}

		// Catches the timeline up to the playback cursor. A cursor behind the last one means the clip
		// looped: the rest of the loop (its region's end, or the clip's), then from where it wraps to.
		void AdvanceTimeline(NativeVoice& voice)
		{
			if (!voice.Timeline || !voice.Live() || !voice.Started || voice.Leaves.empty())
				return;
			const Sound& sound = *voice.Definition;
			if (sound.Tempo.BPM <= 0.0f && sound.Markers.empty())
				return;
			Leaf& leaf = *voice.Leaves.front();
			ma_uint64 cursor = 0;
			if (!leaf.ClipSampleRate || ma_sound_get_cursor_in_pcm_frames(leaf.Sound.get(), &cursor) != MA_SUCCESS)
				return;
			const double now = static_cast<double>(cursor) / leaf.ClipSampleRate;
			const double end = static_cast<double>(leaf.LoopEnd ? leaf.LoopEnd : leaf.ClipFrames) / leaf.ClipSampleRate;
			const double begin = static_cast<double>(leaf.LoopBegin) / leaf.ClipSampleRate;
			if (now + 1.0e-6 < voice.TimelineSeconds)
			{
				EmitTimeline(voice, voice.TimelineSeconds, end - 1.0e-9); // a beat on the end is the next loop's first
				EmitTimeline(voice, begin > 0.0 ? begin - 1.0e-6 : -1.0, now);
			}
			else
				EmitTimeline(voice, voice.TimelineSeconds, now);
			voice.TimelineSeconds = now;
		}

		CallbackState* State(uint64_t token)
		{
			const auto it = token ? s_CallbackStates.find(token) : s_CallbackStates.end();
			return it != s_CallbackStates.end() ? &it->second : nullptr;
		}

		// Playback ended or was stopped: what the Stopped callback reports.
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
			// A virtual voice stays stopped; ApplyVoiceBudget starts it again unless it is paused.
			if (!voice.Live() || !voice.Started || voice.Virtual)
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
			if (!NativeInternal::ResolveClip(voice.ClipOverride ? AssetHandle(voice.ClipOverride) : clip.Clip, file, outError))
				return MA_DOES_NOT_EXIST;
			// Decoded once and shared while a bank or a voice holds it, or streamed in pages.
			if (const int used = NativeInternal::UseClip(file, sound.Stream, voice.SoundHandle, outError); used != MA_SUCCESS)
				return static_cast<ma_result>(used);
			leaf.ClipFile = file;

			const uint32_t channels = ma_engine_get_channels(engine);
			ma_result result = leaf.Output.Init(JPL::NodeLayout().WithInputs(channels).WithOutputs({ channels, channels })) ? MA_SUCCESS : MA_ERROR;
			if (result == MA_SUCCESS)
			{
				// Set before attaching: the audio thread cannot reach the node until then.
				leaf.Output->Channels = channels;
				leaf.Output->SampleRate = static_cast<float>(ma_engine_get_sample_rate(engine));
				result = ma_node_attach_output_bus(leaf.Output.get(), 0, NativeInternal::GetBusInput(sound.Bus), 0);
			}
			// Without a reverb return the send output stays unattached and is never pulled.
			if (result == MA_SUCCESS)
				if (ma_node* reverb = NativeInternal::GetReverbReturn())
					result = ma_node_attach_output_bus(leaf.Output.get(), 1, reverb, 0);
			if (result == MA_SUCCESS)
			{
				// Decoded into memory and shared through the clip cache, or streamed in pages by the
				// engine's streaming thread, from a file or from inside the pack.
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
				{
					result = ma_data_source_set_loop_point_in_pcm_frames(ma_sound_get_data_source(leaf.Sound.get()), begin, end);
					leaf.LoopBegin = begin;
					leaf.LoopEnd = end;
				}
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
				if (NativeInternal::IsPackedClip(file))
					longest = std::max(longest, NativeInternal::GetPackedClipSeconds(file));
				else if (const auto info = AudioFileUtils::GetFileInfo(file))
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

	float NativeInternal::GetGlobalParameterValue(const std::string& name, float fallback)
	{
		const auto it = s_GlobalParameters.find(Utils::String::ToLowerCopy(name));
		return it != s_GlobalParameters.end() ? it->second : fallback;
	}

	void NativeInternal::SetGlobalParameterValue(const std::string& name, float value)
	{
		s_GlobalParameters[Utils::String::ToLowerCopy(name)] = value;
	}

	void NativeInternal::ClearGlobalParameters()
	{
		s_GlobalParameters.clear();
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

	int NativeInternal::CountPlayingVoicesOn(const std::string& busPath)
	{
		int playing = 0;
		for (const NativeVoice* voice : s_Voices)
		{
			if (!voice->Live() || !voice->Started || voice->SnapshotId)
				continue;
			const std::string& bus = voice->Definition->Bus;
			playing += busPath == "bus:/" || bus == busPath || bus.rfind(busPath + "/", 0) == 0;
		}
		return playing;
	}

	int NativeInternal::ApplyVoiceBudget(uint32_t realVoices)
	{
		ma_engine* engine = NativeInternal::GetEngine();
		if (!engine)
			return 0;
		const ma_uint64 now = ma_engine_get_time_in_pcm_frames(engine);
		const ma_uint32 engineRate = ma_engine_get_sample_rate(engine);
		// Where a leaf would be after a virtual spell: its cursor plus the time passed, wrapped in its
		// loop; past the end of a one-shot means it has finished.
		const auto resume = [&](NativeVoice& voice)
		{
			const double seconds = static_cast<double>(now - voice.VirtualSince) / engineRate;
			bool playing = false;
			for (auto& leaf : voice.Leaves)
			{
				ma_uint64 cursor = 0;
				ma_sound_get_cursor_in_pcm_frames(leaf->Sound.get(), &cursor);
				ma_uint64 target = cursor + static_cast<ma_uint64>(seconds * leaf->ClipSampleRate);
				if (leaf->Loop)
				{
					const ma_uint64 end = leaf->LoopEnd ? leaf->LoopEnd : leaf->ClipFrames;
					const ma_uint64 begin = leaf->LoopEnd ? leaf->LoopBegin : 0;
					if (end > begin && target >= end)
						target = begin + (target - end) % (end - begin);
				}
				else if (target >= leaf->ClipFrames)
					continue;
				ma_sound_seek_to_pcm_frame(leaf->Sound.get(), target);
				if (!voice.Paused)
					ma_sound_start(leaf->Sound.get());
				playing = true;
			}
			voice.Virtual = false;
			if (!playing)
			{
				ReleaseLeaves(voice);
				MarkStopped(voice);
			}
		};

		std::vector<NativeVoice*> candidates;
		for (NativeVoice* voice : s_Voices)
		{
			if (voice->Live() && voice->Started && !voice->SnapshotId && !voice->Leaves.empty() && (voice->Virtual || !AllAtEnd(*voice)))
				candidates.push_back(voice);
		}
		const auto loudness = [](const NativeVoice* voice)
		{
			float loudest = 0.0f;
			for (const auto& leaf : voice->Leaves)
				loudest = std::max(loudest, leaf->CurrentGain);
			return loudest;
		};
		// Most important first: higher priority (lower number), then louder, then started earlier.
		std::stable_sort(candidates.begin(), candidates.end(), [&](const NativeVoice* a, const NativeVoice* b)
		{
			if (a->Priority != b->Priority)
				return a->Priority < b->Priority;
			if (loudness(a) != loudness(b))
				return loudness(a) > loudness(b);
			return a->StartOrder < b->StartOrder;
		});
		int virtualCount = 0;
		for (size_t i = 0; i < candidates.size(); ++i)
		{
			NativeVoice& voice = *candidates[i];
			if (i < realVoices)
			{
				if (voice.Virtual)
					resume(voice);
				continue;
			}
			if (!voice.Virtual)
			{
				for (auto& leaf : voice.Leaves)
					ma_sound_stop(leaf->Sound.get());
				voice.Virtual = true;
				voice.VirtualSince = now;
			}
			++virtualCount;
		}
		return virtualCount;
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
		NativeInternal::ReleaseClips();
		NativeInternal::ClearPackedClips();
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
		uint64_t snapshot = 0;
		if (NativeInternal::ParseHandle(eventGuid, snapshot) && NativeInternal::IsMixerSnapshot(snapshot))
		{
			// A snapshot plays no clip; an empty Sound keeps every voice path valid.
			auto impl = CreateScope<Impl>();
			impl->Definition = Ref<Sound>::Create();
			impl->SnapshotId = snapshot;
			impl->Ready = true;
			s_Voices.push_back(impl.get());
			return Ref<AudioEventInstance>::Create(std::move(impl), eventGuid, AudioEngine::GetEventGeneration());
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
		if (m_Impl->SnapshotId && IsValid())
			NativeInternal::SetSnapshotState(m_Impl.get(), m_Impl->SnapshotId, false, 0.0f);
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
			m_Impl->Timeline = true;
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
		if (!IsValid())
			return {};
		AdvanceTimeline(*m_Impl);
		return std::exchange(m_Impl->TimelineQueue, {});
	}

	// Caught up first, so a transition queued between drains counts only events after the request.
	uint64_t AudioEventInstance::GetTimelineSequence() const
	{
		if (!IsValid())
			return 0;
		AdvanceTimeline(*m_Impl);
		return m_Impl->TimelineSequence;
	}

	bool AudioEventInstance::MonitorPlayback()
	{
		return ConfigureCallbacks(0, false);
	}

	bool AudioEventInstance::SetProgrammerSound(const std::string&)
	{
		return CheckResult(MA_NOT_IMPLEMENTED, "programmer sound (the native backend plays dialogue through clip overrides)");
	}

	uint64_t AudioEventInstance::GetEngineTime() const
	{
		ma_engine* engine = NativeInternal::GetEngine();
		return engine ? ma_engine_get_time_in_pcm_frames(engine) : 0;
	}

	uint64_t AudioEventInstance::GetNextSyncTime(MusicSync sync) const
	{
		if (!IsValid() || !m_Impl->Started || m_Impl->Leaves.empty() || sync == MusicSync::Immediate)
			return 0;
		const Sound& sound = *m_Impl->Definition;
		const Leaf& leaf = *m_Impl->Leaves.front();
		ma_engine* engine = NativeInternal::GetEngine();
		const ma_uint64 engineNow = ma_engine_get_time_in_pcm_frames(engine);
		if (!leaf.ClipSampleRate || engineNow < m_Impl->StartEngineTime || m_Impl->Paused)
			return 0;
		// The position heard now, from the engine clock since the first sample: the clip's cursor runs
		// ahead of the output by the node graph's read-ahead, which would land the switch early.
		const double rate = leaf.ClipSampleRate;
		const double end = static_cast<double>(leaf.LoopEnd ? leaf.LoopEnd : leaf.ClipFrames) / rate;
		const double begin = static_cast<double>(leaf.LoopBegin) / rate;
		const double pitch = std::max(ma_sound_get_pitch(leaf.Sound.get()), 0.01f);
		double now = leaf.StartOffset + static_cast<double>(engineNow - m_Impl->StartEngineTime) / ma_engine_get_sample_rate(engine) * pitch;
		if (leaf.Loop && now >= end && end > begin)
			now = begin + std::fmod(now - end, end - begin);
		// The first boundary in (from, to], or a negative time.
		const auto first = [&](double from, double to)
		{
			double best = -1.0;
			if ((sync == MusicSync::NextBeat || sync == MusicSync::NextBar) && sound.Tempo.BPM > 0.0f)
			{
				const double spacing = 60.0 / sound.Tempo.BPM;
				const int64_t stride = sync == MusicSync::NextBar ? sound.Tempo.BeatsPerBar : 1;
				int64_t beat = std::max<int64_t>(0, static_cast<int64_t>(std::floor((from - sound.Tempo.Offset) / spacing)) + 1);
				beat = (beat + stride - 1) / stride * stride;
				const double time = sound.Tempo.Offset + static_cast<double>(beat) * spacing;
				if (time > from && time <= to)
					best = time;
			}
			for (const SoundMarker& marker : sound.Markers)
			{
				const bool counts = sync == MusicSync::NextMarker || (sync == MusicSync::NextSection && marker.Name.starts_with("Section:"));
				if (counts && marker.Seconds > from && marker.Seconds <= to && (best < 0.0 || marker.Seconds < best))
					best = marker.Seconds;
			}
			return best;
		};
		double ahead = first(now, end - 1.0e-9);
		if (ahead >= 0.0)
			ahead -= now;
		else if (leaf.Loop)
		{
			// Past the loop's end: the first boundary after it wraps.
			const double wrapped = first(begin > 0.0 ? begin - 1.0e-9 : -1.0, now);
			if (wrapped < 0.0)
				return 0;
			ahead = (end - now) + (wrapped - begin);
		}
		else
			return 0;
		return engineNow + static_cast<uint64_t>(std::llround(ahead / pitch * ma_engine_get_sample_rate(engine)));
	}

	bool AudioEventInstance::StartAt(uint64_t engineTime)
	{
		if (!IsValid() || m_Impl->SnapshotId)
			return false;
		const bool started = Start();
		// Start cleared the schedule, so playback timing is set on the leaves it just created.
		if (started)
		{
			for (auto& leaf : m_Impl->Leaves)
				ma_sound_set_start_time_in_pcm_frames(leaf->Sound.get(), engineTime);
			m_Impl->ScheduledStart = engineTime;
			m_Impl->StartEngineTime = engineTime;
		}
		return started;
	}

	bool AudioEventInstance::StopAt(uint64_t engineTime)
	{
		if (!IsValid() || !m_Impl->Started || m_Impl->SnapshotId)
			return false;
		for (auto& leaf : m_Impl->Leaves)
			ma_sound_set_stop_time_in_pcm_frames(leaf->Sound.get(), engineTime);
		m_Impl->ScheduledStop = engineTime == UINT64_MAX ? 0 : engineTime; // UINT64_MAX takes a scheduled stop back
		return true;
	}

	bool AudioEventInstance::SetClipOverride(UUID clip)
	{
		if (!IsValid() || m_Impl->SnapshotId || m_Impl->Started)
			return CheckResult(MA_INVALID_OPERATION, "clip override (needs an unstarted Sound instance)");
		std::filesystem::path file;
		std::string error;
		if (clip != 0 && !NativeInternal::ResolveClip(AssetHandle(clip), file, error))
			return CheckResult(MA_DOES_NOT_EXIST, ("clip override: " + error).c_str());
		m_Impl->ClipOverride = static_cast<uint64_t>(clip);
		return true;
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
		return IsValid() && m_Impl->Virtual;
	}

	bool AudioEventInstance::Is3D() const
	{
		return IsValid() && m_Impl->Definition->Spatial.Enabled;
	}

	bool AudioEventInstance::IsOneShot() const
	{
		return IsValid() && !m_Impl->SnapshotId && !m_Impl->Definition->Loops();
	}

	bool AudioEventInstance::IsSnapshot() const
	{
		return IsValid() && m_Impl->SnapshotId != 0;
	}

	bool AudioEventInstance::SetSnapshotIntensity(float intensity)
	{
		if (!IsValid())
			return false;
		if (!m_Impl->SnapshotId)
			return CheckResult(MA_INVALID_OPERATION, "snapshot intensity (this is a Sound, not a mixer snapshot)");
		if (!std::isfinite(intensity) || intensity < 0.0f || intensity > 1.0f)
			return CheckResult(MA_INVALID_ARGS, "snapshot intensity (must be 0-1)");
		m_Impl->SnapshotIntensity = intensity;
		if (m_Impl->Started)
			NativeInternal::SetSnapshotState(m_Impl.get(), m_Impl->SnapshotId, true, intensity);
		return true;
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
		if (parameter->Global)
			return ReportOnce(m_Guid + "\nglobal\n" + name, "Parameter '" + name + "' is global; set it with Audio.SetGlobalParameter");
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
			// Applied when it starts, like a position set before it plays. Each clip
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
		m_Impl->TimelineSeconds = milliseconds / 1000.0 - 1.0e-6;
		m_Impl->TimelineQueue.clear();
	}

	bool AudioEventInstance::Start()
	{
		if (!IsValid())
			return false;
		NativeVoice& voice = *m_Impl;
		if (voice.SnapshotId)
		{
			voice.Started = true;
			NativeInternal::SetSnapshotState(&voice, voice.SnapshotId, true, voice.SnapshotIntensity);
			return true;
		}
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
		// Starting again restarts: a new choice through the tree, from the clips' starts.
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
				if (voice.ScheduledStart)
					ma_sound_set_start_time_in_pcm_frames(leaf->Sound.get(), voice.ScheduledStart);
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
		// The timeline starts just before where playback starts, so a beat on the first sample is reported.
		voice.TimelineQueue.clear();
		voice.TimelineSeconds = -1.0;
		voice.Virtual = false;
		voice.ScheduledStart = voice.ScheduledStop = 0;
		if (!voice.Leaves.empty() && voice.Leaves.front()->ClipSampleRate)
		{
			ma_uint64 cursor = 0;
			if (ma_sound_get_cursor_in_pcm_frames(voice.Leaves.front()->Sound.get(), &cursor) == MA_SUCCESS && cursor > 0)
				voice.TimelineSeconds = static_cast<double>(cursor) / voice.Leaves.front()->ClipSampleRate - 1.0e-6;
		}
		voice.Started = true;
		voice.StartEngineTime = ma_engine_get_time_in_pcm_frames(engine);
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
		if (m_Impl->SnapshotId)
			NativeInternal::SetSnapshotState(m_Impl.get(), m_Impl->SnapshotId, false, 0.0f);
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
		// A paused voice still counts.
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
