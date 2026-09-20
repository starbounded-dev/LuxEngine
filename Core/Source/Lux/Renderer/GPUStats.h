// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <stdint.h>

namespace Lux {

	struct GPUMemoryStats
	{
		uint64_t Used = 0;
		uint64_t TotalAvailable = 0;
		uint64_t AllocationCount = 0;

		uint64_t BufferAllocationSize = 0;
		uint64_t BufferAllocationCount = 0;

		uint64_t ImageAllocationSize = 0;
		uint64_t ImageAllocationCount = 0;
	};

}
