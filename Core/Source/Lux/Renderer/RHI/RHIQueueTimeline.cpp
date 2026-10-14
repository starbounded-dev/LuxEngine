// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "RHIQueueTimeline.h"

#include "Lux/Platform/Vulkan/VulkanDevice.h"
#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/RHI/RHIDevice.h"

#include <algorithm>
#include <array>
#include <atomic>

namespace Lux {

	namespace {

		struct QueueTimeline
		{
			nri::Fence* Fence = nullptr;
			// Written under the queue lock, read anywhere.
			std::atomic<uint64_t> Submitted = 0;
			// Under the queue lock: what the next submission that takes added waits waits for, the
			// highest value per waited queue.
			std::array<uint64_t, static_cast<size_t>(GPUQueue::Count)> TimelineWaits = {};
		};

		std::array<QueueTimeline, static_cast<size_t>(GPUQueue::Count)> s_Queues;

		QueueTimeline& Get(GPUQueue queue)
		{
			LUX_CORE_ASSERT(queue < GPUQueue::Count);
			return s_Queues[static_cast<size_t>(queue)];
		}

		struct QueueLock
		{
			QueueLock() { RenderCommandBuffer::LockQueue(); }
			~QueueLock() { RenderCommandBuffer::UnlockQueue(); }
		};

	}

	void RHIQueueTimeline::Init()
	{
		const NRIInterface& api = RHIDevice::API();
		for (size_t i = 0; i < s_Queues.size(); i++)
		{
			if (!RHIDevice::GetQueue(static_cast<GPUQueue>(i)))
				continue;
			LUX_CORE_VERIFY(api.CreateFence(RHIDevice::Get(), 0, s_Queues[i].Fence) == nri::Result::SUCCESS, "Failed to create a queue timeline fence");
			api.SetDebugName(s_Queues[i].Fence, "Queue timeline");
		}
	}

	void RHIQueueTimeline::Shutdown()
	{
		for (QueueTimeline& timeline : s_Queues)
		{
			if (timeline.Fence)
				RHIDevice::API().DestroyFence(timeline.Fence);
			timeline.Fence = nullptr;
			timeline.Submitted = 0;
			timeline.TimelineWaits = {};
		}
	}

	bool RHIQueueTimeline::HasQueue(GPUQueue queue)
	{
		return Get(queue).Fence != nullptr;
	}

	uint64_t RHIQueueTimeline::Submit(GPUQueue queue, const Submission& submission)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		QueueTimeline& timeline = Get(queue);
		nri::Queue* nriQueue = RHIDevice::GetQueue(queue);
		LUX_CORE_VERIFY(timeline.Fence && nriQueue, "Submission to a queue the device does not have");

		QueueLock lock;
		std::array<nri::FenceSubmitDesc, static_cast<size_t>(GPUQueue::Count) + 1> waits = {};
		uint32_t waitCount = 0;
		if (submission.WaitSemaphore)
			waits[waitCount++] = { submission.WaitSemaphore, 0, nri::StageBits::ALL };
		for (size_t i = 0; submission.TakesAddedWaits && i < timeline.TimelineWaits.size(); i++)
		{
			if (timeline.TimelineWaits[i] != 0)
				waits[waitCount++] = { s_Queues[i].Fence, timeline.TimelineWaits[i], nri::StageBits::ALL };
		}

		const uint64_t value = timeline.Submitted.load(std::memory_order_relaxed) + 1;
		std::array<nri::FenceSubmitDesc, 2> signals = {};
		signals[0] = { timeline.Fence, value, nri::StageBits::ALL };
		signals[1] = { submission.SignalSemaphore, 0, nri::StageBits::ALL };

		nri::QueueSubmitDesc submitDesc = {};
		submitDesc.waitFences = waits.data();
		submitDesc.waitFenceNum = waitCount;
		submitDesc.commandBuffers = submission.CommandBuffers.data();
		submitDesc.commandBufferNum = static_cast<uint32_t>(submission.CommandBuffers.size());
		submitDesc.signalFences = signals.data();
		submitDesc.signalFenceNum = submission.SignalSemaphore ? 2u : 1u;

		const nri::Result result = RHIDevice::API().QueueSubmit(*nriQueue, submitDesc);
		if (result == nri::Result::DEVICE_LOST)
			VulkanDevice::ReportDeviceLost("QueueSubmit");
		if (result != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Queue submission failed (NRI result {})", static_cast<int>(result));
			return 0;
		}

		// The waits are consumed only by a submission that happened.
		if (submission.TakesAddedWaits)
			timeline.TimelineWaits = {};
		timeline.Submitted.store(value, std::memory_order_release);
		return value;
	}

	void RHIQueueTimeline::AddWait(GPUQueue queue, GPUQueue waitedQueue, uint64_t value)
	{
		if (queue == waitedQueue || value == 0 || !HasQueue(waitedQueue))
			return;

		QueueLock lock;
		uint64_t& wait = Get(queue).TimelineWaits[static_cast<size_t>(waitedQueue)];
		wait = std::max(wait, value);
	}

	uint64_t RHIQueueTimeline::GetSubmittedValue(GPUQueue queue)
	{
		return Get(queue).Submitted.load(std::memory_order_acquire);
	}

	uint64_t RHIQueueTimeline::GetCompletedValue(GPUQueue queue)
	{
		nri::Fence* fence = Get(queue).Fence;
		return fence ? RHIDevice::API().GetFenceValue(*fence) : 0;
	}

	void RHIQueueTimeline::Wait(GPUQueue queue, uint64_t value)
	{
		nri::Fence* fence = Get(queue).Fence;
		if (!fence || value == 0)
			return;

		LUX_PROFILE_FUNCTION_AUTO;
		RHIDevice::API().Wait(*fence, value);
	}

}
