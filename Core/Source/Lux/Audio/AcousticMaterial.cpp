// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AcousticMaterial.h"

#include "Lux/Serialization/StreamReader.h"
#include "Lux/Serialization/StreamWriter.h"
#include <soundwave/AcousticMaterialSettings.h>
#include "Lux/Serialization/Yaml.h"
#include <cmath>

namespace Lux
{
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
		static_assert(AcousticMaterialCount == Soundwave::kMaterialTagCount);
		const auto& p = Soundwave::GetDefaultMaterialProperties(static_cast<Soundwave::MaterialTag>(IsValidAcousticMaterial(material) ? material : AcousticMaterial::Default));
		return { p.AbsorptionLF, p.AbsorptionHF, p.Scattering, p.TransmissionLF, p.TransmissionHF, p.FlatTransmissionLF, p.FlatTransmissionHF };
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
