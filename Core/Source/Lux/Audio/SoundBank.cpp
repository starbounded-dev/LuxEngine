// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SoundBank.h"

#include "Lux/Serialization/Yaml.h"

#include <algorithm>
#include <unordered_set>

namespace Lux
{
	namespace
	{
		constexpr int kVersion = 1;
	}

	bool SoundBank::Add(AssetHandle sound)
	{
		if (!sound || Contains(sound) || Sounds.size() >= MaxSounds)
			return false;
		Sounds.push_back(sound);
		return true;
	}

	bool SoundBank::Remove(AssetHandle sound)
	{
		const auto it = std::find(Sounds.begin(), Sounds.end(), sound);
		if (it == Sounds.end())
			return false;
		Sounds.erase(it);
		return true;
	}

	bool SoundBank::Contains(AssetHandle sound) const
	{
		return std::find(Sounds.begin(), Sounds.end(), sound) != Sounds.end();
	}

	bool SoundBank::Validate(std::string* error) const
	{
		const auto fail = [error](const char* message)
		{
			if (error)
				*error = message;
			return false;
		};
		if (Sounds.size() > MaxSounds)
			return fail("a bank holds at most 2048 Sounds");
		std::unordered_set<uint64_t> seen;
		for (const AssetHandle sound : Sounds)
		{
			if (!sound)
				return fail("a bank lists an empty Sound");
			if (!seen.insert(static_cast<uint64_t>(sound)).second)
				return fail("a bank lists the same Sound twice");
		}
		return true;
	}

	std::string SoundBank::ToYAML() const
	{
		Yaml::Writer out;
		out << Yaml::BeginMap << Yaml::Key << "SoundBank" << Yaml::BeginMap;
		out << Yaml::Key << "Version" << Yaml::Value << kVersion;
		out << Yaml::Key << "Resident" << Yaml::Value << Resident;
		out << Yaml::Key << "Sounds" << Yaml::Value << Yaml::BeginSeq;
		for (const AssetHandle sound : Sounds)
			out << static_cast<uint64_t>(sound);
		out << Yaml::EndSeq;
		out << Yaml::EndMap << Yaml::EndMap;
		return out.c_str();
	}

	bool SoundBank::FromYAML(const std::string& text)
	{
		try
		{
			const auto node = Yaml::Load(text)["SoundBank"];
			if (!node || !node.IsMap())
				throw std::runtime_error("missing SoundBank map");
			if (node["Version"].as<int>(kVersion) != kVersion)
				throw std::runtime_error("unsupported version");
			SoundBank parsed;
			parsed.Resident = node["Resident"].as<bool>(false);
			if (const auto sounds = node["Sounds"])
			{
				if (!sounds.IsSequence() || sounds.size() > MaxSounds)
					throw std::runtime_error("Sounds must be a list of at most 2048 handles");
				for (const auto& sound : sounds)
					parsed.Sounds.push_back(sound.as<uint64_t>(0));
			}
			std::string error;
			if (!parsed.Validate(&error))
				throw std::runtime_error(error);
			Sounds = std::move(parsed.Sounds);
			Resident = parsed.Resident;
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid sound bank: {}", error.what());
			return false;
		}
	}
}
