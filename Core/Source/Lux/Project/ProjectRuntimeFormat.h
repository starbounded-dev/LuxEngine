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
			// 16: GTAO slice/step sample counts
			// 15: removed all temporal settings (TAA, SMAA T2x, GTAO/SSR accumulation)
			uint32_t Version = 16;
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
