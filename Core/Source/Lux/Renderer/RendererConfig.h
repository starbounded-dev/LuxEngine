// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <string>

namespace Lux {

	struct RendererConfig
	{
		uint32_t FramesInFlight = 3;

		bool ComputeEnvironmentMaps = true;

		// Tiering settings
		uint32_t EnvironmentMapResolution = 1024;
		uint32_t IrradianceMapComputeSamples = 512;

		std::string ShaderPackPath;
	};

}
