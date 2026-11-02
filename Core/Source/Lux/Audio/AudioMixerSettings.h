// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <string>
#include <vector>

namespace Lux {

	namespace Yaml { class Writer; class Node; }

	// One mixer bus. A bus plays into its parent, named by the path: "bus:/SFX/Steps" plays into
	// "bus:/SFX", and every top-level bus into the master "bus:/".
	struct AudioMixerBus
	{
		std::string Path;
		float Volume = 1.0f; // linear, 0-4
		bool Muted = false;
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

	// The project's mix for the native backend; the FMOD backend authors its mix in FMOD Studio and
	// ignores this. Main thread; read once by AudioEngine::Init.
	struct AudioMixerSettings
	{
		std::vector<AudioMixerBus> Buses = DefaultBuses(); // never lists the master "bus:/"
		AudioReverbReturnSettings Reverb;
		bool operator==(const AudioMixerSettings&) const = default;

		// Music, SFX, Dialogue and Ambience: the default accessibility bus mappings.
		static std::vector<AudioMixerBus> DefaultBuses();
		// "bus:/X" -> "bus:/", "bus:/X/Y" -> "bus:/X".
		static std::string ParentPath(const std::string& path);

		// Paths are "bus:/" plus '/'-separated nonempty names, unique, each with its parent listed
		// before it; values are finite and in range. outError names the first problem.
		bool Validate(std::string* outError = nullptr) const;
		void SerializeYAML(Yaml::Writer& out) const;
		// Missing keys use defaults; on any error the settings are unchanged and false is returned.
		bool DeserializeYAML(const Yaml::Node& node);
	};

}
