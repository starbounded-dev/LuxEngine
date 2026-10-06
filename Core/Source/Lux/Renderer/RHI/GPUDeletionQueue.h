// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>

namespace Lux {

	// GPU object releases deferred until the GPU has finished the frame that last used them.
	// Each release is tagged with the render frame it was queued in; Renderer::RT_BeginFrame
	// retires everything up to the newest frame whose completion it has waited on.
	//
	// Threading: render thread only. Renderer::SubmitResourceFree routes every release there, and
	// DrainAll runs at shutdown after the render thread has stopped.
	class GPUDeletionQueue
	{
	public:
		// `frame` must not be lower than the frame of any release already queued.
		void Enqueue(uint64_t frame, std::function<void()> release, size_t captureBytes);
		// Runs every release queued in `completedFrame` or earlier.
		void RT_Retire(uint64_t completedFrame);
		// Runs everything. Only once the GPU is idle.
		void DrainAll();

		size_t GetPendingCount() const { return m_Entries.size(); }
		size_t GetPendingBytes() const { return m_PendingBytes; }

	private:
		struct Entry
		{
			uint64_t Frame = 0;
			std::function<void()> Release;
			size_t CaptureBytes = 0;
		};

		std::deque<Entry> m_Entries;
		size_t m_PendingBytes = 0;
	};

}
