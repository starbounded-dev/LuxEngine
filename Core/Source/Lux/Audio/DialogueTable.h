#pragma once

#include <cstdint>
#include "AudioEventRef.h"
#include "Lux/Asset/Asset.h"
#include <map>

namespace Lux
{
	class StreamReader;
	class StreamWriter;
	enum class DialoguePriority : uint8_t { Low, Normal, High, Critical };
	enum class DialogueQueueMode : uint8_t { Interrupt, Queue, DropIfBusy };

	struct DialogueTranslation
	{
		std::string Text, SpeakerName, AudioKey;
	};

	struct DialogueLine
	{
		AudioEventRef Event;
		DialoguePriority Priority = DialoguePriority::Normal;
		bool Interruptible = true;
		std::map<std::string, DialogueTranslation> Translations;
	};

	class DialogueTable : public Asset
	{
	public:
		static AssetType GetStaticType() { return AssetType::DialogueTable; }
		AssetType GetAssetType() const override { return GetStaticType(); }
		std::string DefaultLanguage = "en";
		std::map<std::string, DialogueLine> Lines;
		float BarkCooldown = 2.0f, BarkRadius = 10.0f;
		bool Validate() const;
		std::string ToYAML() const;
		bool FromYAML(const std::string& text);
		static bool ValidLanguage(const std::string& language);
	};

	struct DialogueSettings
	{
		AssetHandle Table = 0;
		std::string Language = "en";
		bool Serialize(StreamWriter& stream) const;
		bool Deserialize(StreamReader& stream);
	};
}
