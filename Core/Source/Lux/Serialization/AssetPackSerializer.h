// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "AssetPackFile.h"

#include "Lux/Core/Buffer.h"
#include "Obfuscation.h"

#include <filesystem>

namespace Lux {

	class AssetPackSerializer
	{
	public:
		// With a key the pack is a scrambled container (Obfuscation.h).
		static bool Serialize(const std::filesystem::path& path, AssetPackFile& file, Buffer appBinary, std::atomic<float>& progress, const Obfuscation::Key* key = nullptr);
		static bool DeserializeIndex(const std::filesystem::path& path, AssetPackFile& file);
	private:
		static uint64_t CalculateIndexTableSize(const AssetPackFile& file);
	};

}

