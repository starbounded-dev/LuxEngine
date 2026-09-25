// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"

#include <cstdint>

namespace Lux
{
	struct ProjectInfo
	{
		struct FileHeader
		{
			char Header[4] = { 'L', 'P', 'R', 'J' };
			// 25: bounded occlusion settings follow performance budgets
			// 24: fixed render width/height close the scene-renderer block
			// 23: bounded performance budgets follow accessibility settings
			// 22: bounded accessibility configuration follows dialogue settings
			// 21: dialogue table handle and bounded language follow the surface table
			// 20: project surface table asset handle follows the zone reverb mode
			// 19: zone reverb mode follows acoustic material overrides
			// 18: acoustic material overrides follow the bank manifest
			// 17: explicit AudioBankManifest follows the unchanged fixed ProjectInfo block
			// 16: GTAO slice/step sample counts
			// 15: removed all temporal settings (TAA, SMAA T2x, GTAO/SSR accumulation)
			uint32_t Version = 25;
		};

		struct Audio
		{
			double FileStreamingDurationThreshold = 1.0;
		};

		FileHeader HeaderData;
		AssetHandle StartScene = 0;
		Audio AudioInfo;
	};
}
