#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <string_view>

struct VAWorld;
namespace YAML { class Node; class Emitter; }

namespace Lux
{
	class StreamReader;
	class StreamWriter;

	// Stable IDs shared by collider acoustics and future surface sound tables.
	enum class AcousticMaterial : uint8_t
	{
		Default = 0,
		Brick = 1,
		Carpet = 2,
		Cloth = 3,
		Concrete = 4,
		ConcretePolished = 5,
		Dirt = 6,
		Glass = 7,
		Grass = 8,
		Gravel = 9,
		Marble = 10,
		Metal = 11,
		Plaster = 12,
		Plastic = 13,
		Rock = 14,
		Snow = 15,
		Soil = 16,
		Water = 17,
		Wood = 18,
		WoodThin = 19,
		Ceramic = 20,
		Rubber = 21,
		Foliage = 22,
		Count
	};

	inline constexpr size_t AcousticMaterialCount = static_cast<size_t>(AcousticMaterial::Count);
	inline constexpr std::array<const char*, AcousticMaterialCount> AcousticMaterialNames = {
		"Default",
		"Brick",
		"Carpet",
		"Cloth",
		"Concrete",
		"ConcretePolished",
		"Dirt",
		"Glass",
		"Grass",
		"Gravel",
		"Marble",
		"Metal",
		"Plaster",
		"Plastic",
		"Rock",
		"Snow",
		"Soil",
		"Water",
		"Wood",
		"WoodThin",
		"Ceramic",
		"Rubber",
		"Foliage",
	};

	bool IsValidAcousticMaterial(AcousticMaterial material);
	const char* AcousticMaterialName(AcousticMaterial material);
	bool ParseAcousticMaterial(std::string_view name, AcousticMaterial& material);

	struct AcousticMaterialProperties
	{
		float AbsorptionLF = 0.0f;
		float AbsorptionHF = 0.0f;
		float Scattering = 0.0f;
		float TransmissionLF = 1.0f; // metres through closed geometry until energy is lost
		float TransmissionHF = 1.0f;
		float FlatTransmissionLF = 0.0f; // energy loss on open/thin geometry
		float FlatTransmissionHF = 0.0f;
		bool IsValid() const;
	};

	struct AcousticMaterialOverride
	{
		bool Enabled = false;
		AcousticMaterialProperties Properties;
	};

	struct AcousticMaterialSettings
	{
		std::array<AcousticMaterialOverride, AcousticMaterialCount> Overrides{};
		bool Validate() const;
		void SerializeYAML(YAML::Emitter& out) const;
		bool DeserializeYAML(const YAML::Node& node);
		bool Serialize(StreamWriter& stream) const;
		bool Deserialize(StreamReader& stream);
	};

	// Cached SDK presets. Engine names absent from VA use the documented nearest preset.
	AcousticMaterialProperties GetDefaultAcousticMaterialProperties(AcousticMaterial material);
	const char* AcousticMaterialPresetName(AcousticMaterial material);
	// Called only on an idle world. Unique custom IDs prevent overrides leaking to other tags.
	int AcousticMaterialVAID(AcousticMaterial material);
	bool ConfigureAcousticMaterials(VAWorld* world, const AcousticMaterialSettings& settings);
}
