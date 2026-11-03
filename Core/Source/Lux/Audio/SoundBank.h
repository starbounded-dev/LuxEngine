// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"

#include <string>
#include <vector>

namespace Lux
{
	// A list of Sounds that load and unload together (.lbank): the engine's replacement for FMOD's
	// banks. Loading a bank preloads its Sounds' clips; a Sound may sit in several banks and stays
	// loaded while any of them is. Lives inside the asset pack, not in a file of its own.
	class SoundBank : public Asset
	{
	public:
		static AssetType GetStaticType() { return AssetType::SoundBank; }
		AssetType GetAssetType() const override { return GetStaticType(); }

		static constexpr size_t MaxSounds = 2048;
		static constexpr size_t MaxFileBytes = 64 * 1024;   // the serialized YAML, at save, load and in packs

		std::vector<AssetHandle> Sounds;   // no duplicates, no zero handles
		bool Resident = false;             // loaded at game start and never unloaded

		// Both return whether the list changed: a zero handle, a duplicate or a full bank is refused.
		bool Add(AssetHandle sound);
		bool Remove(AssetHandle sound);
		bool Contains(AssetHandle sound) const;

		// Structural checks only; whether each handle names a Sound is the asset system's to say.
		bool Validate(std::string* error = nullptr) const;
		std::string ToYAML() const;
		// Replaces this bank's settings, or leaves them unchanged and reports why it failed.
		bool FromYAML(const std::string& text);
	};
}
