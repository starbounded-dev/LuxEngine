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
			// 22: bounded accessibility configuration follows dialogue settings
			// 21: dialogue table handle and bounded language follow the surface table
			// 20: project surface table asset handle follows the zone reverb mode
			// 19: zone reverb mode follows acoustic material overrides
			// 18: acoustic material overrides follow the bank manifest
			// 17: explicit AudioBankManifest follows the unchanged fixed ProjectInfo block
			// 16: GTAO slice/step sample counts
			// 15: removed all temporal settings (TAA, SMAA T2x, GTAO/SSR accumulation)
			uint32_t Version = 22;
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
