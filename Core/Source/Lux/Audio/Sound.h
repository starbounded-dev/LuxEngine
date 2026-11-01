// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"

#include <cstdint>
#include <string>

namespace Lux
{
	// How a 3D Sound gets quieter with distance d, clamped to [MinDistance, MaxDistance]:
	//   Inverse: MinDistance / (MinDistance + Rolloff * (d - MinDistance)); holds its last value past
	//            MaxDistance.
	//   Linear:  1 - Rolloff * (d - MinDistance) / (MaxDistance - MinDistance), floored at 0.
	//   None:    1.
	// The same formulas as miniaudio's attenuation models of those names.
	enum class SoundAttenuation : uint8_t { None, Inverse, Linear };

	struct SoundSpatialSettings
	{
		bool Enabled = true;   // 3D: panned and attenuated from the listener. False: 2D.
		SoundAttenuation Attenuation = SoundAttenuation::Inverse;
		float MinDistance = 1.0f;    // metres; full volume up to here
		float MaxDistance = 50.0f;   // metres; attenuation stops here, and distance culling uses it
		float Rolloff = 1.0f;
	};

	// A sound authored in the editor and played by the native audio backend (.lsound). This is the
	// minimal form from the native backend plan's Phase 2: one clip with fixed settings. Containers,
	// randomization and parameters come later. Instances read it when they are created, so an edit
	// applies to the next instance.
	class Sound : public Asset
	{
	public:
		static AssetType GetStaticType() { return AssetType::Sound; }
		AssetType GetAssetType() const override { return GetStaticType(); }

		AssetHandle Clip = 0;        // an Audio asset (.wav, .ogg)
		std::string Bus = "bus:/";   // where it mixes; every bus plays through the master until the mixer exists
		float Volume = 1.0f;         // linear gain, 0 to 4
		float Pitch = 1.0f;          // playback rate, above 0 and up to 4
		bool Loop = false;
		SoundSpatialSettings Spatial;

		// Writes the first problem to error, when given.
		bool Validate(std::string* error = nullptr) const;
		std::string ToYAML() const;
		// Replaces this sound's settings, or leaves them unchanged and reports why it failed.
		bool FromYAML(const std::string& text);
	};

	const char* SoundAttenuationName(SoundAttenuation attenuation);
	bool ParseSoundAttenuation(const std::string& name, SoundAttenuation& outAttenuation);
}
