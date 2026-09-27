// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioPerformanceSettings.h"
#include "Lux/Serialization/StreamReader.h"
#include "Lux/Serialization/StreamWriter.h"
#include "Lux/Serialization/Yaml.h"
#include <cmath>
namespace Lux
{
	namespace
	{
		constexpr uint32_t kMaximumSettingsBytes = 65536;
	}
	bool AudioPerformanceSettings::Validate() const
	{
		if (RealVoices == 0 || RealVoices > 512 || !std::isfinite(CPUPercent) || CPUPercent <= 0 || CPUPercent > 100 ||
			!std::isfinite(RaytracingMilliseconds) || RaytracingMilliseconds <= 0 || RaytracingMilliseconds > 1000 ||
			BankMemoryMiB == 0 || BankMemoryMiB > 65536 || BusVoices.size() > 64)
			return false;
		for (const auto& [path, limit] : BusVoices)
			if (!path.starts_with("bus:/") || path.size() > 1024 || path.find('\0') != std::string::npos || limit == 0 || limit > 65536)
				return false;
		return true;
	}
	void AudioPerformanceSettings::SerializeYAML(Yaml::Writer& out) const
	{
		out << Yaml::BeginMap;
		out << Yaml::Key << "MuteWhenUnfocused" << Yaml::Value << MuteWhenUnfocused;
		out << Yaml::Key << "RealVoices" << Yaml::Value << RealVoices;
		out << Yaml::Key << "CPUPercent" << Yaml::Value << CPUPercent;
		out << Yaml::Key << "RaytracingMilliseconds" << Yaml::Value << RaytracingMilliseconds;
		out << Yaml::Key << "BankMemoryMiB" << Yaml::Value << BankMemoryMiB;
		out << Yaml::Key << "BusVoices" << Yaml::BeginMap;
		for (const auto& [path, limit] : BusVoices)
			out << Yaml::Key << path << Yaml::Value << limit;
		out << Yaml::EndMap << Yaml::EndMap;
	}
	bool AudioPerformanceSettings::DeserializeYAML(const Yaml::Node& node)
	{
		try
		{
			AudioPerformanceSettings parsed;
			if (node)
			{
				if (!node.IsMap())
					throw std::runtime_error("expected settings map");
				if (node["MuteWhenUnfocused"])
					parsed.MuteWhenUnfocused = node["MuteWhenUnfocused"].as<bool>();
				if (node["RealVoices"])
					parsed.RealVoices = node["RealVoices"].as<uint32_t>();
				if (node["CPUPercent"])
					parsed.CPUPercent = node["CPUPercent"].as<float>();
				if (node["RaytracingMilliseconds"])
					parsed.RaytracingMilliseconds = node["RaytracingMilliseconds"].as<float>();
				if (node["BankMemoryMiB"])
					parsed.BankMemoryMiB = node["BankMemoryMiB"].as<uint32_t>();
				if (auto buses = node["BusVoices"])
				{
					if (!buses.IsMap() || buses.size() > 64)
						throw std::runtime_error("expected at most 64 bus budgets");
					parsed.BusVoices.clear();
					for (const auto& entry : buses)
						if (!parsed.BusVoices.emplace(entry.first.as<std::string>(), entry.second.as<uint32_t>()).second)
							throw std::runtime_error("duplicate bus budget");
				}
			}
			if (!parsed.Validate())
				throw std::runtime_error("budget values are out of range");
			*this = std::move(parsed);
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid performance settings: {}", error.what());
			return false;
		}
	}
	bool AudioPerformanceSettings::Serialize(StreamWriter& stream) const
	{
		if (!Validate())
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot export invalid audio performance settings");
			return false;
		}
		Yaml::Writer out;
		SerializeYAML(out);
		const std::string text = out.c_str();
		if (text.size() > kMaximumSettingsBytes)
		{
			LUX_CORE_ERROR_TAG("Audio", "Audio performance settings exceed 64 KiB");
			return false;
		}
		stream.WriteRaw<uint32_t>(static_cast<uint32_t>(text.size()));
		return stream.WriteData(text.data(), text.size()) && stream.IsStreamGood();
	}
	bool AudioPerformanceSettings::Deserialize(StreamReader& stream)
	{
		uint32_t size = 0;
		if (!stream.ReadData(reinterpret_cast<char*>(&size), sizeof(size)) || size == 0 || size > kMaximumSettingsBytes)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid runtime performance settings length");
			return false;
		}
		std::string text(size, '\0');
		if (!stream.ReadData(text.data(), text.size()))
		{
			LUX_CORE_ERROR_TAG("Audio", "Truncated runtime performance settings");
			return false;
		}
		try
		{
			return DeserializeYAML(Yaml::Load(text));
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid runtime performance settings: {}", error.what());
			return false;
		}
	}
	bool IsValidStudioPlatform(const std::string& name)
	{
		return !name.empty() && name.size() <= 64 && name.front() != ' ' && name.back() != ' ' &&
			std::all_of(name.begin(), name.end(), [](unsigned char c)
			{
				return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-';
			});
	}
	bool AudioDesktopProfile::Validate() const
	{
		return IsValidStudioPlatform(StudioPlatform) && !BankOutputPath.empty() &&
			BankOutputPath.generic_string().find('\0') == std::string::npos && Performance.Validate();
	}
	void AudioDesktopProfile::SerializeYAML(Yaml::Writer& out) const
	{
		out << Yaml::BeginMap;
		out << Yaml::Key << "Enabled" << Yaml::Value << Enabled;
		out << Yaml::Key << "StudioPlatform" << Yaml::Value << StudioPlatform;
		out << Yaml::Key << "BankOutputPath" << Yaml::Value << BankOutputPath.generic_string();
		out << Yaml::Key << "Performance" << Yaml::Value;
		Performance.SerializeYAML(out);
		out << Yaml::EndMap;
	}
	bool AudioDesktopProfile::DeserializeYAML(const Yaml::Node& node)
	{
		try
		{
			AudioDesktopProfile parsed;
			if (node)
			{
				if (!node.IsMap())
					throw std::runtime_error("expected a desktop audio profile map");
				if (node["Enabled"])
					parsed.Enabled = node["Enabled"].as<bool>();
				if (node["StudioPlatform"])
					parsed.StudioPlatform = node["StudioPlatform"].as<std::string>();
				if (node["BankOutputPath"])
					parsed.BankOutputPath = node["BankOutputPath"].as<std::string>();
				if (!parsed.Performance.DeserializeYAML(node["Performance"]))
					return false;
			}
			if (!parsed.Validate())
				throw std::runtime_error("invalid platform name, bank directory or performance budget");
			*this = std::move(parsed);
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid desktop audio profile: {}", error.what());
			return false;
		}
	}

}
