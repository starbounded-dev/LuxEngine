// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioEventRefSerializer.h"

#include "Lux/Serialization/Yaml.h"

namespace Lux::AudioEventRefSerializer
{
	namespace
	{
		std::string Join(std::string_view prefix, std::string_view name)
		{
			std::string key(prefix);
			key += name;
			return key;
		}
	}

	void Write(Yaml::Writer& out, std::string_view key, const AudioEventRef& reference)
	{
		out << Yaml::Key << key << Yaml::BeginMap;
		out << Yaml::Key << "Guid" << Yaml::Value << reference.Guid;
		out << Yaml::Key << "Path" << Yaml::Value << reference.Path;
		out << Yaml::Key << "BankName" << Yaml::Value << reference.BankName;
		if (reference.Sound != 0)
			out << Yaml::Key << "Sound" << Yaml::Value << reference.Sound;
		out << Yaml::EndMap;
	}

	AudioEventRef Read(const Yaml::Node& node)
	{
		AudioEventRef reference;
		if (!node)
			return reference;
		reference.Guid = node["Guid"].as<std::string>("");
		reference.Path = node["Path"].as<std::string>("");
		reference.BankName = node["BankName"].as<std::string>("");
		reference.Sound = node["Sound"].as<uint64_t>(0);
		return reference;
	}

	void WriteFlat(Yaml::Writer& out, std::string_view prefix, const AudioEventRef& reference)
	{
		out << Yaml::Key << Join(prefix, "Guid") << Yaml::Value << reference.Guid;
		out << Yaml::Key << Join(prefix, "Path") << Yaml::Value << reference.Path;
		out << Yaml::Key << Join(prefix, "Bank") << Yaml::Value << reference.BankName;
		if (reference.Sound != 0)
			out << Yaml::Key << Join(prefix, "Sound") << Yaml::Value << reference.Sound;
	}

	AudioEventRef ReadFlat(const Yaml::Node& node, std::string_view prefix)
	{
		AudioEventRef reference;
		reference.Guid = node[Join(prefix, "Guid")].as<std::string>("");
		reference.Path = node[Join(prefix, "Path")].as<std::string>("");
		reference.BankName = node[Join(prefix, "Bank")].as<std::string>("");
		reference.Sound = node[Join(prefix, "Sound")].as<uint64_t>(0);
		return reference;
	}
}
