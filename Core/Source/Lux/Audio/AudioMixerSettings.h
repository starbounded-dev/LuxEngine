// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/UUID.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Lux {

	namespace Yaml { class Writer; class Node; }
	class StreamReader;
	class StreamWriter;

	enum class AudioMixerEffectType : uint8_t { LowPass, HighPass, PeakingEQ, Delay, Compressor };

	// One stage of a bus's effect chain, applied in list order after the bus volume. Each type reads
	// only its own fields; a bypassed stage is left out of the chain.
	struct AudioMixerEffect
	{
		AudioMixerEffectType Type = AudioMixerEffectType::LowPass;
		bool Bypass = false;
		float FrequencyHz = 1000.0f;  // LowPass/HighPass cutoff, PeakingEQ centre; 20-20000
		float GainDb = 0.0f;          // PeakingEQ boost or cut; -24-24
		float Q = 0.707f;             // PeakingEQ width; 0.1-10
		float DelaySeconds = 0.25f;   // Delay; 0.001-2
		float Feedback = 0.3f;        // Delay decay per repeat; 0-0.95
		float Wet = 0.5f;             // Delay wet level; 0-1 (the dry signal passes at 1)
		float ThresholdDb = -18.0f;   // Compressor; -60-0
		float Ratio = 4.0f;           // Compressor; 1-20
		float AttackMs = 10.0f;       // Compressor; 0.1-500
		float ReleaseMs = 100.0f;     // Compressor; 1-5000
		float MakeupDb = 0.0f;        // Compressor; 0-24
		bool operator==(const AudioMixerEffect&) const = default;
	};

	// One mixer bus. A bus plays into its parent, named by the path: "bus:/SFX/Steps" plays into
	// "bus:/SFX", and every top-level bus into the master "bus:/".
	struct AudioMixerBus
	{
		std::string Path;
		float Volume = 1.0f; // linear, 0-4
		bool Muted = false;
		std::vector<AudioMixerEffect> Effects; // unused in snapshot overrides
		bool operator==(const AudioMixerBus&) const = default;
	};

	// The built-in reverb return every voice sends to (Soundwave's ReverbSend plus a Sound's Send
	// curves). Its output plays into the master bus.
	struct AudioReverbReturnSettings
	{
		float RoomSize = 0.7f; // 0-1
		float Damping = 0.5f;  // 0-1
		float Width = 1.0f;    // 0-1
		float Volume = 1.0f;   // 0-4, the return's level
		bool operator==(const AudioReverbReturnSettings&) const = default;
	};

	// A named mix change zones and scripts blend in. Intensity 0-1 moves each listed bus from its own
	// volume toward the snapshot's; where active snapshots list the same bus, the highest Priority
	// wins (the one started last on a tie). A zone or script refers to it by ID, through the slot an
	// AudioEventRef uses for a Sound (AudioEventRef::Sound), so references survive renames.
	struct AudioMixerSnapshot
	{
		UUID ID;
		std::string Name;
		int Priority = 0;
		std::vector<AudioMixerBus> Buses; // Path and Volume of each override; Muted is unused
		bool operator==(const AudioMixerSnapshot&) const = default;
	};

	// A volume control over a group of buses ("vca:/Name"), as FMOD's VCAs: AudioEngine::SetVCAVolume
	// multiplies each listed bus's volume. Saved here are only its name and buses; its volume is
	// gameplay state, 1 at start.
	struct AudioMixerVCA
	{
		std::string Path;
		std::vector<std::string> Buses;
		bool operator==(const AudioMixerVCA&) const = default;
	};

	// The project's mix for the native backend; the FMOD backend authors its mix in FMOD Studio and
	// ignores this. Main thread; read once by AudioEngine::Init.
	struct AudioMixerSettings
	{
		std::vector<AudioMixerBus> Buses = DefaultBuses(); // never lists the master "bus:/"
		AudioReverbReturnSettings Reverb;
		std::vector<AudioMixerSnapshot> Snapshots;
		std::vector<AudioMixerVCA> VCAs;
		bool operator==(const AudioMixerSettings&) const = default;

		// Music, SFX, Dialogue and Ambience: the default accessibility bus mappings.
		static std::vector<AudioMixerBus> DefaultBuses();
		static constexpr size_t kMaxEffects = 8; // per bus
		static const char* EffectName(AudioMixerEffectType type);
		// "bus:/X" -> "bus:/", "bus:/X/Y" -> "bus:/X".
		static std::string ParentPath(const std::string& path);

		// Paths are "bus:/" plus '/'-separated nonempty names, unique, each with its parent listed
		// before it; values are finite and in range. Snapshots have unique nonzero IDs and nonempty
		// unique names, and override listed buses once each. outError names the first problem.
		bool Validate(std::string* outError = nullptr) const;
		void SerializeYAML(Yaml::Writer& out) const;
		// Missing keys use defaults; on any error the settings are unchanged and false is returned.
		bool DeserializeYAML(const Yaml::Node& node);
		// The runtime project's block: a length-prefixed YAML text of at most 64 KiB.
		bool Serialize(StreamWriter& stream) const;
		bool Deserialize(StreamReader& stream);
	};

}
