// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

namespace Lux {

	namespace RendererUtils {

		struct ResourceAllocationCounts
		{
			uint32_t Samplers = 0;
		};

		ResourceAllocationCounts& GetResourceAllocationCounts();
	}
}
