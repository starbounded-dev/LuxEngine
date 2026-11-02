// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once
#include <cstdint>
namespace Lux::Yaml { class Node; class Writer; }
namespace Lux::Soundwave { struct Settings; }
namespace Lux
{
	class StreamReader;
	class StreamWriter;

	struct AudioOcclusionSettings
	{
		float UpdateRateHz = 20.0f;   // how often each source is recast
		uint32_t CastBudget = 32;     // sources recast per frame at most
		float Strength = 1.0f;        // scales wall dB loss; 1 uses authored material transmission
		uint32_t ReflectionDepth = 2;
		uint32_t OpeningGridSize = 8;
		uint32_t MaxBounces = 2;
		uint32_t CandidateBudget = 4096; // indirect work per source update
		uint32_t PathBudget = 64;
		uint32_t OpeningNodeBudget = 128;
		float Absorption = 1.0f; // fraction of authored material absorption applied at a reflection
		uint32_t ReverbRays = 256;     // listener reverb rays per measurement
		uint32_t ReverbBounces = 16;
		float ReverbReferenceSeconds = 1.5f; // decay time that gives a strong (63%) reverb send
		bool CompareRaycast = false; // temporary engine-oracle diagnostics, never a playback mode
		Soundwave::Settings GetKernelSettings() const;
		bool Validate() const;
		void SerializeYAML(Yaml::Writer& out) const;
		bool DeserializeYAML(const Yaml::Node& node);
		bool Serialize(StreamWriter& stream) const;
		bool Deserialize(StreamReader& stream);
	};
}
