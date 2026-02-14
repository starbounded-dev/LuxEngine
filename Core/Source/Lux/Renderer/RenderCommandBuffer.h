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
		static Ref<RenderCommandBuffer> Create(uint32_t count = 0, const std::string& debugName = "", bool enableQueries = false) { return Ref<RenderCommandBuffer>::Create(count, enableQueries, debugName); }

		void Begin();
		void End();
		void Submit();

		void RT_Begin();
		void RT_End();
		void RT_Submit();

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

		float GetExecutionGPUTime(uint32_t frameIndex) const;
		const PipelineStatistics& GetPipelineStatistics(uint32_t frameIndex) const;

		void RT_BeginTimerQuery(const std::string& name);
		void RT_EndTimerQuery();

		float GetTimerQueryTime(const std::string& name) const;
	public:
		static void LockQueue();
		static void UnlockQueue();
	public:
		RenderCommandBuffer(uint32_t count, bool enableQueries, const std::string& debugName);
		virtual ~RenderCommandBuffer() = default;
	private:
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
