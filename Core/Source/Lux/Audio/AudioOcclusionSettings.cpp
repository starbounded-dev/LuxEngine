// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioOcclusionSettings.h"
#include "Lux/Serialization/StreamReader.h"
#include "Lux/Serialization/StreamWriter.h"
#include "Lux/Serialization/Yaml.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <soundwave/Settings.h>
#include <set>
namespace Lux
{
	namespace
	{
		constexpr uint32_t kMaximumSettingsBytes = 4096;
		constexpr float kMinimumRateHz = 1.0f;
		constexpr float kMaximumRateHz = 120.0f;
		constexpr uint32_t kMaximumCastBudget = 1024;
		constexpr float kMaximumStrength = 4.0f;
		constexpr uint32_t kKernelSettingsVersion = 1;
		constexpr std::array<std::string_view, 14> kKernelKeys{ "Version", "ReflectionDepth", "OpeningGridSize", "MaxBounces",
			"CandidateBudget", "PathBudget", "OpeningNodeBudget", "Absorption", "CompareRaycast", "ReverbRays", "ReverbBounces",
			"ReverbReferenceSeconds", "AirAbsorptionLF", "AirAbsorptionHF" };

		void ValidateKernelKeys(const Yaml::Node& node)
		{
			if (!node.IsMap())
				throw std::runtime_error("Kernel must be a settings map");
			std::set<std::string> seen;
			for (const auto& entry : node)
			{
				const auto name = entry.first.as<std::string>();
				if (std::find(kKernelKeys.begin(), kKernelKeys.end(), name) == kKernelKeys.end())
					throw std::runtime_error("unknown Kernel key '" + name + "'");
				if (!seen.insert(name).second)
					throw std::runtime_error("duplicate Kernel key '" + name + "'");
			}
		}
	}
	Soundwave::Settings AudioOcclusionSettings::GetKernelSettings() const
	{
		Soundwave::Settings settings;
		settings.ReflectionDepth = ReflectionDepth;
		settings.OpeningGridSize = OpeningGridSize;
		settings.MaxBounces = MaxBounces;
		settings.CandidateBudget = CandidateBudget;
		settings.PathBudget = PathBudget;
		settings.OpeningNodeBudget = OpeningNodeBudget;
		settings.Strength = Strength;
		settings.Absorption = Absorption;
		settings.ReverbRays = ReverbRays;
		settings.ReverbBounces = ReverbBounces;
		settings.ReverbReferenceSeconds = ReverbReferenceSeconds;
		settings.AirAbsorptionLF = AirAbsorptionLF;
		settings.AirAbsorptionHF = AirAbsorptionHF;
		return settings;
	}
	bool AudioOcclusionSettings::Validate() const
	{
		return std::isfinite(UpdateRateHz) && UpdateRateHz >= kMinimumRateHz &&
			UpdateRateHz <= kMaximumRateHz && CastBudget > 0 && CastBudget <= kMaximumCastBudget && std::isfinite(Strength) &&
			Strength >= 0.0f && Strength <= kMaximumStrength && GetKernelSettings().Validate();
	}
	void AudioOcclusionSettings::SerializeYAML(Yaml::Writer& out) const
	{
		out << Yaml::BeginMap;
		out << Yaml::Key << "UpdateRateHz" << Yaml::Value << UpdateRateHz;
		out << Yaml::Key << "CastBudget" << Yaml::Value << CastBudget;
		out << Yaml::Key << "Strength" << Yaml::Value << Strength;
		out << Yaml::Key << "Kernel" << Yaml::Value << Yaml::BeginMap;
		out << Yaml::Key << "Version" << Yaml::Value << kKernelSettingsVersion;
		out << Yaml::Key << "ReflectionDepth" << Yaml::Value << ReflectionDepth;
		out << Yaml::Key << "OpeningGridSize" << Yaml::Value << OpeningGridSize;
		out << Yaml::Key << "MaxBounces" << Yaml::Value << MaxBounces;
		out << Yaml::Key << "CandidateBudget" << Yaml::Value << CandidateBudget;
		out << Yaml::Key << "PathBudget" << Yaml::Value << PathBudget;
		out << Yaml::Key << "OpeningNodeBudget" << Yaml::Value << OpeningNodeBudget;
		out << Yaml::Key << "Absorption" << Yaml::Value << Absorption;
		out << Yaml::Key << "CompareRaycast" << Yaml::Value << CompareRaycast;
		out << Yaml::Key << "ReverbRays" << Yaml::Value << ReverbRays;
		out << Yaml::Key << "ReverbBounces" << Yaml::Value << ReverbBounces;
		out << Yaml::Key << "ReverbReferenceSeconds" << Yaml::Value << ReverbReferenceSeconds;
		out << Yaml::Key << "AirAbsorptionLF" << Yaml::Value << AirAbsorptionLF;
		out << Yaml::Key << "AirAbsorptionHF" << Yaml::Value << AirAbsorptionHF;
		out << Yaml::EndMap;
		out << Yaml::EndMap;
	}
	bool AudioOcclusionSettings::DeserializeYAML(const Yaml::Node& node)
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
					// Pre-Soundwave files chose a backend here. Soundwave always drives playback now.
					const std::string source = node["Source"].as<std::string>();
					if (source != "Engine" && source != "Raytraced")
						throw std::runtime_error("unknown source '" + source + "'");
				}
				if (node["UpdateRateHz"])
					parsed.UpdateRateHz = node["UpdateRateHz"].as<float>();
				if (node["CastBudget"])
					parsed.CastBudget = node["CastBudget"].as<uint32_t>();
				if (node["Strength"])
					parsed.Strength = node["Strength"].as<float>();
				if (const auto kernel = node["Kernel"])
				{
					ValidateKernelKeys(kernel);
					if (kernel["Version"] && kernel["Version"].as<uint32_t>() != kKernelSettingsVersion)
						throw std::runtime_error("unsupported Kernel settings version");
					if (kernel["ReflectionDepth"])
						parsed.ReflectionDepth = kernel["ReflectionDepth"].as<uint32_t>();
					if (kernel["OpeningGridSize"])
						parsed.OpeningGridSize = kernel["OpeningGridSize"].as<uint32_t>();
					if (kernel["MaxBounces"])
						parsed.MaxBounces = kernel["MaxBounces"].as<uint32_t>();
					if (kernel["CandidateBudget"])
						parsed.CandidateBudget = kernel["CandidateBudget"].as<uint32_t>();
					if (kernel["PathBudget"])
						parsed.PathBudget = kernel["PathBudget"].as<uint32_t>();
					if (kernel["OpeningNodeBudget"])
						parsed.OpeningNodeBudget = kernel["OpeningNodeBudget"].as<uint32_t>();
					if (kernel["Absorption"])
						parsed.Absorption = kernel["Absorption"].as<float>();
					if (kernel["CompareRaycast"])
						parsed.CompareRaycast = kernel["CompareRaycast"].as<bool>();
					if (kernel["ReverbRays"])
						parsed.ReverbRays = kernel["ReverbRays"].as<uint32_t>();
					if (kernel["ReverbBounces"])
						parsed.ReverbBounces = kernel["ReverbBounces"].as<uint32_t>();
					if (kernel["ReverbReferenceSeconds"])
						parsed.ReverbReferenceSeconds = kernel["ReverbReferenceSeconds"].as<float>();
					if (kernel["AirAbsorptionLF"])
						parsed.AirAbsorptionLF = kernel["AirAbsorptionLF"].as<float>();
					if (kernel["AirAbsorptionHF"])
						parsed.AirAbsorptionHF = kernel["AirAbsorptionHF"].as<float>();
				}
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
		Yaml::Writer out;
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
			return DeserializeYAML(Yaml::Load(text));
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid runtime occlusion settings: {}", error.what());
			return false;
		}
	}
}
