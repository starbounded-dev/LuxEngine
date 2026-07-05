#pragma once

#include "Lux/Core/Ref.h"

#include "PipelineSpecification.h"

#include "nvrhi/nvrhi.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>

//pipeline queries require direct vulkan access, this can toggle that
#define CMD_BUFFER_USE_VULKAN_QUERIES
#include <vulkan/vulkan.h>

namespace Lux {

	class RenderCommandBuffer : public RefCounted
	{
	public:
		static Ref<RenderCommandBuffer> Create(uint32_t count = 0, const std::string& debugName = "", bool enableQueries = false, nvrhi::CommandQueue queue = nvrhi::CommandQueue::Graphics) { return Ref<RenderCommandBuffer>::Create(count, enableQueries, debugName, queue); }

		// recordFrameQueries controls the frame-level timer/pipeline-statistics query
		// bracketing. It must stay true for a normal single-submit frame. When a frame
		// is split into two graphics submits (async compute), only ONE of the two
		// Begin/End pairs may carry the frame query (the pool is per-frame-in-flight and
		// re-resetting it while the first submit is still in flight trips validation);
		// pass false on the second pair. Per-pass named queries are independent and keep
		// working in both halves.
		void Begin(bool recordFrameQueries = true);
		void End(bool recordFrameQueries = true);
		void Submit();

		void RT_Begin(bool recordFrameQueries = true);
		void RT_End(bool recordFrameQueries = true);
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

		float GetExecutionGPUTime(uint32_t frameIndex) const;
		const PipelineStatistics& GetPipelineStatistics(uint32_t frameIndex) const;

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
		nvrhi::static_vector<float, 3> m_GPUWorkTimes;

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

		nvrhi::static_vector<PipelineStatistics, 3> m_PipelineStatisticsQueryResults;

#ifdef CMD_BUFFER_USE_VULKAN_QUERIES
		uint32_t m_PipelineQueryCount = 0;
		std::vector<VkQueryPool> m_PipelineStatisticsQueryPools;
#endif
	};
}
