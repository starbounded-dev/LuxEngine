// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/RHI/ResourceStateTracker.h"

#include <vector>

namespace nri {
	struct BufferBarrierDesc;
	struct CommandBuffer;
	struct TextureBarrierDesc;
}

namespace Lux {

	// Records the tracker's transitions as NRI barriers (NRI migration Phase 13), batched until
	// Commit. Each state's access, layout and stages are NVRHI's Vulkan ones (vulkan-constants.cpp
	// g_ResourceStateMap), so the barriers stay the ones NVRHI placed; UnorderedAccess also covers
	// storage clears. On the copy queue the barriers keep only what that queue can execute (see
	// SetQueue).
	//
	// Threading: one thread at a time, owned with its tracker (ResourceStateTracker).
	class NRIBarrierEmitter final : public IBarrierEmitter
	{
	public:
		NRIBarrierEmitter();
		~NRIBarrierEmitter() override;

		// The command buffer Commit records into, set for each open command buffer. Barriers
		// committed without one are dropped (logged once).
		void SetCommandBuffer(nri::CommandBuffer* commandBuffer) { m_CommandBuffer = commandBuffer; }
		// The queue the command buffer runs on. A copy queue has no shader, attachment or
		// vertex-input stages, so its barriers keep only copy accesses and wait on all of its own
		// commands; the layout change stays. The queue that reads the resources next waits on the
		// copy queue's timeline semaphore, which orders and makes visible everything before it.
		void SetQueue(GPUQueue queue) { m_Queue = queue; }

		void TransitionTexture(const TrackedTexture& texture, const TextureSubresourceRange& range, ResourceState before, ResourceState after) override;
		void TransitionBuffer(const TrackedBuffer& buffer, ResourceState before, ResourceState after) override;
		void Commit() override;

	private:
		nri::CommandBuffer* m_CommandBuffer = nullptr;
		GPUQueue m_Queue = GPUQueue::Graphics;
		std::vector<nri::TextureBarrierDesc> m_Textures;
		std::vector<nri::BufferBarrierDesc> m_Buffers;
	};

}
