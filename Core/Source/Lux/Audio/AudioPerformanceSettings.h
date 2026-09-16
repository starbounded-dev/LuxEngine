#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
namespace YAML { class Node; class Emitter; }
namespace Lux
{
	class StreamReader;
	class StreamWriter;
	struct AudioPerformanceSettings
	{
		bool MuteWhenUnfocused = false;
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
	// Editor authoring profile. Native exports flatten its effective settings into the runtime data.
	struct AudioDesktopProfile
	{
		bool Enabled = false;
		std::string StudioPlatform = "Desktop";
		std::filesystem::path BankOutputPath = "Build/Desktop";
		AudioPerformanceSettings Performance;
		bool Validate() const;
		void SerializeYAML(YAML::Emitter& out) const;
		bool DeserializeYAML(const YAML::Node& node);
	};
	bool IsValidStudioPlatform(const std::string& name);

}
