// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Buffer.h"
#include "Lux/Renderer/RHI/ResourceStateTracker.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace Lux {

	class RenderCommandBuffer;

	struct TextureSubresource
	{
		uint32_t Mip = 0;
		uint32_t Layer = 0;
	};

	// Reads subresources of `texture` back to host memory (NRI migration Phase 13): records copies into
	// a HOST_READBACK buffer on `commandBuffer` (begun and submitted here), waits for the GPU, and
	// writes each subresource's rows tightly packed (Utils::GetImageMemoryRowPitch, rows of blocks
	// for compressed formats) into `buffer`, one after another in the order given. Blocks the calling
	// thread, which must be one that may record the command buffer (the render thread, or the main
	// thread between frames). `buffer` is left empty, and the failure logged, when anything fails.
	void ReadbackTexture(RenderCommandBuffer& commandBuffer, const TrackedTexture& texture, std::span<const TextureSubresource> subresources, Buffer& buffer, std::string_view debugName);

}
