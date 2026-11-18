// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"

#include <vector>

namespace Lux
{
	struct SkeletalExtractionResult
	{
		AssetHandle Skeleton = 0;
		std::vector<AssetHandle> Clips;
	};

	// Editor only. Reads the rigged model behind a MeshSource and bakes its skeleton and clips into
	// "<model>.lskel" and "<model>_<clip>.lanim" next to it, then registers them as assets. An exported
	// game reads these baked files, so it needs no Assimp. Running it again overwrites the files and
	// keeps their handles, so references to them stay valid. Returns false (and logs why) when the
	// handle is not a MeshSource, the file cannot be read, or the model has no skeleton.
	bool ExtractSkeletalAssets(AssetHandle meshSource, SkeletalExtractionResult& result);
}
