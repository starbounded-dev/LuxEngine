#include "lpch.h"
#include "DialogueTable.h"
#include "Lux/Serialization/StreamReader.h"
#include "Lux/Serialization/StreamWriter.h"
#include <yaml-cpp/yaml.h>
#include <cmath>

namespace Lux
{
	namespace
	{
		constexpr size_t kMaxLines = 100000, kMaxLanguages = 64, kMaxTextBytes = 16384, kMaxKeyBytes = 512;
		bool ValidText(const std::string& text, size_t limit, bool allowEmpty = false)
		{
			return (allowEmpty || !text.empty()) && text.size() <= limit && text.find('\0') == std::string::npos;
		}
	}

	bool DialogueTable::ValidLanguage(const std::string& language)
	{
		return ValidText(language, 63) && std::all_of(language.begin(), language.end(), [](unsigned char c)
		{
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
		});
	}

	bool DialogueTable::Validate() const
	{
		if (!ValidLanguage(DefaultLanguage) || Lines.size() > kMaxLines || !std::isfinite(BarkCooldown) || BarkCooldown < 0.0f
			|| !std::isfinite(BarkRadius) || BarkRadius < 0.0f)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid dialogue table language, size or bark settings");
			return false;
		}
		for (const auto& [key, line] : Lines)
		{
			if (!ValidText(key, kMaxKeyBytes) || !line.Event.IsValid() || line.Priority > DialoguePriority::Critical
				|| line.Translations.size() > kMaxLanguages || !line.Translations.contains(DefaultLanguage))
			{
				LUX_CORE_ERROR_TAG("Audio", "Dialogue line '{}' needs an event and a translation in '{}'", key, DefaultLanguage);
				return false;
			}
			for (const auto& [language, translation] : line.Translations)
			{
				if (!ValidLanguage(language) || !ValidText(translation.Text, kMaxTextBytes)
					|| !ValidText(translation.SpeakerName, kMaxKeyBytes, true) || !ValidText(translation.AudioKey, kMaxKeyBytes, true))
				{
					LUX_CORE_ERROR_TAG("Audio", "Invalid '{}' translation for dialogue '{}'", language, key);
					return false;
				}
			}
		}
		return true;
	}

	std::string DialogueTable::ToYAML() const
	{
		YAML::Emitter out;
		out << YAML::BeginMap << YAML::Key << "Version" << YAML::Value << 1;
		out << YAML::Key << "DefaultLanguage" << YAML::Value << DefaultLanguage;
		out << YAML::Key << "BarkCooldown" << YAML::Value << BarkCooldown;
		out << YAML::Key << "BarkRadius" << YAML::Value << BarkRadius;
		out << YAML::Key << "Lines" << YAML::BeginMap;
		for (const auto& [key, line] : Lines)
		{
			out << YAML::Key << key << YAML::BeginMap;
			out << YAML::Key << "Event" << YAML::BeginMap;
			out << YAML::Key << "Guid" << YAML::Value << line.Event.Guid;
			out << YAML::Key << "Path" << YAML::Value << line.Event.Path;
			out << YAML::Key << "BankName" << YAML::Value << line.Event.BankName << YAML::EndMap;
			out << YAML::Key << "Priority" << YAML::Value << static_cast<uint32_t>(line.Priority);
			out << YAML::Key << "Interruptible" << YAML::Value << line.Interruptible;
			out << YAML::Key << "Translations" << YAML::BeginMap;
			for (const auto& [language, translation] : line.Translations)
			{
				out << YAML::Key << language << YAML::BeginMap;
				out << YAML::Key << "Text" << YAML::Value << translation.Text;
				out << YAML::Key << "SpeakerName" << YAML::Value << translation.SpeakerName;
				out << YAML::Key << "AudioKey" << YAML::Value << translation.AudioKey << YAML::EndMap;
			}
			out << YAML::EndMap << YAML::EndMap;
		}
		out << YAML::EndMap << YAML::EndMap;
		return out.c_str();
	}

	bool DialogueTable::FromYAML(const std::string& text)
	{
		try
		{
			const auto root = YAML::Load(text);
			if (!root.IsMap() || root["Version"].as<int>(1) != 1)
				throw std::runtime_error("unsupported dialogue table version");
			DialogueTable parsed;
			parsed.DefaultLanguage = root["DefaultLanguage"].as<std::string>("en");
			parsed.BarkCooldown = root["BarkCooldown"].as<float>(2.0f);
			parsed.BarkRadius = root["BarkRadius"].as<float>(10.0f);
			if (auto lines = root["Lines"])
			{
				if (!lines.IsMap() || lines.size() > kMaxLines)
					throw std::runtime_error("invalid dialogue line map");
				for (const auto& entry : lines)
				{
					const auto key = entry.first.as<std::string>();
					if (parsed.Lines.contains(key))
						throw std::runtime_error("duplicate dialogue key: " + key);
					auto& line = parsed.Lines[key];
					const auto node = entry.second;
					if (auto reference = node["Event"])
					{
						line.Event.Guid = reference["Guid"].as<std::string>("");
						line.Event.Path = reference["Path"].as<std::string>("");
						line.Event.BankName = reference["BankName"].as<std::string>("");
					}
					const auto priority = node["Priority"].as<uint32_t>(1);
					if (priority > static_cast<uint32_t>(DialoguePriority::Critical))
						throw std::runtime_error("invalid dialogue priority");
					line.Priority = static_cast<DialoguePriority>(priority);
					line.Interruptible = node["Interruptible"].as<bool>(true);
					const auto translations = node["Translations"];
					if (!translations.IsMap() || translations.size() > kMaxLanguages)
						throw std::runtime_error("invalid translations for " + key);
					for (const auto& localized : translations)
					{
						const auto language = localized.first.as<std::string>();
						if (line.Translations.contains(language))
							throw std::runtime_error("duplicate dialogue language: " + language);
						auto& value = line.Translations[language];
						value.Text = localized.second["Text"].as<std::string>("");
						value.SpeakerName = localized.second["SpeakerName"].as<std::string>("");
						value.AudioKey = localized.second["AudioKey"].as<std::string>("");
					}
				}
			}
			if (!parsed.Validate())
				return false;
			DefaultLanguage = std::move(parsed.DefaultLanguage);
			Lines = std::move(parsed.Lines);
			BarkCooldown = parsed.BarkCooldown;
			BarkRadius = parsed.BarkRadius;
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot load dialogue table: {}", error.what());
			return false;
		}
	}

	bool DialogueSettings::Serialize(StreamWriter& stream) const
	{
		if (!DialogueTable::ValidLanguage(Language))
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid dialogue language '{}'", Language);
			return false;
		}
		stream.WriteRaw<uint64_t>(Table);
		stream.WriteRaw<uint32_t>(static_cast<uint32_t>(Language.size()));
		return stream.WriteData(Language.data(), Language.size()) && stream.IsStreamGood();
	}

	bool DialogueSettings::Deserialize(StreamReader& stream)
	{
		uint64_t table = 0;
		uint32_t size = 0;
		if (!stream.ReadData(reinterpret_cast<char*>(&table), sizeof(table)) || !stream.ReadData(reinterpret_cast<char*>(&size), sizeof(size)) || size == 0 || size > 63)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid or truncated runtime dialogue settings");
			return false;
		}
		std::string language(size, '\0');
		if (!stream.ReadData(language.data(), size) || !DialogueTable::ValidLanguage(language))
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid runtime dialogue language");
			return false;
		}
		Table = table;
		Language = std::move(language);
		return true;
	}
}
