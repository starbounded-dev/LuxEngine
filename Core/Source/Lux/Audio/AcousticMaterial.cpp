#include "lpch.h"
#include "AcousticMaterial.h"

#include "Lux/Serialization/StreamReader.h"
#include "Lux/Serialization/StreamWriter.h"
#include "vaudio.h"
#include <yaml-cpp/yaml.h>
#include <cmath>

namespace Lux
{
	namespace
	{
		constexpr int kCustomMaterialBase = 1000;
		constexpr std::array<VAMaterialType, AcousticMaterialCount> kPresets = {
			VAMaterialConcrete,
			VAMaterialBrick,
			VAMaterialCloth,
			VAMaterialCloth,
			VAMaterialConcrete,
			VAMaterialConcretePolished,
			VAMaterialDirt,
			VAMaterialGlass,
			VAMaterialGrass,
			VAMaterialGravel,
			VAMaterialMarble,
			VAMaterialMetal,
			VAMaterialGyprock,
			VAMaterialWoodIndoor,
			VAMaterialRock,
			VAMaterialSnow,
			VAMaterialMud,
			VAMaterialWater,
			VAMaterialWoodOutdoor,
			VAMaterialWoodIndoor,
			VAMaterialTile,
			VAMaterialCloth,
			VAMaterialLeaf,
		};
		constexpr std::array<const char*, AcousticMaterialCount> kPresetNames = {
			"Concrete",
			"Brick",
			"Cloth",
			"Cloth",
			"Concrete",
			"ConcretePolished",
			"Dirt",
			"Glass",
			"Grass",
			"Gravel",
			"Marble",
			"Metal",
			"Gyprock",
			"WoodIndoor",
			"Rock",
			"Snow",
			"Mud",
			"Water",
			"WoodOutdoor",
			"WoodIndoor",
			"Tile",
			"Cloth",
			"Leaf",
		};

		AcousticMaterialProperties ReadProperties(VAWorld* world, int id)
		{
			AcousticMaterialProperties properties;
			properties.AbsorptionLF = vaWorldGetMaterialAbsorptionLF(world, id);
			properties.AbsorptionHF = vaWorldGetMaterialAbsorptionHF(world, id);
			properties.Scattering = vaWorldGetMaterialScattering(world, id);
			properties.TransmissionLF = vaWorldGetMaterialTransmissionLF(world, id);
			properties.TransmissionHF = vaWorldGetMaterialTransmissionHF(world, id);
			properties.FlatTransmissionLF = vaWorldGetMaterialFlatTransmissionLF(world, id);
			properties.FlatTransmissionHF = vaWorldGetMaterialFlatTransmissionHF(world, id);
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
			VAWorld* world = vaWorldCreate();
			if (!world)
			{
				LUX_CORE_ERROR_TAG("Audio", "Cannot read VA material presets: world creation failed");
				return result;
			}
			for (size_t i = 0; i < result.size(); ++i)
				result[i] = ReadProperties(world, kPresets[i]);
			vaWorldDestroy(world);
			return result;
		}();
		return s_Defaults[IsValidAcousticMaterial(material) ? static_cast<size_t>(material) : 0];
	}

	const char* AcousticMaterialPresetName(AcousticMaterial material)
	{
		return kPresetNames[IsValidAcousticMaterial(material) ? static_cast<size_t>(material) : 0];
	}

	int AcousticMaterialVAID(AcousticMaterial material)
	{
		return kCustomMaterialBase + (IsValidAcousticMaterial(material) ? static_cast<int>(material) : 0);
	}

	bool ConfigureAcousticMaterials(VAWorld* world, const AcousticMaterialSettings& settings)
	{
		if (!settings.Validate())
			return false;
		for (size_t i = 0; i < AcousticMaterialCount; ++i)
		{
			const int id = AcousticMaterialVAID(static_cast<AcousticMaterial>(i));
			const auto check = [i](VAResult result)
			{
				if (result == VA_SUCCESS || result == VA_UNCHANGED)
					return true;
				LUX_CORE_ERROR_TAG("Audio", "Failed to configure VA material {0} (VAResult={1})", AcousticMaterialNames[i], result);
				return false;
			};
			if (!vaWorldHasMaterial(world, id) && !check(vaWorldCreateMaterial(world, id)))
				return false;
			const auto properties = settings.Overrides[i].Enabled ? settings.Overrides[i].Properties : ReadProperties(world, kPresets[i]);
			if (!check(vaWorldSetMaterialAbsorptionLF(world, id, properties.AbsorptionLF)))
				return false;
			if (!check(vaWorldSetMaterialAbsorptionHF(world, id, properties.AbsorptionHF)))
				return false;
			if (!check(vaWorldSetMaterialScattering(world, id, properties.Scattering)))
				return false;
			if (!check(vaWorldSetMaterialTransmissionLF(world, id, properties.TransmissionLF)))
				return false;
			if (!check(vaWorldSetMaterialTransmissionHF(world, id, properties.TransmissionHF)))
				return false;
			if (!check(vaWorldSetMaterialFlatTransmissionLF(world, id, properties.FlatTransmissionLF)))
				return false;
			if (!check(vaWorldSetMaterialFlatTransmissionHF(world, id, properties.FlatTransmissionHF)))
				return false;
		}
		return true;
	}

	void AcousticMaterialSettings::SerializeYAML(YAML::Emitter& out) const
	{
		out << YAML::BeginMap;
		for (size_t i = 0; i < Overrides.size(); ++i)
		{
			if (!Overrides[i].Enabled)
				continue;
			out << YAML::Key << AcousticMaterialNames[i] << YAML::Value << YAML::BeginMap;
			const auto& properties = Overrides[i].Properties;
			out << YAML::Key << "AbsorptionLF" << YAML::Value << properties.AbsorptionLF;
			out << YAML::Key << "AbsorptionHF" << YAML::Value << properties.AbsorptionHF;
			out << YAML::Key << "Scattering" << YAML::Value << properties.Scattering;
			out << YAML::Key << "TransmissionLF" << YAML::Value << properties.TransmissionLF;
			out << YAML::Key << "TransmissionHF" << YAML::Value << properties.TransmissionHF;
			out << YAML::Key << "FlatTransmissionLF" << YAML::Value << properties.FlatTransmissionLF;
			out << YAML::Key << "FlatTransmissionHF" << YAML::Value << properties.FlatTransmissionHF;
			out << YAML::EndMap;
		}
		out << YAML::EndMap;
	}

	bool AcousticMaterialSettings::DeserializeYAML(const YAML::Node& node)
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
