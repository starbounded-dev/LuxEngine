// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/RHI/RHITypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nri {
	struct CommandAllocator;
	struct CommandBuffer;
}

namespace Lux {

	// NRI command buffers for one queue, each with its own allocator, reused once the GPU has
	// finished their last submission (RHIQueueTimeline values). The pool starts with
	// `initialCount` and grows when every command buffer is still in flight, as NVRHI's command
	// lists did (NRI migration Phase 13).
	//
	// Threading: one thread at a time (the owner serializes).
	class CommandBufferPool
	{
	public:
		CommandBufferPool(GPUQueue queue, uint32_t initialCount, std::string debugName);
		// Frees the command buffers through the deletion queue.
		~CommandBufferPool();
		CommandBufferPool(const CommandBufferPool&) = delete;
		CommandBufferPool& operator=(const CommandBufferPool&) = delete;

		GPUQueue GetQueue() const { return m_Queue; }

		// A reset command buffer in the recording state. Null, logged, when NRI fails.
		nri::CommandBuffer* Begin();
		// Ends the command buffer Begin returned. False, logged, when NRI fails.
		bool End(nri::CommandBuffer& commandBuffer);
		// The timeline value of its submission on the pool's queue; it is reused after that.
		void MarkSubmitted(nri::CommandBuffer& commandBuffer, uint64_t value);

	private:
		struct Entry
		{
			nri::CommandAllocator* Allocator = nullptr;
			nri::CommandBuffer* CommandBuffer = nullptr;
			uint64_t RetireValue = 0;
		};

		bool AddEntry();

		GPUQueue m_Queue;
		std::string m_DebugName;
		std::vector<Entry> m_Entries;
		// Where the next search for a free command buffer starts, so they are used in turn.
		size_t m_Next = 0;
	};

}
