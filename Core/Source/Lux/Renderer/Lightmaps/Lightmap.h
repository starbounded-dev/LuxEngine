// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Ref.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Lux {

	// A baked lightmap atlas for one scene: the mean incident radiance (irradiance / pi) per texel as
	// RGBA16F, and where each lit submesh instance sits in it. The renderer adds
	// albedo * (1 - metalness) * radiance as indirect diffuse (GBuffer_Static.glsl).
	//
	// Stored as a sidecar file `<project assets>/Lightmaps/<scene name>.lxlm` found by convention, so
	// there is no asset type yet: it is not part of runtime exports (docs/GI_PLAN.md, GI3 limits).
	class LightmapData : public RefCounted
	{
	public:
		struct Entry
		{
			uint64_t EntityID = 0;
			uint32_t Submesh = 0;
			glm::vec4 Rect{ 0.0f }; // scale.xy, offset.xy: atlasUV = offset + scale * lightmapUV
		};

		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<uint16_t> Texels; // RGBA16F, Width * Height * 4
		std::vector<Entry> Entries;

		// The atlas as the renderer reads it: one uvec2 per texel, (r | g << 16, b | a << 16).
		std::vector<uint32_t> PackedForGPU() const;

		bool Save(const std::filesystem::path& path) const;
		static Ref<LightmapData> Load(const std::filesystem::path& path);

		// <project assets>/Lightmaps/<sceneName>.lxlm; empty without an active project.
		static std::filesystem::path PathForScene(const std::string& sceneName);
	};

}
