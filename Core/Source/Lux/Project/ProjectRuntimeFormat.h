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
			// 20: project surface table asset handle follows the zone reverb mode
			// 19: zone reverb mode follows acoustic material overrides
			// 18: acoustic material overrides follow the bank manifest
			// 17: explicit AudioBankManifest follows the unchanged fixed ProjectInfo block
			// 16: GTAO slice/step sample counts
			// 15: removed all temporal settings (TAA, SMAA T2x, GTAO/SSR accumulation)
			uint32_t Version = 20;
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
