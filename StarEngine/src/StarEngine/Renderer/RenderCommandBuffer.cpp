#include "sepch.h"
#include "RenderCommandBuffer.h"

#include "StarEngine/Platform/Vulkan/VulkanDeviceManager.h"
#include "StarEngine/Renderer/Renderer.h"
#include "StarEngine/Platform/Vulkan/VulkanSwapChain.h"

namespace StarEngine {

	static std::mutex s_GraphicsQueueMutex;

	RenderCommandBuffer::RenderCommandBuffer(uint32_t count, const std::string& debugName)
	{
		if (count == 0)
		{
			// 0 means one per frame in flight
			count = Renderer::GetConfig().FramesInFlight;
		}

		auto device = Application::GetGraphicsDevice();

		for (uint32_t i = 0; i < count; i++)
		{
			m_CommandLists.push_back(device->createCommandList());
			m_TimerQueries.push_back(device->createTimerQuery());
			m_PipelineStatisticsQueryResults.emplace_back();
		}
	}

	void RenderCommandBuffer::Begin()
	{
		Ref<RenderCommandBuffer> instance = this;
		Renderer::Submit([instance]() mutable { instance->RT_Begin(); });
	}

	void RenderCommandBuffer::End()
	{
		Ref<RenderCommandBuffer> instance = this;
		Renderer::Submit([instance]() mutable { instance->RT_End(); });

	}

	void RenderCommandBuffer::Submit()
	{
		Ref<RenderCommandBuffer> instance = this;
		Renderer::Submit([instance]() mutable { instance->RT_Submit(); });
	}

	void RenderCommandBuffer::RT_Begin()
	{
		uint32_t commandBufferIndex = Renderer::RT_GetCurrentFrameIndex();
		commandBufferIndex %= m_CommandLists.size();

		m_ActiveCommandBuffer = m_CommandLists[commandBufferIndex];
		m_ActiveTimerQuery = m_TimerQueries[commandBufferIndex];

		m_ActiveCommandBuffer->open();

		auto device = Application::GetGraphicsDevice();

		//m_ActiveCommandBuffer->beginTimerQuery(m_ActiveTimerQuery);
		m_ActiveCommandBuffer->beginMarker(m_DebugName.c_str());

		// Pipeline stats query
		// vkCmdResetQueryPool(commandBuffer, instance->m_PipelineStatisticsQueryPools[commandBufferIndex], 0, instance->m_PipelineQueryCount);
		// vkCmdBeginQuery(commandBuffer, instance->m_PipelineStatisticsQueryPools[commandBufferIndex], 0, 0);
	}

	void RenderCommandBuffer::RT_End()
	{
		m_ActiveCommandBuffer->endMarker();
		//	m_ActiveCommandBuffer->endTimerQuery(m_ActiveTimerQuery);

		m_ActiveCommandBuffer->close();

		m_ActiveCommandBuffer = nullptr;
		m_ActiveTimerQuery = nullptr;
	}

	void RenderCommandBuffer::RT_Submit()
	{
		SE_CORE_TRACE_TAG("Renderer", "Submitting Render Command Buffer {}", m_DebugName);

		auto device = Application::GetGraphicsDevice();

		// TODO(Yan): fences

		uint32_t commandBufferIndex = Renderer::RT_GetCurrentFrameIndex();
		commandBufferIndex %= m_CommandLists.size();

		LockQueue();
		device->executeCommandList(m_CommandLists[commandBufferIndex]);
		UnlockQueue();
	}

	void RenderCommandBuffer::RT_Wait(vk::Semaphore waitSemaphore)
	{
		auto device = (nvrhi::vulkan::IDevice*)Application::GetGraphicsDevice().Get();
		LockQueue();
		device->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, waitSemaphore, 0);
		UnlockQueue();
	}

	void RenderCommandBuffer::RT_CommitGraphicsState()
	{
		m_ActiveCommandBuffer->setGraphicsState(m_GraphicsState);
	}

	float RenderCommandBuffer::GetExecutionGPUTime(uint32_t frameIndex, nvrhi::TimerQueryHandle timerQuery) const
	{
		return 0.0f;
	}

	const StarEngine::PipelineStatistics& RenderCommandBuffer::GetPipelineStatistics(uint32_t frameIndex) const
	{
		return m_PipelineStatisticsQueryResults[0];
	}

	nvrhi::TimerQueryHandle RenderCommandBuffer::BeginTimerQuery()
	{
		return nullptr;
	}

	void RenderCommandBuffer::EndTimerQuery(nvrhi::TimerQueryHandle queryID)
	{
	}

	void RenderCommandBuffer::LockQueue()
	{
		s_GraphicsQueueMutex.lock();
	}

	void RenderCommandBuffer::UnlockQueue()
	{
		s_GraphicsQueueMutex.unlock();
	}

}
