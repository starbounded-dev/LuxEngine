#include "lpch.h"
#include "RenderCommandBuffer.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Platform/Vulkan/VulkanDiagnostics.h"
#include "Lux/Platform/Vulkan/VulkanSwapChain.h"

#include <unordered_map>
#include <unordered_set>

namespace Lux {

	static std::mutex s_GraphicsQueueMutex;

	RenderCommandBuffer::RenderCommandBuffer(uint32_t count, bool enableQueries, const std::string& debugName)
		: m_DebugName(debugName)
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
			m_PipelineStatisticsQueryResults.emplace_back();
		}

#ifdef LUX_DIST
		// GPU timer/statistics queries exist to feed the editor's profiling
		// panels; shipping builds skip the per-frame query begin/end/poll cost.
		enableQueries = false;
#endif
		m_QueryEnabled = enableQueries;

		if (enableQueries)
		{
			for (uint32_t i = 0; i < count; i++)
			{
				m_TimerQueries.push_back(device->createTimerQuery());

				m_NamedTimerQueries.emplace_back();
				m_GPUWorkTimes.push_back(0.f);
			}

#ifdef CMD_BUFFER_USE_VULKAN_QUERIES

			vk::QueryPoolCreateInfo queryPoolCreateInfo = {};

			// Pipeline statistics queries
			m_PipelineQueryCount = 7;
			queryPoolCreateInfo.queryType = vk::QueryType::ePipelineStatistics;
			queryPoolCreateInfo.queryCount = m_PipelineQueryCount;
			queryPoolCreateInfo.pipelineStatistics =
				vk::QueryPipelineStatisticFlagBits::eInputAssemblyVertices |
				vk::QueryPipelineStatisticFlagBits::eInputAssemblyPrimitives |
				vk::QueryPipelineStatisticFlagBits::eVertexShaderInvocations |
				vk::QueryPipelineStatisticFlagBits::eClippingInvocations |
				vk::QueryPipelineStatisticFlagBits::eClippingPrimitives |
				vk::QueryPipelineStatisticFlagBits::eFragmentShaderInvocations |
				vk::QueryPipelineStatisticFlagBits::eComputeShaderInvocations;

			vk::Device vkdevice = vk::Device(device->getNativeObject(nvrhi::ObjectTypes::VK_Device));

			m_PipelineStatisticsQueryPools.resize(count);
			for (auto& pipelineStatisticsQueryPool : m_PipelineStatisticsQueryPools)
			{
				vk::QueryPool qp;
				//@TODO we are not destroying these cleanly
				auto r = vkdevice.createQueryPool(&queryPoolCreateInfo, nullptr, &qp);

				pipelineStatisticsQueryPool = qp;
			}

			// Timestamp queries
			vk::QueryPoolCreateInfo timestampQueryPoolCreateInfo = {};
			timestampQueryPoolCreateInfo.queryType = vk::QueryType::eTimestamp;
			timestampQueryPoolCreateInfo.queryCount = 2; // Begin and end timestamps
#endif
		}
	}

	void RenderCommandBuffer::Begin()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Ref<RenderCommandBuffer> instance = this;
		Renderer::Submit([instance]() mutable {
			instance->RT_Begin();
			});
	}

	void RenderCommandBuffer::End()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Ref<RenderCommandBuffer> instance = this;
		Renderer::Submit([instance]() mutable { instance->RT_End(); });
	}

	void RenderCommandBuffer::Submit()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Ref<RenderCommandBuffer> instance = this;
		Renderer::Submit([instance]() mutable { instance->RT_Submit(); });
	}

	void RenderCommandBuffer::RT_Begin()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		uint32_t commandBufferIndex = Renderer::RT_GetCurrentFrameIndex();
		commandBufferIndex %= m_CommandLists.size();

		m_ActiveCommandBuffer = m_CommandLists[commandBufferIndex];



		m_ActiveCommandBuffer->open();

		auto device = Application::GetGraphicsDevice();

		if (m_QueryEnabled)
		{
			m_ActiveTimerQuery = m_TimerQueries[commandBufferIndex];

			// Poll and cache the frame-level timer query result BEFORE resetting
			if (device->pollTimerQuery(m_ActiveTimerQuery))
			{
				float timeInSeconds = device->getTimerQueryTime(m_ActiveTimerQuery);
				m_GPUWorkTimes[commandBufferIndex] = timeInSeconds * 1000.0f;
			}

			// Reset and begin the frame-level timer query
			device->resetTimerQuery(m_ActiveTimerQuery);

			//do the same with the smaller queries
			for (auto& [name, timerQuery] : m_NamedTimerQueries[commandBufferIndex])
			{
				if (device->pollTimerQuery(timerQuery))
				{
					float timeInSeconds = device->getTimerQueryTime(timerQuery);
					float timeInMs = timeInSeconds * 1000.0f;
					auto it = m_NamedTimerQueryResults.find(name);
					if (it != m_NamedTimerQueryResults.end())
					{
						//moving average
						m_NamedTimerQueryResults[name] = timeInMs * 0.1 + it->second * 0.9;
					}
					else
					{
						m_NamedTimerQueryResults[name] = timeInMs;
					}
				}
			}

			m_ActiveCommandBuffer->beginTimerQuery(m_ActiveTimerQuery);
		}

		RT_BeginMarker(m_DebugName);

#ifdef CMD_BUFFER_USE_VULKAN_QUERIES

		if (m_QueryEnabled)
		{
			vk::CommandBuffer cmd = vk::CommandBuffer(m_ActiveCommandBuffer->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer));

			// Pipeline stats query
			cmd.resetQueryPool(m_PipelineStatisticsQueryPools[commandBufferIndex], 0, m_PipelineQueryCount);
			cmd.beginQuery(m_PipelineStatisticsQueryPools[commandBufferIndex], 0, vk::QueryControlFlags{});
		}
#endif
	}

	void RenderCommandBuffer::RT_End()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		RT_EndMarker();

		if (m_QueryEnabled)
		{
			m_ActiveCommandBuffer->endTimerQuery(m_ActiveTimerQuery);


			uint32_t commandBufferIndex = Renderer::RT_GetCurrentFrameIndex();
			commandBufferIndex %= m_CommandLists.size();
#ifdef CMD_BUFFER_USE_VULKAN_QUERIES
			vk::CommandBuffer cmd = vk::CommandBuffer(m_ActiveCommandBuffer->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer));

			cmd.endQuery(m_PipelineStatisticsQueryPools[commandBufferIndex], 0);
#endif
			m_ActiveTimerQuery = nullptr;
		}
		m_ActiveCommandBuffer->close();

		m_ActiveCommandBuffer = nullptr;

	}

	void RenderCommandBuffer::RT_Submit()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		RT_Submit(VK_NULL_HANDLE);
	}

	void RenderCommandBuffer::RT_Submit(VkSemaphore waitSemaphore)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_TRACE_TAG("Renderer", "Submitting Render Command Buffer {}", m_DebugName);

		auto device = Application::GetGraphicsDevice();

		if (m_QueryEnabled)
		{
			if (m_TimerQueryStack.size() > 0)
			{
				LUX_CORE_WARN("Mismatched timer queries '{}'!", m_TimerQueryStack[0]);
				m_TimerQueryStack.clear();
			}
		}

		// TODO(Yan): fences

		uint32_t commandBufferIndex = Renderer::RT_GetCurrentFrameIndex();
		commandBufferIndex %= m_CommandLists.size();

		LockQueue();
		if (waitSemaphore)
		{
			auto vulkanDevice = (nvrhi::vulkan::IDevice*)device.Get();
			vulkanDevice->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, waitSemaphore, 0);
		}
		device->executeCommandList(m_CommandLists[commandBufferIndex]);
		UnlockQueue();

#ifdef CMD_BUFFER_USE_VULKAN_QUERIES
		if (m_QueryEnabled)
		{
			vk::Device vkdevice = vk::Device(device->getNativeObject(nvrhi::ObjectTypes::VK_Device));
			vkdevice.getQueryPoolResults(m_PipelineStatisticsQueryPools[commandBufferIndex], 0, 1,
				sizeof(PipelineStatistics), (void*)&m_PipelineStatisticsQueryResults[commandBufferIndex], vk::DeviceSize(sizeof(uint64_t)), vk::QueryResultFlagBits::e64);
		}
#endif
	}

	void RenderCommandBuffer::RT_Wait(VkSemaphore waitSemaphore)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		auto device = (nvrhi::vulkan::IDevice*)Application::GetGraphicsDevice().Get();
		LockQueue();
		device->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, waitSemaphore, 0);
		UnlockQueue();
	}

	void RenderCommandBuffer::RT_BeginMarker(const std::string& label)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		nvrhi::CommandListHandle commandList = GetActive();
		commandList->beginMarker(label.c_str());
		Utils::SetVulkanCheckpoint(VkCommandBuffer(commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer)), label);
	}

	void RenderCommandBuffer::RT_EndMarker()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		GetActive()->endMarker();
	}

	void RenderCommandBuffer::RT_CommitGraphicsState()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_ActiveCommandBuffer->setGraphicsState(m_GraphicsState);
	}

	void RenderCommandBuffer::RT_CommitComputeState()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_ActiveCommandBuffer->setComputeState(m_ComputeState);
	}

	float RenderCommandBuffer::GetExecutionGPUTime(uint32_t frameIndex) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_QueryEnabled)
		{
			// Use the frame-level timer query
			frameIndex %= m_TimerQueries.size();

			return m_GPUWorkTimes[frameIndex];
		}
		else
		{
			return 0;
		}
	}

	const Lux::PipelineStatistics& RenderCommandBuffer::GetPipelineStatistics(uint32_t frameIndex) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_PipelineStatisticsQueryResults[frameIndex % m_PipelineStatisticsQueryResults.size()];
	}

	void RenderCommandBuffer::RT_BeginTimerQuery(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_QueryEnabled)
		{
			return;
		}

		auto device = Application::GetGraphicsDevice();

		uint32_t commandBufferIndex = Renderer::RT_GetCurrentFrameIndex();
		commandBufferIndex %= m_CommandLists.size();

		// Get or create the timer query for this name
		auto& timerQuery = m_NamedTimerQueries[commandBufferIndex][name];
		if (!timerQuery)
		{
			timerQuery = device->createTimerQuery();
			if (!timerQuery)
			{
				LUX_CORE_ERROR("Failed to create timer query for '{}'!", name);
				return;
			}
		}

		nvrhi::ITimerQuery* queryPtr = timerQuery.Get();

		device->resetTimerQuery(queryPtr);
		m_ActiveCommandBuffer->beginTimerQuery(queryPtr);

		// Push onto the stack
		m_TimerQueryStack.push_back(name);
	}

	void RenderCommandBuffer::RT_EndTimerQuery()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_QueryEnabled)
		{
			return;
		}

		if (m_TimerQueryStack.empty())
		{
			LUX_CORE_WARN("Mismatched GPU Queries!, last was {}", lastpop);
			return;
		}

		uint32_t commandBufferIndex = Renderer::RT_GetCurrentFrameIndex();
		commandBufferIndex %= m_CommandLists.size();

		// Pop the top query from the stack
		std::string name = std::move(m_TimerQueryStack.back());
		m_TimerQueryStack.pop_back();

		auto it = m_NamedTimerQueries[commandBufferIndex].find(name);
		if (it == m_NamedTimerQueries[commandBufferIndex].end() || !it->second)
		{
			LUX_CORE_ERROR("Mismatched GPU Queries! {}", name);
			return;
		}

		auto& timerQuery = it->second;
		m_ActiveCommandBuffer->endTimerQuery(timerQuery.Get());
		lastpop = name;
	}

	float RenderCommandBuffer::GetTimerQueryTime(const std::string& name) const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_QueryEnabled)
		{
			auto it = m_NamedTimerQueryResults.find(name);
			if (it != m_NamedTimerQueryResults.end())
				return it->second;
		}

		return 0.0f;
	}

	void RenderCommandBuffer::LockQueue()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		s_GraphicsQueueMutex.lock();
	}

	void RenderCommandBuffer::UnlockQueue()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		s_GraphicsQueueMutex.unlock();
	}
}
