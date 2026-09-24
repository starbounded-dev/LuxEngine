// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Vulkan.h"

namespace Lux::Utils {

	struct VulkanCheckpointData
	{
		char Data[64 + 1] {};
	};

	void SetVulkanCheckpoint(VkCommandBuffer commandBuffer, const std::string& data);

}

