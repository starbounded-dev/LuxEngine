// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "RHITypes.h"

namespace Lux {

	namespace {

		struct ShaderStageName
		{
			ShaderStage Stage;
			std::string_view Name;
		};

		// Spellings copied from nvrhi::utils::ShaderStageToString / ShaderStageFromString: the
		// shader cache (VulkanShaderCache) writes these to disk, so they must match what it holds.
		constexpr ShaderStageName k_ShaderStageNames[] = {
			{ ShaderStage::None,          "None" },
			{ ShaderStage::Compute,       "Compute" },
			{ ShaderStage::Vertex,        "Vertex" },
			{ ShaderStage::Hull,          "Hull" },
			{ ShaderStage::Domain,        "Domain" },
			{ ShaderStage::Geometry,      "Geometry" },
			{ ShaderStage::Pixel,         "Pixel" },
			{ ShaderStage::Amplification, "Amplification" },
			{ ShaderStage::Mesh,          "Mesh" },
			{ ShaderStage::AllGraphics,   "AllGraphics" },
			{ ShaderStage::RayGeneration, "RayGeneration" },
			{ ShaderStage::AnyHit,        "AnyHit" },
			{ ShaderStage::ClosestHit,    "ClosestHit" },
			{ ShaderStage::Miss,          "Miss" },
			{ ShaderStage::Intersection,  "Intersection" },
			{ ShaderStage::Callable,      "Callable" },
			{ ShaderStage::AllRayTracing, "AllRayTracing" },
			{ ShaderStage::All,           "All" },
		};

	}

	const char* ShaderStageToString(ShaderStage stage)
	{
		for (const ShaderStageName& entry : k_ShaderStageNames)
		{
			if (entry.Stage == stage)
				return entry.Name.data();
		}

		return "<INVALID>";
	}

	ShaderStage ShaderStageFromString(std::string_view string)
	{
		for (const ShaderStageName& entry : k_ShaderStageNames)
		{
			if (entry.Name == string)
				return entry.Stage;
		}

		return ShaderStage::None;
	}

}
