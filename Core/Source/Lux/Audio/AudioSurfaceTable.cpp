// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioSurfaceTable.h"
#include "AudioEventRefSerializer.h"

#include "Lux/Serialization/Yaml.h"
#include <cmath>

namespace Lux
{
	bool AudioSurfaceTable::Validate() const
	{
		return std::isfinite(ImpactCooldown) && ImpactCooldown >= 0.0f && ImpactCooldown <= 60.0f
			&& std::isfinite(MinimumImpulse) && MinimumImpulse >= 0.0f
			&& std::isfinite(MinimumMotionSpeed) && MinimumMotionSpeed >= 0.0f;
	}

	std::string AudioSurfaceTable::ToYAML() const
	{
		Yaml::Writer out;
		out << Yaml::BeginMap << Yaml::Key << "AudioSurfaceTable" << Yaml::BeginMap;
		out << Yaml::Key << "Version" << Yaml::Value << 1;
		out << Yaml::Key << "ImpactCooldown" << Yaml::Value << ImpactCooldown;
		out << Yaml::Key << "MinimumImpulse" << Yaml::Value << MinimumImpulse;
		out << Yaml::Key << "MinimumMotionSpeed" << Yaml::Value << MinimumMotionSpeed;
		out << Yaml::Key << "Surfaces" << Yaml::BeginMap;
		for (size_t i = 0; i < Surfaces.size(); ++i)
		{
			out << Yaml::Key << AcousticMaterialNames[i] << Yaml::BeginMap;
			AudioEventRefSerializer::Write(out, "Footstep", Surfaces[i].Footstep);
			AudioEventRefSerializer::Write(out, "Impact", Surfaces[i].Impact);
			AudioEventRefSerializer::Write(out, "Scrape", Surfaces[i].Scrape);
			AudioEventRefSerializer::Write(out, "Roll", Surfaces[i].Roll);
			out << Yaml::EndMap;
		}
		out << Yaml::EndMap << Yaml::EndMap << Yaml::EndMap;
		return out.c_str();
	}

	bool AudioSurfaceTable::FromYAML(const std::string& text)
	{
		try
		{
			const auto node = Yaml::Load(text)["AudioSurfaceTable"];
			if (!node || node["Version"].as<int>(1) != 1)
				throw std::runtime_error("Missing surface table or unsupported version");
			AudioSurfaceTable parsed;
			parsed.ImpactCooldown = node["ImpactCooldown"].as<float>(0.15f);
			parsed.MinimumImpulse = node["MinimumImpulse"].as<float>(0.5f);
			parsed.MinimumMotionSpeed = node["MinimumMotionSpeed"].as<float>(0.1f);
			if (!parsed.Validate())
				throw std::runtime_error("Invalid surface sound thresholds");
			if (auto surfaces = node["Surfaces"])
			{
				if (!surfaces.IsMap())
					throw std::runtime_error("Surfaces must be a map");
				for (const auto& entry : surfaces)
				{
					AcousticMaterial material;
					if (!ParseAcousticMaterial(entry.first.as<std::string>(), material))
						throw std::runtime_error("Unknown surface material");
					const auto event = [&](const char* name) { return AudioEventRefSerializer::Read(entry.second[name]); };
					parsed.Surfaces[static_cast<size_t>(material)] = { event("Footstep"), event("Impact"), event("Scrape"), event("Roll") };
				}
			}
			Surfaces = std::move(parsed.Surfaces);
			ImpactCooldown = parsed.ImpactCooldown;
			MinimumImpulse = parsed.MinimumImpulse;
			MinimumMotionSpeed = parsed.MinimumMotionSpeed;
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid audio surface table: {}", error.what());
			return false;
		}
	}
}
