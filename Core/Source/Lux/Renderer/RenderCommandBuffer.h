#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Core/Ref.h"
#include "Lux/Debug/Profiler.h"

#include "PipelineSpecification.h"

#include "nvrhi/nvrhi.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

//pipeline queries require direct vulkan access, this can toggle that
#define CMD_BUFFER_USE_VULKAN_QUERIES
#include <vulkan/vulkan.h>

#if LUX_ENABLE_PROFILING
#include <tracy/TracyVulkan.hpp>
#endif

namespace Lux {

	class RenderCommandBuffer : public RefCounted
	{
	public:
		static Ref<RenderCommandBuffer> Create(uint32_t count = 0, const std::string& debugName = "", bool enableQueries = false, nvrhi::CommandQueue queue = nvrhi::CommandQueue::Graphics) { return Ref<RenderCommandBuffer>::Create(count, enableQueries, debugName, queue); }

		void Begin();
		void End();
		void Submit();

		void RT_Begin();
		void RT_End();
		void RT_Submit();
		void RT_Submit(VkSemaphore waitSemaphore);

		void RT_Wait(VkSemaphore waitSemaphore);

		void RT_BeginMarker(const std::string& label);
		void RT_EndMarker();

		nvrhi::GraphicsState& GetGraphicsState() { return m_GraphicsState; }
		const nvrhi::GraphicsState& GetGraphicsState() const { return m_GraphicsState; }
		void SetGraphicsState(nvrhi::GraphicsState& graphicsState) { m_GraphicsState = graphicsState; }
		void RT_CommitGraphicsState();

		nvrhi::ComputeState& GetComputeState() { return m_ComputeState; }
		const nvrhi::ComputeState& GetComputeState() const { return m_ComputeState; }
		void SetComputeState(nvrhi::ComputeState& computeState) { m_ComputeState = computeState; }
		void RT_CommitComputeState();

		nvrhi::CommandListHandle GetActive() const { return m_ActiveCommandBuffer; }
		nvrhi::CommandListHandle Get(uint32_t index = 0) const { LUX_CORE_VERIFY(index < m_CommandLists.size());  return m_CommandLists[index]; }

		// The queue this command buffer records/submits on (Graphics by default).
		nvrhi::CommandQueue GetQueue() const { return m_Queue; }
		// The nvrhi execution-instance id returned by the most recent submit on this
		// buffer's queue. Feed it to Renderer::QueueWaitForCommandList so another
		// queue can wait for this buffer's work to finish (cross-queue sync).
		uint64_t GetLastExecutionInstance() const { return m_LastExecutionInstance; }

		// Most recently resolved frame-level GPU time, in milliseconds. Not indexed by
		// frame: see m_LastGPUWorkTime for why a per-index handoff cannot work here.
		float GetExecutionGPUTime() const;
		PipelineStatistics GetPipelineStatistics() const;

		void RT_BeginTimerQuery(const std::string& name);
		void RT_EndTimerQuery();

		float GetTimerQueryTime(const std::string& name) const;
	public:
		static void LockQueue();
		static void UnlockQueue();
	public:
		RenderCommandBuffer(uint32_t count, bool enableQueries, const std::string& debugName, nvrhi::CommandQueue queue = nvrhi::CommandQueue::Graphics);
		virtual ~RenderCommandBuffer() = default;
	private:
		nvrhi::CommandQueue m_Queue = nvrhi::CommandQueue::Graphics;
		uint64_t m_LastExecutionInstance = 0;

		nvrhi::static_vector<nvrhi::CommandListHandle, 3> m_CommandLists;
		nvrhi::static_vector<nvrhi::TimerQueryHandle, 3> m_TimerQueries;

		// Published by the render thread, read by the main thread (profiling panels).
		//
		// Deliberately a single latest value rather than a per-frame slot. The render
		// thread indexes its command lists and query pools by
		// Renderer::RT_GetCurrentFrameIndex() - the swapchain back-buffer index - while
		// every caller of the getters below is on the main thread, holding
		// Application::m_CurrentFrameIndex. Those are two unrelated sequences with
		// different periods, and under VK_PRESENT_MODE_MAILBOX_KHR the acquired
		// back-buffer index is not even monotonic. Handing a value between the threads
		// via either index reads a foreign slot, which is what previously reported a
		// ~0.01 ms frame time against multi-millisecond pass timings.
		//
		// This mirrors m_NamedTimerQueryResults, which is keyed by name only and has
		// always reported correctly for exactly this reason.
		std::atomic<float> m_LastGPUWorkTime = 0.0f;

		bool m_QueryEnabled;

		std::string m_DebugName;

		nvrhi::CommandListHandle m_ActiveCommandBuffer;
		nvrhi::TimerQueryHandle m_ActiveTimerQuery;

		nvrhi::GraphicsState m_GraphicsState;
		nvrhi::ComputeState m_ComputeState;

		std::string lastpop;

		// String-based timer query storage - allocated on demand
		nvrhi::static_vector < std::unordered_map<std::string, nvrhi::TimerQueryHandle>, 3> m_NamedTimerQueries;
		std::unordered_map<std::string, float> m_NamedTimerQueryResults;
		std::vector<std::string> m_TimerQueryStack;  // Stack of active timer queries

#if LUX_ENABLE_PROFILING
		// Tracy GPU zones, pushed and popped in lockstep with m_TimerQueryStack so every
		// profiled pass shows up on Tracy's GPU timeline alongside the engine's own
		// timer-query number.
		//
		// Held through Scope because tracy::VkCtxScope is neither copyable nor movable -
		// its destructor is what writes the closing timestamp - so it cannot sit in a
		// vector directly. The per-zone allocation is acceptable here: this whole path
		// compiles out in Dist, and TRACY_ON_DEMAND makes each zone inert until a
		// profiler actually connects.
		std::vector<Scope<tracy::VkCtxScope>> m_TracyGPUZones;
#endif

		// Same publishing rule as m_LastGPUWorkTime: written on the render thread from
		// whichever query pool was just submitted, read on the main thread. Atomic for
		// the same reason - these seven counters are read as a set, and a torn read
		// would report vertex and fragment totals from different frames.
		std::atomic<PipelineStatistics> m_LastPipelineStatistics = PipelineStatistics{};

#ifdef CMD_BUFFER_USE_VULKAN_QUERIES
		uint32_t m_PipelineQueryCount = 0;
		std::vector<VkQueryPool> m_PipelineStatisticsQueryPools;
#endif
	};
}
