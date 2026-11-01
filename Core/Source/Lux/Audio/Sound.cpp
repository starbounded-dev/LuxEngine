// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Sound.h"

#include "Lux/Serialization/Yaml.h"

#include <cmath>

namespace Lux
{
	namespace
	{
		constexpr int kVersion = 1;
		constexpr size_t kMaxBusBytes = 256;
		constexpr float kMaxVolume = 4.0f, kMaxPitch = 4.0f;

		bool Fail(std::string* error, const char* message)
		{
			if (error)
				*error = message;
			return false;
		}
	}

	const char* SoundAttenuationName(SoundAttenuation attenuation)
	{
		switch (attenuation)
		{
			case SoundAttenuation::None:    return "None";
			case SoundAttenuation::Inverse: return "Inverse";
			case SoundAttenuation::Linear:  return "Linear";
		}
		return "Inverse";
	}

	bool ParseSoundAttenuation(const std::string& name, SoundAttenuation& outAttenuation)
	{
		for (const auto attenuation : { SoundAttenuation::None, SoundAttenuation::Inverse, SoundAttenuation::Linear })
		{
			if (name == SoundAttenuationName(attenuation))
			{
				outAttenuation = attenuation;
				return true;
			}
		}
		return false;
	}

	bool Sound::Validate(std::string* error) const
	{
		if (Bus.empty() || Bus.size() > kMaxBusBytes || !Bus.starts_with("bus:/") || Bus.find('\0') != std::string::npos)
			return Fail(error, "the bus must be a bus:/ path of at most 256 bytes");
		if (!std::isfinite(Volume) || Volume < 0.0f || Volume > kMaxVolume)
			return Fail(error, "volume must be finite and between 0 and 4");
		if (!std::isfinite(Pitch) || Pitch <= 0.0f || Pitch > kMaxPitch)
			return Fail(error, "pitch must be finite, above 0 and at most 4");
		const auto& spatial = Spatial;
		if (!std::isfinite(spatial.MinDistance) || !std::isfinite(spatial.MaxDistance) || spatial.MinDistance <= 0.0f
			|| spatial.MaxDistance < spatial.MinDistance)
			return Fail(error, "distances must be finite, with 0 < minimum <= maximum");
		if (!std::isfinite(spatial.Rolloff) || spatial.Rolloff < 0.0f)
			return Fail(error, "rolloff must be finite and not negative");
		return true;
	}

	std::string Sound::ToYAML() const
	{
		Yaml::Writer out;
		out << Yaml::BeginMap << Yaml::Key << "Sound" << Yaml::BeginMap;
		out << Yaml::Key << "Version" << Yaml::Value << kVersion;
		out << Yaml::Key << "Clip" << Yaml::Value << Clip;
		out << Yaml::Key << "Bus" << Yaml::Value << Bus;
		out << Yaml::Key << "Volume" << Yaml::Value << Volume;
		out << Yaml::Key << "Pitch" << Yaml::Value << Pitch;
		out << Yaml::Key << "Loop" << Yaml::Value << Loop;
		out << Yaml::Key << "Spatial" << Yaml::BeginMap;
		out << Yaml::Key << "Enabled" << Yaml::Value << Spatial.Enabled;
		out << Yaml::Key << "Attenuation" << Yaml::Value << SoundAttenuationName(Spatial.Attenuation);
		out << Yaml::Key << "MinDistance" << Yaml::Value << Spatial.MinDistance;
		out << Yaml::Key << "MaxDistance" << Yaml::Value << Spatial.MaxDistance;
		out << Yaml::Key << "Rolloff" << Yaml::Value << Spatial.Rolloff;
		out << Yaml::EndMap << Yaml::EndMap << Yaml::EndMap;
		return out.c_str();
	}

	bool Sound::FromYAML(const std::string& text)
	{
		try
		{
			const auto node = Yaml::Load(text)["Sound"];
			if (!node || !node.IsMap())
				throw std::runtime_error("missing Sound map");
			if (node["Version"].as<int>(kVersion) != kVersion)
				throw std::runtime_error("unsupported version");
			Sound parsed;
			parsed.Clip = node["Clip"].as<uint64_t>(0);
			parsed.Bus = node["Bus"].as<std::string>(parsed.Bus);
			parsed.Volume = node["Volume"].as<float>(parsed.Volume);
			parsed.Pitch = node["Pitch"].as<float>(parsed.Pitch);
			parsed.Loop = node["Loop"].as<bool>(parsed.Loop);
			if (const auto spatial = node["Spatial"])
			{
				if (!spatial.IsMap())
					throw std::runtime_error("Spatial must be a map");
				parsed.Spatial.Enabled = spatial["Enabled"].as<bool>(parsed.Spatial.Enabled);
				if (const auto attenuation = spatial["Attenuation"];
					attenuation && !ParseSoundAttenuation(attenuation.as<std::string>(""), parsed.Spatial.Attenuation))
					throw std::runtime_error("unknown attenuation model");
				parsed.Spatial.MinDistance = spatial["MinDistance"].as<float>(parsed.Spatial.MinDistance);
				parsed.Spatial.MaxDistance = spatial["MaxDistance"].as<float>(parsed.Spatial.MaxDistance);
				parsed.Spatial.Rolloff = spatial["Rolloff"].as<float>(parsed.Spatial.Rolloff);
			}
			std::string error;
			if (!parsed.Validate(&error))
				throw std::runtime_error(error);
			Clip = parsed.Clip;
			Bus = std::move(parsed.Bus);
			Volume = parsed.Volume;
			Pitch = parsed.Pitch;
			Loop = parsed.Loop;
			Spatial = parsed.Spatial;
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid sound: {}", error.what());
			return false;
		}
	}
}
