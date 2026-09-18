#include "lpch.h"
#include "AudioAccessibilitySettings.h"
#include "DialogueTable.h"
#include "Lux/Serialization/StreamReader.h"
#include "Lux/Serialization/StreamWriter.h"
#include <yaml-cpp/yaml.h>
#include <cmath>
#include <set>

namespace Lux
{
	namespace
	{
		constexpr uint32_t kMaxConfigBytes = 8 * 1024 * 1024;
		bool Range(float value, float low, float high)
		{
			return std::isfinite(value) && value >= low && value <= high;
		}
		bool Text(const std::string& value, size_t max)
		{
			return value.size() <= max && value.find('\0') == std::string::npos;
		}
	}

	bool AudioAccessibilityPreferences::Validate() const
	{
		const bool valid = DynamicRange <= AudioDynamicRange::Night && Range(TextSize, 12, 72) &&
			Range(BackgroundOpacity, 0, 1) && Range(DurationMultiplier, 0.5f, 3) && MaxLines >= 1 && MaxLines <= 10 &&
			Range(DialogueBoost, 1, 2) && std::all_of(Volumes.begin(), Volumes.end(), [](float volume) { return Range(volume, 0, 1); });
		if (!valid)
			LUX_CORE_ERROR_TAG("Audio", "Invalid audio accessibility preferences");
		return valid;
	}

	void AudioAccessibilityPreferences::SerializeYAML(YAML::Emitter& out) const
	{
		out << YAML::BeginMap;
#define WRITE_PREFERENCE(field) out << YAML::Key << #field << YAML::Value << field
		WRITE_PREFERENCE(Subtitles);
		WRITE_PREFERENCE(Captions);
		WRITE_PREFERENCE(VisualCues);
		WRITE_PREFERENCE(SpeakerNames);
		WRITE_PREFERENCE(DirectionIndicators);
		WRITE_PREFERENCE(Mono);
		WRITE_PREFERENCE(AudioDescriptions);
		WRITE_PREFERENCE(TextSize);
		WRITE_PREFERENCE(BackgroundOpacity);
		WRITE_PREFERENCE(DurationMultiplier);
		WRITE_PREFERENCE(MaxLines);
		WRITE_PREFERENCE(DialogueBoost);
#undef WRITE_PREFERENCE
		out << YAML::Key << "DynamicRange" << YAML::Value << static_cast<uint32_t>(DynamicRange);
		out << YAML::Key << "Volumes" << YAML::BeginMap;
		for (size_t i = 0; i < AudioCategoryCount; ++i)
			out << YAML::Key << AudioCategoryNames[i] << YAML::Value << Volumes[i];
		out << YAML::EndMap << YAML::EndMap;
	}

	bool AudioAccessibilityPreferences::DeserializeYAML(const YAML::Node& node)
	{
		try
		{
			AudioAccessibilityPreferences parsed;
			if (node && !node.IsNull() && !node.IsMap())
				throw std::runtime_error("preferences must be a map");
			if (node && !node.IsNull())
			{
#define READ_PREFERENCE(field) parsed.field = node[#field].as<decltype(field)>(parsed.field)
				READ_PREFERENCE(Subtitles);
				READ_PREFERENCE(Captions);
				READ_PREFERENCE(VisualCues);
				READ_PREFERENCE(SpeakerNames);
				READ_PREFERENCE(DirectionIndicators);
				READ_PREFERENCE(Mono);
				READ_PREFERENCE(AudioDescriptions);
				READ_PREFERENCE(TextSize);
				READ_PREFERENCE(BackgroundOpacity);
				READ_PREFERENCE(DurationMultiplier);
				READ_PREFERENCE(MaxLines);
				READ_PREFERENCE(DialogueBoost);
#undef READ_PREFERENCE
				const uint32_t range = node["DynamicRange"].as<uint32_t>(0);
				if (range > 2)
					throw std::runtime_error("invalid dynamic range");
				parsed.DynamicRange = static_cast<AudioDynamicRange>(range);
				if (auto volumes = node["Volumes"])
				{
					for (size_t i = 0; i < AudioCategoryCount; ++i)
						parsed.Volumes[i] = volumes[AudioCategoryNames[i]].as<float>(1.0f);
				}
			}
			if (!parsed.Validate())
				return false;
			*this = parsed;
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot load accessibility preferences: {}", error.what());
			return false;
		}
	}

	bool AudioAccessibilityConfig::Validate() const
	{
		if (!Defaults.Validate() || !DialogueTable::ValidLanguage(DefaultLanguage) || !Range(DescriptionDuck, 0, 1) || Events.size() > 100000 || SpeakerColors.size() > 4096)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid accessibility defaults, language, ducking or metadata count");
			return false;
		}
		std::set<std::string> paths;
		for (size_t i = 0; i < AudioCategoryCount; ++i)
		{
			const auto& path = BusPaths[i];
			if (!path.empty() && (!Text(path, 512) || !path.starts_with("bus:/") || !paths.insert(path).second || (i != 0 && path == "bus:/")))
			{
				LUX_CORE_ERROR_TAG("Audio", "Accessibility bus mappings must be unique bus paths; only Master may use bus:/");
				return false;
			}
		}
		for (size_t i = 1; i < AudioCategoryCount; ++i)
		{
			for (size_t j = i + 1; j < AudioCategoryCount; ++j)
			{
				if (!BusPaths[i].empty() && !BusPaths[j].empty() &&
					(BusPaths[i].starts_with(BusPaths[j] + "/") || BusPaths[j].starts_with(BusPaths[i] + "/")))
				{
					LUX_CORE_ERROR_TAG("Audio", "Accessibility category buses must not contain each other: '{}' and '{}'", BusPaths[i], BusPaths[j]);
					return false;
				}
			}
		}
		for (const auto& [guid, event] : Events)
		{
			if (guid.empty() || !Text(guid, 512) || event.Category >= AudioCategory::Count || !Range(event.Intensity, 0, 1) || !Range(event.MaxDistance, 0.01f, 100000) || event.Captions.size() > 64)
			{
				LUX_CORE_ERROR_TAG("Audio", "Invalid accessibility event metadata for {}", guid);
				return false;
			}
			for (const auto& [language, caption] : event.Captions)
			{
				if (!DialogueTable::ValidLanguage(language) || !Text(caption, 16384))
				{
					LUX_CORE_ERROR_TAG("Audio", "Invalid caption language or text for {}", guid);
					return false;
				}
			}
		}
		for (const auto& [name, color] : SpeakerColors)
		{
			if (!Text(name, 512) || name.empty() || !Range(color.r, 0, 1) || !Range(color.g, 0, 1) || !Range(color.b, 0, 1) || !Range(color.a, 0, 1))
			{
				LUX_CORE_ERROR_TAG("Audio", "Invalid subtitle speaker color for {}", name);
				return false;
			}
		}
		return true;
	}

	void AudioAccessibilityConfig::SerializeYAML(YAML::Emitter& out) const
	{
		out << YAML::BeginMap << YAML::Key << "Version" << YAML::Value << 1;
		out << YAML::Key << "BuiltInUI" << YAML::Value << BuiltInUI;
		out << YAML::Key << "DefaultLanguage" << YAML::Value << DefaultLanguage;
		out << YAML::Key << "DescriptionDuck" << YAML::Value << DescriptionDuck;
		out << YAML::Key << "Defaults" << YAML::Value;
		Defaults.SerializeYAML(out);
		out << YAML::Key << "Buses" << YAML::BeginMap;
		for (size_t i = 0; i < AudioCategoryCount; ++i)
			out << YAML::Key << AudioCategoryNames[i] << YAML::Value << BusPaths[i];
		out << YAML::EndMap << YAML::Key << "Events" << YAML::BeginMap;
		for (const auto& [guid, event] : Events)
		{
			out << YAML::Key << guid << YAML::BeginMap;
			out << YAML::Key << "Category" << YAML::Value << static_cast<uint32_t>(event.Category);
			out << YAML::Key << "VisualCue" << YAML::Value << event.VisualCue;
			out << YAML::Key << "Intensity" << YAML::Value << event.Intensity;
			out << YAML::Key << "MaxDistance" << YAML::Value << event.MaxDistance;
			out << YAML::Key << "Captions" << YAML::BeginMap;
			for (const auto& [language, caption] : event.Captions)
				out << YAML::Key << language << YAML::Value << caption;
			out << YAML::EndMap << YAML::EndMap;
		}
		out << YAML::EndMap << YAML::Key << "SpeakerColors" << YAML::BeginMap;
		for (const auto& [name, color] : SpeakerColors)
			out << YAML::Key << name << YAML::Flow << YAML::BeginSeq << color.r << color.g << color.b << color.a << YAML::EndSeq;
		out << YAML::EndMap << YAML::EndMap;
	}

	bool AudioAccessibilityConfig::DeserializeYAML(const YAML::Node& node)
	{
		try
		{
			AudioAccessibilityConfig parsed;
			if (!node || node.IsNull())
			{
				*this = parsed;
				return true;
			}
			if (!node.IsMap() || node["Version"].as<int>(1) != 1)
				throw std::runtime_error("unsupported accessibility format");
			parsed.BuiltInUI = node["BuiltInUI"].as<bool>(true);
			parsed.DefaultLanguage = node["DefaultLanguage"].as<std::string>("en");
			parsed.DescriptionDuck = node["DescriptionDuck"].as<float>(0.25f);
			if (!parsed.Defaults.DeserializeYAML(node["Defaults"]))
				return false;
			if (auto buses = node["Buses"])
			{
				for (size_t i = 0; i < AudioCategoryCount; ++i)
					parsed.BusPaths[i] = buses[AudioCategoryNames[i]].as<std::string>(parsed.BusPaths[i]);
			}
			if (auto events = node["Events"])
			{
				if (!events.IsMap() || events.size() > 100000)
					throw std::runtime_error("invalid event caption map");
				for (const auto& entry : events)
				{
					AudioEventAccessibility event;
					const auto value = entry.second;
					const auto category = value["Category"].as<uint32_t>(2);
					if (category >= AudioCategoryCount)
						throw std::runtime_error("invalid sound category");
					event.Category = static_cast<AudioCategory>(category);
					event.VisualCue = value["VisualCue"].as<bool>(false);
					event.Intensity = value["Intensity"].as<float>(1);
					event.MaxDistance = value["MaxDistance"].as<float>(50);
					if (auto captions = value["Captions"])
					{
						if (!captions.IsMap() || captions.size() > 64)
							throw std::runtime_error("invalid caption translations");
						for (const auto& caption : captions)
						{
							if (!event.Captions.emplace(caption.first.as<std::string>(), caption.second.as<std::string>()).second)
								throw std::runtime_error("duplicate caption language");
						}
					}
					if (!parsed.Events.emplace(entry.first.as<std::string>(), std::move(event)).second)
						throw std::runtime_error("duplicate caption event");
				}
			}
			if (auto colors = node["SpeakerColors"])
			{
				if (!colors.IsMap() || colors.size() > 4096)
					throw std::runtime_error("invalid speaker colors");
				for (const auto& entry : colors)
				{
					if (!entry.second.IsSequence() || entry.second.size() != 4)
						throw std::runtime_error("speaker color requires RGBA");
					const auto color = entry.second;
					if (!parsed.SpeakerColors.emplace(entry.first.as<std::string>(), glm::vec4(color[0].as<float>(), color[1].as<float>(), color[2].as<float>(), color[3].as<float>())).second)
						throw std::runtime_error("duplicate speaker color");
				}
			}
			if (!parsed.Validate())
				throw std::runtime_error("invalid accessibility configuration values");
			*this = std::move(parsed);
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot load audio accessibility configuration: {}", error.what());
			return false;
		}
	}

	bool AudioAccessibilityConfig::Serialize(StreamWriter& stream) const
	{
		if (!Validate())
			return false;
		YAML::Emitter out;
		SerializeYAML(out);
		const std::string text = out.c_str();
		if (text.size() > kMaxConfigBytes)
		{
			LUX_CORE_ERROR_TAG("Audio", "Audio accessibility configuration exceeds 8 MiB");
			return false;
		}
		stream.WriteRaw<uint32_t>(static_cast<uint32_t>(text.size()));
		return stream.WriteData(text.data(), text.size()) && stream.IsStreamGood();
	}

	bool AudioAccessibilityConfig::Deserialize(StreamReader& stream)
	{
		uint32_t length = 0;
		if (!stream.ReadData(reinterpret_cast<char*>(&length), sizeof(length)) || length > kMaxConfigBytes)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid runtime accessibility block length");
			return false;
		}
		std::string text(length, '\0');
		if (!stream.ReadData(text.data(), text.size()))
		{
			LUX_CORE_ERROR_TAG("Audio", "Truncated runtime accessibility block");
			return false;
		}
		try
		{
			return DeserializeYAML(YAML::Load(text));
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid runtime accessibility YAML: {}", error.what());
			return false;
		}
	}
}
