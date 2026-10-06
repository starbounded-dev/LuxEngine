// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "GPUDeletionQueue.h"

namespace Lux {

	void GPUDeletionQueue::Enqueue(uint64_t frame, std::function<void()> release, size_t captureBytes)
	{
		LUX_CORE_ASSERT(m_Entries.empty() || m_Entries.back().Frame <= frame, "GPU deletion queue frames must not go backwards");
		m_PendingBytes += captureBytes;
		m_Entries.push_back({ frame, std::move(release), captureBytes });
	}

	void GPUDeletionQueue::RT_Retire(uint64_t completedFrame)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// A release can queue another one (a destructor that frees through SubmitResourceFree); that
		// lands at the back with the current frame, so it is never retired in this same pass.
		while (!m_Entries.empty() && m_Entries.front().Frame <= completedFrame)
		{
			Entry entry = std::move(m_Entries.front());
			m_Entries.pop_front();
			m_PendingBytes -= entry.CaptureBytes;
			entry.Release();
		}
	}

	void GPUDeletionQueue::DrainAll()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		while (!m_Entries.empty())
		{
			Entry entry = std::move(m_Entries.front());
			m_Entries.pop_front();
			m_PendingBytes -= entry.CaptureBytes;
			entry.Release();
		}
	}

}
