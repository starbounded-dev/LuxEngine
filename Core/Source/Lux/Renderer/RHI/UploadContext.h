// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/RHI/ResourceStateTracker.h"

#include <cstdint>

namespace nri {
	struct CommandBuffer;
}

namespace Lux {

	class NRIBuffer;
	class StagingStream;

	// What a Renderer::RecordResourceUpload callback records into: the shared upload batch, which
	// Renderer::FlushResourceUploads submits ahead of every command buffer submission (NRI migration
	// Phase 13). Writes stage their data at once, so callers may free it on return; each records an
	// NRI copy after the destination is required as CopyDest, and the batch returns every resource
	// to its resting state when it is flushed.
	//
	// Threading: valid only inside the callback, which runs on the calling thread under the batch
	// lock.
	class UploadContext
	{
	public:
		UploadContext(nri::CommandBuffer& commandBuffer, ResourceStateTracker& tracker, IBarrierEmitter& emitter, StagingStream& staging);

		// `size` bytes of `data` into `buffer` at `offset`.
		void WriteBuffer(const NRIBuffer& buffer, const void* data, uint64_t size, uint64_t offset = 0);
		// Mip `mip` of layer `layer` (a cube face is a layer) from rows of `rowPitch` bytes, rows of
		// 4x4 blocks for compressed formats (Utils::GetImageMemoryRowPitch).
		void WriteTexture(const TrackedTexture& texture, uint32_t layer, uint32_t mip, const void* data, uint64_t rowPitch);
		// A new texture's first transition, from undefined contents into its resting state, which the
		// tracker assumes every texture starts a command buffer in. NRITexture::Create records it.
		void InitializeTexture(const TrackedTexture& texture);

	private:
		nri::CommandBuffer& m_CommandBuffer;
		ResourceStateTracker& m_Tracker;
		IBarrierEmitter& m_Emitter;
		StagingStream& m_Staging;
	};

}
