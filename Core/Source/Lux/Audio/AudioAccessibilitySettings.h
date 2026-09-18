#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <glm/glm.hpp>

namespace YAML { class Node; class Emitter; }
namespace Lux
{
	class StreamReader;
	class StreamWriter;
	enum class AudioCategory : uint8_t { Master, Music, SFX, Dialogue, UI, Ambience, Count };
	enum class AudioDynamicRange : uint8_t { Full, Reduced, Night };
	inline constexpr size_t AudioCategoryCount = static_cast<size_t>(AudioCategory::Count);
	inline constexpr const char* AudioCategoryNames[] = { "Master", "Music", "SFX", "Dialogue", "UI", "Ambience" };

	struct AudioAccessibilityPreferences
	{
		bool Subtitles = true, Captions = false, VisualCues = false, SpeakerNames = true, DirectionIndicators = true;
		bool Mono = false, AudioDescriptions = false;
		AudioDynamicRange DynamicRange = AudioDynamicRange::Full;
		float TextSize = 24.0f, BackgroundOpacity = 0.75f, DurationMultiplier = 1.0f;
		uint32_t MaxLines = 3;
		float DialogueBoost = 1.0f;
		std::array<float, AudioCategoryCount> Volumes{ 1, 1, 1, 1, 1, 1 };
		bool Validate() const;
		void SerializeYAML(YAML::Emitter& out) const;
		bool DeserializeYAML(const YAML::Node& node);
	};

	struct AudioEventAccessibility
	{
		// Locale -> caption. Empty translations opt out of closed captions.
		std::map<std::string, std::string> Captions;
		AudioCategory Category = AudioCategory::SFX;
		bool VisualCue = false;
		float Intensity = 1.0f, MaxDistance = 50.0f;
	};

	struct AudioAccessibilityConfig
	{
		AudioAccessibilityPreferences Defaults;
		bool BuiltInUI = true;
		std::string DefaultLanguage = "en";
		std::array<std::string, AudioCategoryCount> BusPaths{ "bus:/", "", "", "", "", "" };
		float DescriptionDuck = 0.25f;
		std::map<std::string, AudioEventAccessibility> Events; // Stable FMOD event GUID.
		std::map<std::string, glm::vec4> SpeakerColors; // Localized speaker name -> RGBA.
		bool Validate() const;
		void SerializeYAML(YAML::Emitter& out) const;
		bool DeserializeYAML(const YAML::Node& node);
		bool Serialize(StreamWriter& stream) const;
		bool Deserialize(StreamReader& stream);
	};
}
