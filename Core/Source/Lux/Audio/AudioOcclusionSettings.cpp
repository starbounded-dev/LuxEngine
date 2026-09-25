// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioOcclusionSettings.h"
#include "Lux/Serialization/StreamReader.h"
#include "Lux/Serialization/StreamWriter.h"
#include <yaml-cpp/yaml.h>
#include <cmath>
namespace Lux
{
	namespace
	{
		constexpr uint32_t kMaximumSettingsBytes = 4096;
		constexpr float kMinimumRateHz = 1.0f;
		constexpr float kMaximumRateHz = 120.0f;
		constexpr uint32_t kMaximumCastBudget = 1024;
		constexpr float kMaximumStrength = 4.0f;

		const char* SourceName(AudioOcclusionSource source)
		{
			return source == AudioOcclusionSource::Raytraced ? "Raytraced" : "Engine";
		}
	}
	bool AudioOcclusionSettings::Validate() const
	{
		return Source <= AudioOcclusionSource::Raytraced && std::isfinite(UpdateRateHz) && UpdateRateHz >= kMinimumRateHz &&
			UpdateRateHz <= kMaximumRateHz && CastBudget > 0 && CastBudget <= kMaximumCastBudget && std::isfinite(Strength) &&
			Strength >= 0.0f && Strength <= kMaximumStrength;
	}
	void AudioOcclusionSettings::SerializeYAML(YAML::Emitter& out) const
	{
		out << YAML::BeginMap;
		out << YAML::Key << "Source" << YAML::Value << SourceName(Source);
		out << YAML::Key << "UpdateRateHz" << YAML::Value << UpdateRateHz;
		out << YAML::Key << "CastBudget" << YAML::Value << CastBudget;
		out << YAML::Key << "Strength" << YAML::Value << Strength;
		out << YAML::EndMap;
	}
	bool AudioOcclusionSettings::DeserializeYAML(const YAML::Node& node)
	{
		try
		{
			AudioOcclusionSettings parsed;
			if (node)
			{
				if (!node.IsMap())
					throw std::runtime_error("expected settings map");
				if (node["Source"])
				{
					const std::string source = node["Source"].as<std::string>();
					if (source == "Engine")
						parsed.Source = AudioOcclusionSource::Engine;
					else if (source == "Raytraced")
						parsed.Source = AudioOcclusionSource::Raytraced;
					else
						throw std::runtime_error("unknown source '" + source + "'");
				}
				if (node["UpdateRateHz"])
					parsed.UpdateRateHz = node["UpdateRateHz"].as<float>();
				if (node["CastBudget"])
					parsed.CastBudget = node["CastBudget"].as<uint32_t>();
				if (node["Strength"])
					parsed.Strength = node["Strength"].as<float>();
			}
			if (!parsed.Validate())
				throw std::runtime_error("values are out of range");
			*this = parsed;
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid occlusion settings: {}", error.what());
			return false;
		}
	}
	bool AudioOcclusionSettings::Serialize(StreamWriter& stream) const
	{
		if (!Validate())
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot export invalid audio occlusion settings");
			return false;
		}
		YAML::Emitter out;
		SerializeYAML(out);
		const std::string text = out.c_str();
		if (text.size() > kMaximumSettingsBytes)
		{
			LUX_CORE_ERROR_TAG("Audio", "Audio occlusion settings exceed 4 KiB");
			return false;
		}
		stream.WriteRaw<uint32_t>(static_cast<uint32_t>(text.size()));
		return stream.WriteData(text.data(), text.size()) && stream.IsStreamGood();
	}
	bool AudioOcclusionSettings::Deserialize(StreamReader& stream)
	{
		uint32_t size = 0;
		if (!stream.ReadData(reinterpret_cast<char*>(&size), sizeof(size)) || size == 0 || size > kMaximumSettingsBytes)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid runtime occlusion settings length");
			return false;
		}
		std::string text(size, '\0');
		if (!stream.ReadData(text.data(), text.size()))
		{
			LUX_CORE_ERROR_TAG("Audio", "Truncated runtime occlusion settings");
			return false;
		}
		try
		{
			return DeserializeYAML(YAML::Load(text));
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid runtime occlusion settings: {}", error.what());
			return false;
		}
	}
}
