#pragma once
#include <cstdint>
#include <map>
#include <string>
namespace YAML { class Node; class Emitter; }
namespace Lux
{
	class StreamReader;
	class StreamWriter;
	struct AudioPerformanceSettings
	{
		uint32_t RealVoices = 64;
		float CPUPercent = 5.0f;
		float RaytracingMilliseconds = 2.0f;
		uint32_t BankMemoryMiB = 64;
		// Inclusive channel counts; these are warning thresholds, not bus muting rules.
		std::map<std::string, uint32_t> BusVoices{ { "bus:/", 64 } };
		bool Validate() const;
		void SerializeYAML(YAML::Emitter& out) const;
		bool DeserializeYAML(const YAML::Node& node);
		bool Serialize(StreamWriter& stream) const;
		bool Deserialize(StreamReader& stream);
	};
}
