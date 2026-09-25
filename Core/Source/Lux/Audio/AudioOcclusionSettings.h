// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once
#include <cstdint>
namespace YAML { class Node; class Emitter; }
namespace Lux
{
	class StreamReader;
	class StreamWriter;

	// Which measurement drives each event's Studio Occlusion parameter. Engine casts rays through the
	// acoustic geometry; Raytraced uses VA's per-source result, which does not respond to geometry in
	// the current SDK (docs/vercidium-repro) and is kept for when it does.
	enum class AudioOcclusionSource : uint8_t { Engine, Raytraced };

	struct AudioOcclusionSettings
	{
		AudioOcclusionSource Source = AudioOcclusionSource::Engine;
		float UpdateRateHz = 20.0f;   // how often each source is recast
		uint32_t CastBudget = 32;     // sources recast per frame at most
		float Strength = 1.0f;        // scales every wall's dB loss; 1 follows VA's material data
		bool Validate() const;
		void SerializeYAML(YAML::Emitter& out) const;
		bool DeserializeYAML(const YAML::Node& node);
		bool Serialize(StreamWriter& stream) const;
		bool Deserialize(StreamReader& stream);
	};
}
