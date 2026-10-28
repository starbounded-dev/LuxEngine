// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AcousticMaterial.h"

#include "Lux/Serialization/StreamReader.h"
#include "Lux/Serialization/StreamWriter.h"
#include "Lux/Serialization/Yaml.h"
#include <algorithm>
#include <cmath>

namespace Lux
{
	namespace
	{
		// LuxEngine's own default coefficients, derived from published acoustics data. Each row holds the
		// published inputs; Derive() turns them into engine properties.
		//
		// Absorption: random-incidence octave-band coefficients; LF averages 125 and 250 Hz, HF averages
		// 2 and 4 kHz. Unmarked rows come from https://sengpielaudio.com/calculator-RT60Coeff.htm.
		// "estimate" rows have no measured octave-band source and follow the trend for that surface.
		// Scattering: estimates that grow with surface roughness, from 0.05 (smooth) to 0.7 (foliage).
		// Transmission: the field-incidence mass law, TL = 20 log10(m'' f) - 47 dB with m'' = density *
		// thickness in kg/m^2, at 125 Hz (LF) and 2 kHz (HF) for a typical construction thickness. Solids
		// lose 30 dB per transmission distance, so distance = 30 * thickness / TL; a thin (one-sided)
		// surface of the material loses the energy fraction 1 - 10^(-TL / 10). Foliage scatters through
		// its volume instead, so ISO 9613-2's foliage attenuation per metre replaces the mass law.
		struct PublishedMaterial
		{
			float Absorption125, Absorption250, Absorption2000, Absorption4000;
			float Scattering;
			float Density;   // kg/m^3
			float Thickness; // m
			float AttenuationLF = 0.0f, AttenuationHF = 0.0f; // dB/m, used instead of the mass law when set
		};

		constexpr float kMassLawOffsetDb = 47.0f;
		constexpr float kLowBandHz = 125.0f;
		constexpr float kHighBandHz = 2000.0f;
		constexpr float kMinimumLossDb = 1.0f; // light porous layers fall below the mass law's range
		constexpr float kLossPerTransmissionDistanceDb = 30.0f;

		constexpr std::array<PublishedMaterial, AcousticMaterialCount> kPublished = { {
			{ 0.10f, 0.05f, 0.09f, 0.08f, 0.10f, 2300.0f, 0.30f },             // Default: as Concrete
			{ 0.03f, 0.03f, 0.05f, 0.07f, 0.10f, 1800.0f, 0.30f },             // Brick: unglazed
			{ 0.02f, 0.06f, 0.60f, 0.65f, 0.10f, 200.0f, 0.01f },              // Carpet: on concrete
			{ 0.14f, 0.35f, 0.70f, 0.65f, 0.10f, 610.0f, 0.001f },             // Cloth: 18 oz/sq yd molleton curtain
			{ 0.10f, 0.05f, 0.09f, 0.08f, 0.10f, 2300.0f, 0.30f },             // Concrete: block, painted
			{ 0.01f, 0.01f, 0.02f, 0.02f, 0.05f, 2300.0f, 0.30f },             // ConcretePolished: concrete or tile
			{ 0.15f, 0.25f, 0.60f, 0.60f, 0.40f, 1600.0f, 1.0f },              // Dirt: estimate, porous ground
			{ 0.35f, 0.25f, 0.07f, 0.04f, 0.05f, 2500.0f, 0.006f },            // Glass: window
			{ 0.15f, 0.30f, 0.80f, 0.85f, 0.50f, 1600.0f, 1.0f },              // Grass: estimate, grass over soil
			{ 0.25f, 0.60f, 0.70f, 0.70f, 0.40f, 1600.0f, 1.0f },              // Gravel: loose, moist (Egan; 1 kHz value carried up)
			{ 0.01f, 0.01f, 0.02f, 0.02f, 0.05f, 2700.0f, 0.30f },             // Marble: marble/tile
			{ 0.03f, 0.03f, 0.03f, 0.03f, 0.05f, 7850.0f, 0.003f },            // Metal: estimate, steel sheet
			{ 0.013f, 0.015f, 0.04f, 0.05f, 0.05f, 800.0f, 0.0125f },          // Plaster: smooth; transmission as 12.5 mm gypsum board
			{ 0.02f, 0.03f, 0.03f, 0.02f, 0.05f, 1300.0f, 0.005f },            // Plastic: vinyl tile on concrete
			{ 0.02f, 0.02f, 0.05f, 0.05f, 0.30f, 2600.0f, 1.0f },              // Rock: limestone wall (pyroomacoustics materials.json)
			{ 0.75f, 0.75f, 0.75f, 0.75f, 0.50f, 300.0f, 1.0f },               // Snow: broadband 0.75 (Engineering ToolBox)
			{ 0.15f, 0.25f, 0.60f, 0.60f, 0.40f, 1600.0f, 1.0f },              // Soil: estimate, porous ground
			{ 0.008f, 0.008f, 0.020f, 0.025f, 0.05f, 1000.0f, 1.0f },          // Water: water or ice surface
			{ 0.15f, 0.11f, 0.06f, 0.07f, 0.10f, 600.0f, 0.04f },              // Wood: on joists
			{ 0.28f, 0.22f, 0.10f, 0.11f, 0.10f, 600.0f, 0.0095f },            // WoodThin: 3/8 in plywood panel
			{ 0.01f, 0.01f, 0.02f, 0.02f, 0.05f, 2300.0f, 0.01f },             // Ceramic: marble/tile
			{ 0.04f, 0.04f, 0.10f, 0.10f, 0.10f, 1200.0f, 0.005f },            // Rubber: 5 mm on concrete (pyroomacoustics materials.json)
			{ 0.05f, 0.10f, 0.30f, 0.35f, 0.70f, 0.0f, 1.0f, 0.03f, 0.08f },   // Foliage: estimate absorption; ISO 9613-2 attenuation
		} };
		// A missing row would be zero-filled and silently derive nonsense; every row has a thickness.
		static_assert(kPublished[AcousticMaterialCount - 1].Thickness > 0.0f, "kPublished needs one row per AcousticMaterial");

		float TransmissionLossDb(const PublishedMaterial& source, float hz, float attenuation)
		{
			if (attenuation > 0.0f)
				return attenuation * source.Thickness;
			return std::max(20.0f * std::log10(source.Density * source.Thickness * hz) - kMassLawOffsetDb, kMinimumLossDb);
		}

		AcousticMaterialProperties Derive(const PublishedMaterial& source)
		{
			const float lossLF = TransmissionLossDb(source, kLowBandHz, source.AttenuationLF);
			const float lossHF = TransmissionLossDb(source, kHighBandHz, source.AttenuationHF);
			AcousticMaterialProperties properties;
			properties.AbsorptionLF = 0.5f * (source.Absorption125 + source.Absorption250);
			properties.AbsorptionHF = 0.5f * (source.Absorption2000 + source.Absorption4000);
			properties.Scattering = source.Scattering;
			properties.TransmissionLF = kLossPerTransmissionDistanceDb * source.Thickness / lossLF;
			properties.TransmissionHF = kLossPerTransmissionDistanceDb * source.Thickness / lossHF;
			properties.FlatTransmissionLF = 1.0f - std::pow(10.0f, -lossLF / 10.0f);
			properties.FlatTransmissionHF = 1.0f - std::pow(10.0f, -lossHF / 10.0f);
			return properties;
		}
	}

	bool IsValidAcousticMaterial(AcousticMaterial material)
	{
		return static_cast<size_t>(material) < AcousticMaterialCount;
	}

	const char* AcousticMaterialName(AcousticMaterial material)
	{
		return IsValidAcousticMaterial(material) ? AcousticMaterialNames[static_cast<size_t>(material)] : "Default";
	}

	bool ParseAcousticMaterial(std::string_view name, AcousticMaterial& material)
	{
		for (size_t i = 0; i < AcousticMaterialCount; ++i)
		{
			if (name == AcousticMaterialNames[i])
			{
				material = static_cast<AcousticMaterial>(i);
				return true;
			}
		}
		LUX_CORE_ERROR_TAG("Audio", "Unknown acoustic material '{0}'", name);
		return false;
	}

	bool AcousticMaterialProperties::IsValid() const
	{
		const auto fraction = [](float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; };
		return fraction(AbsorptionLF) && fraction(AbsorptionHF) && fraction(Scattering)
			&& std::isfinite(TransmissionLF) && TransmissionLF > 0.0f
			&& std::isfinite(TransmissionHF) && TransmissionHF > 0.0f
			&& std::isfinite(FlatTransmissionLF) && FlatTransmissionLF >= 0.0f
			&& std::isfinite(FlatTransmissionHF) && FlatTransmissionHF >= 0.0f;
	}

	bool AcousticMaterialSettings::Validate() const
	{
		for (size_t i = 0; i < Overrides.size(); ++i)
		{
			if (Overrides[i].Enabled && !Overrides[i].Properties.IsValid())
			{
				LUX_CORE_ERROR_TAG("Audio", "Invalid acoustic properties for {0}: absorption/scattering must be 0..1, transmission distance positive, and flat loss nonnegative; all values must be finite", AcousticMaterialNames[i]);
				return false;
			}
		}
		return true;
	}

	AcousticMaterialProperties GetDefaultAcousticMaterialProperties(AcousticMaterial material)
	{
		static const auto s_Defaults = []()
		{
			std::array<AcousticMaterialProperties, AcousticMaterialCount> result{};
			for (size_t i = 0; i < result.size(); ++i)
				result[i] = Derive(kPublished[i]);
			return result;
		}();
		return s_Defaults[IsValidAcousticMaterial(material) ? static_cast<size_t>(material) : 0];
	}

	void AcousticMaterialSettings::SerializeYAML(Yaml::Writer& out) const
	{
		out << Yaml::BeginMap;
		for (size_t i = 0; i < Overrides.size(); ++i)
		{
			if (!Overrides[i].Enabled)
				continue;
			out << Yaml::Key << AcousticMaterialNames[i] << Yaml::Value << Yaml::BeginMap;
			const auto& properties = Overrides[i].Properties;
			out << Yaml::Key << "AbsorptionLF" << Yaml::Value << properties.AbsorptionLF;
			out << Yaml::Key << "AbsorptionHF" << Yaml::Value << properties.AbsorptionHF;
			out << Yaml::Key << "Scattering" << Yaml::Value << properties.Scattering;
			out << Yaml::Key << "TransmissionLF" << Yaml::Value << properties.TransmissionLF;
			out << Yaml::Key << "TransmissionHF" << Yaml::Value << properties.TransmissionHF;
			out << Yaml::Key << "FlatTransmissionLF" << Yaml::Value << properties.FlatTransmissionLF;
			out << Yaml::Key << "FlatTransmissionHF" << Yaml::Value << properties.FlatTransmissionHF;
			out << Yaml::EndMap;
		}
		out << Yaml::EndMap;
	}

	bool AcousticMaterialSettings::DeserializeYAML(const Yaml::Node& node)
	{
		AcousticMaterialSettings result;
		try
		{
			if (node && !node.IsNull() && !node.IsMap())
				throw std::runtime_error("expected a material map");
			if (node && !node.IsNull())
			{
				for (const auto& entry : node)
				{
					AcousticMaterial material;
					if (!ParseAcousticMaterial(entry.first.as<std::string>(), material))
						return false;
					auto& value = result.Overrides[static_cast<size_t>(material)];
					if (value.Enabled || !entry.second.IsMap())
						throw std::runtime_error("duplicate material or invalid properties map");
					value.Enabled = true;
					value.Properties = GetDefaultAcousticMaterialProperties(material);
					auto& properties = value.Properties;
					properties.AbsorptionLF = entry.second["AbsorptionLF"].as<float>(properties.AbsorptionLF);
					properties.AbsorptionHF = entry.second["AbsorptionHF"].as<float>(properties.AbsorptionHF);
					properties.Scattering = entry.second["Scattering"].as<float>(properties.Scattering);
					properties.TransmissionLF = entry.second["TransmissionLF"].as<float>(properties.TransmissionLF);
					properties.TransmissionHF = entry.second["TransmissionHF"].as<float>(properties.TransmissionHF);
					properties.FlatTransmissionLF = entry.second["FlatTransmissionLF"].as<float>(properties.FlatTransmissionLF);
					properties.FlatTransmissionHF = entry.second["FlatTransmissionHF"].as<float>(properties.FlatTransmissionHF);
				}
			}
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot read acoustic materials: {0}", error.what());
			return false;
		}
		if (!result.Validate())
			return false;
		*this = result;
		return true;
	}

	bool AcousticMaterialSettings::Serialize(StreamWriter& stream) const
	{
		if (!Validate())
			return false;
		uint32_t count = 0;
		for (const auto& value : Overrides)
			count += value.Enabled ? 1 : 0;
		stream.WriteRaw<uint32_t>(count);
		for (size_t i = 0; i < Overrides.size(); ++i)
		{
			if (!Overrides[i].Enabled)
				continue;
			stream.WriteRaw<uint8_t>(static_cast<uint8_t>(i));
			const auto& properties = Overrides[i].Properties;
			stream.WriteRaw<float>(properties.AbsorptionLF);
			stream.WriteRaw<float>(properties.AbsorptionHF);
			stream.WriteRaw<float>(properties.Scattering);
			stream.WriteRaw<float>(properties.TransmissionLF);
			stream.WriteRaw<float>(properties.TransmissionHF);
			stream.WriteRaw<float>(properties.FlatTransmissionLF);
			stream.WriteRaw<float>(properties.FlatTransmissionHF);
		}
		if (!stream.IsStreamGood())
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot write runtime acoustic materials");
			return false;
		}
		return true;
	}

	bool AcousticMaterialSettings::Deserialize(StreamReader& stream)
	{
		AcousticMaterialSettings result;
		const auto read = [&stream](auto& value)
		{
			return stream.ReadData(reinterpret_cast<char*>(&value), sizeof(value)) && stream.IsStreamGood();
		};
		uint32_t count = 0;
		bool valid = read(count) && count <= AcousticMaterialCount;
		for (uint32_t i = 0; valid && i < count; ++i)
		{
			uint8_t id = 0;
			valid = read(id) && id < AcousticMaterialCount;
			if (!valid)
				break;
			auto& value = result.Overrides[id];
			valid = !value.Enabled;
			value.Enabled = true;
			auto& properties = value.Properties;
			valid = valid && read(properties.AbsorptionLF) && read(properties.AbsorptionHF) && read(properties.Scattering)
				&& read(properties.TransmissionLF) && read(properties.TransmissionHF)
				&& read(properties.FlatTransmissionLF) && read(properties.FlatTransmissionHF);
		}
		if (!valid)
		{
			LUX_CORE_ERROR_TAG("Audio", "Runtime acoustic materials are truncated or contain invalid/duplicate IDs");
			return false;
		}
		if (!result.Validate())
			return false;
		*this = result;
		return true;
	}
}
