#pragma once

#include "AcousticMaterial.h"
#include "AudioEventRef.h"
#include "Lux/Asset/Asset.h"

#include <array>

namespace Lux
{
	struct AudioSurfaceSounds
	{
		AudioEventRef Footstep, Impact, Scrape, Roll;
	};

	class AudioSurfaceTable : public Asset
	{
	public:
		static AssetType GetStaticType() { return AssetType::AudioSurfaceTable; }
		AssetType GetAssetType() const override { return GetStaticType(); }
		std::array<AudioSurfaceSounds, AcousticMaterialCount> Surfaces;
		float ImpactCooldown = 0.15f;
		float MinimumImpulse = 0.5f;
		float MinimumMotionSpeed = 0.1f;

		std::string ToYAML() const;
		bool FromYAML(const std::string& text);
		bool Validate() const;
	};
}
