#pragma once

#include "StarEngine/Core/Ref.h"

#include "PipelineSpecification.h"

#include "nvrhi/nvrhi.h"

// TODO(shea): REMOVE
#include <vulkan/vulkan.hpp>

#include <mutex>

namespace StarEngine {

	// TODO(shea): Markers!
	class RenderCommandBuffer : public RefCounted
	{
	public:
		static Ref<RenderCommandBuffer> Create(uint32_t count = 0, const std::string& debugName = "") { return Ref<RenderCommandBuffer>::Create(count, debugName); }

		void Begin();
		void End();
		void Submit();

		void RT_Begin();
		void RT_End();
		void RT_Submit();

		void RT_Wait(vk::Semaphore waitSemaphore);

		nvrhi::GraphicsState& GetGraphicsState() { return m_GraphicsState; }
		const nvrhi::GraphicsState& GetGraphicsState() const { return m_GraphicsState; }
		void SetGraphicsState(nvrhi::GraphicsState& graphicsState) { m_GraphicsState = graphicsState; }
		void RT_CommitGraphicsState();

		nvrhi::CommandListHandle GetActive() const { return m_ActiveCommandBuffer; }
		nvrhi::CommandListHandle Get(uint32_t index = 0) const { SE_CORE_VERIFY(index < m_CommandLists.size());  return m_CommandLists[index]; }

		float GetExecutionGPUTime(uint32_t frameIndex, nvrhi::TimerQueryHandle timerQuery = nullptr) const;
		const PipelineStatistics& GetPipelineStatistics(uint32_t frameIndex) const;

		nvrhi::TimerQueryHandle BeginTimerQuery();
		void EndTimerQuery(nvrhi::TimerQueryHandle timerQuery);
	public:
		static void LockQueue();
		static void UnlockQueue();
	public:
		RenderCommandBuffer(uint32_t count, const std::string& debugName);
		virtual ~RenderCommandBuffer() = default;
	private:
		nvrhi::static_vector<nvrhi::CommandListHandle, 3> m_CommandLists;
		nvrhi::static_vector<nvrhi::TimerQueryHandle, 3> m_TimerQueries;

		std::string m_DebugName;

		nvrhi::CommandListHandle m_ActiveCommandBuffer;
		nvrhi::TimerQueryHandle m_ActiveTimerQuery;

		nvrhi::GraphicsState m_GraphicsState;

		// std::vector<VkFence> m_WaitFences;

		uint32_t m_TimestampQueryCount = 0;
		uint32_t m_TimestampNextAvailableQuery = 2;
		// std::vector<VkQueryPool> m_TimestampQueryPools;
		// std::vector<VkQueryPool> m_PipelineStatisticsQueryPools;
		std::vector<std::vector<uint64_t>> m_TimestampQueryResults;
		std::vector<std::vector<float>> m_ExecutionGPUTimes;

		uint32_t m_PipelineQueryCount = 0;
		nvrhi::static_vector<PipelineStatistics, 3> m_PipelineStatisticsQueryResults;
	};

}
