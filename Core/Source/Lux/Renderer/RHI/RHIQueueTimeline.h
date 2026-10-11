// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/RHI/RHITypes.h"

#include <cstdint>
#include <span>

namespace nri {
	struct CommandBuffer;
	struct Fence;
}

namespace Lux {

	// GPU submission and completion per queue (NRI migration Phase 13): one NRI timeline fence per
	// queue, advanced by every submission to it, so a value names a submission and the submission
	// is complete once the queue's fence reaches it. Cross-queue waits added for a queue are carried
	// by its next submission that takes them.
	//
	// Threading: any thread. Submissions and added waits are serialized by
	// RenderCommandBuffer::LockQueue (one lock for every queue); CPU waits take no lock.
	class RHIQueueTimeline
	{
	public:
		// After RHIDevice::Init: a fence for each queue the device has.
		static void Init();
		// Before RHIDevice::Shutdown, with the GPU idle.
		static void Shutdown();

		static bool HasQueue(GPUQueue queue);

		struct Submission
		{
			// Possibly none: a submission can only wait and signal.
			std::span<nri::CommandBuffer* const> CommandBuffers;
			// NRI wrappers of binary semaphores, such as a swapchain's acquire and present
			// semaphores; optional. A semaphore wait belongs to the submission that first uses what
			// it guards, so it is passed here, never left for whichever submission comes next.
			nri::Fence* WaitSemaphore = nullptr;
			nri::Fence* SignalSemaphore = nullptr;
			// Whether the submission carries the waits added for the queue (AddWait). The upload
			// batch leaves them for the command buffer it is flushed ahead of.
			bool TakesAddedWaits = true;
		};
		// Submits to `queue`, then signals the queue's timeline. Returns the submission's value; 0,
		// logged, when it fails (the added waits then stay for the next submission).
		static uint64_t Submit(GPUQueue queue, const Submission& submission);

		// The next submission to `queue` that takes added waits waits for `waitedQueue` to reach
		// `value`. Ignored for the same queue (it runs in order), for value 0, and for a queue the
		// device does not have.
		static void AddWait(GPUQueue queue, GPUQueue waitedQueue, uint64_t value);

		// The value of the latest submission to `queue`, and the latest it has completed.
		static uint64_t GetSubmittedValue(GPUQueue queue);
		static uint64_t GetCompletedValue(GPUQueue queue);
		// Blocks until `queue` reaches `value` (at once for 0).
		static void Wait(GPUQueue queue, uint64_t value);
	};

}
