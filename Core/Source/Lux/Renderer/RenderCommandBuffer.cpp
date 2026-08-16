#include "lpch.h"
#include "RenderCommandBuffer.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Platform/Vulkan/VulkanDiagnostics.h"
#include "Lux/Platform/Vulkan/VulkanSwapChain.h"

#include <unordered_map>
#include <unordered_set>

namespace Lux {

	static std::mutex s_GraphicsQueueMutex;

	// Exponential smoothing weight for the newest GPU timer sample, so the profiling
	// panels do not flicker on single-frame outliers. Matches the weight already used
	// for the named per-pass queries below, which keeps the frame-level total directly
	// comparable to the sum of the per-pass timings displayed next to it.
	static constexpr float kGPUTimeSmoothing = 0.1f;

	RenderCommandBuffer::RenderCommandBuffer(uint32_t count, bool enableQueries, const std::string& debugName, nvrhi::CommandQueue queue)
		: m_Queue(queue), m_DebugName(debugName)
	{
		if (count == 0)
		{
			// 0 means one per frame in flight
			count = Renderer::GetConfig().FramesInFlight;
		}

		auto device = Application::GetGraphicsDevice();

		// Command lists are bound to a queue type at creation; a Compute list can
		// only be executed on the compute queue (COPY/COMPUTE expose a subset of
		// methods). Graphics (the default) is unchanged from before.
		nvrhi::CommandListParameters clParams;
		clParams.setQueueType(m_Queue);

		for (uint32_t i = 0; i < count; i++)
			m_CommandLists.push_back(device->createCommandList(clParams));

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

			// Poll and publish the frame-level timer query result BEFORE resetting.
			// A query that is not ready yet simply leaves the previous value in place;
			// the panels then show the last resolved frame rather than a zero.
			if (device->pollTimerQuery(m_ActiveTimerQuery))
			{
				const float timeInMs = device->getTimerQueryTime(m_ActiveTimerQuery) * 1000.0f;
				const float previous = m_LastGPUWorkTime.load(std::memory_order_relaxed);

				// Seed on the first resolved sample, otherwise the average crawls up from
				// zero over dozens of frames and reads as a bogus sub-millisecond frame.
				const float smoothed = previous > 0.0f
					? timeInMs * kGPUTimeSmoothing + previous * (1.0f - kGPUTimeSmoothing)
					: timeInMs;

				m_LastGPUWorkTime.store(smoothed, std::memory_order_relaxed);
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

		// Flush batched resource uploads first: anything this command list may
		// consume (mesh buffers, texture data) must reach the queue ahead of it.
		// Cheap no-op when nothing is pending.
		Renderer::FlushResourceUploads();

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
			vulkanDevice->queueWaitForSemaphore(m_Queue, waitSemaphore, 0);
		}

		// If asset uploads were flushed on the dedicated transfer queue, make this
		// queue wait for that copy to complete before it reads the uploaded mesh/
		// texture data. No-op in graphics-queue fallback mode (nothing pending).
		uint64_t uploadInstance = 0;
		if (Renderer::ConsumePendingUpload(m_Queue, uploadInstance))
			Renderer::QueueWaitForCommandList(m_Queue, nvrhi::CommandQueue::Copy, uploadInstance);

		// Execute on this buffer's queue (Graphics unless this is a compute
		// command buffer) and keep the returned instance id so another queue can
		// wait on it via Renderer::QueueWaitForCommandList.
		m_LastExecutionInstance = device->executeCommandList(m_CommandLists[commandBufferIndex], m_Queue);
		UnlockQueue();

#ifdef CMD_BUFFER_USE_VULKAN_QUERIES
		if (m_QueryEnabled)
		{
			vk::Device vkdevice = vk::Device(device->getNativeObject(nvrhi::ObjectTypes::VK_Device));

			PipelineStatistics resolved = {};
			const vk::Result queryResult = vkdevice.getQueryPoolResults(m_PipelineStatisticsQueryPools[commandBufferIndex], 0, 1,
				sizeof(PipelineStatistics), static_cast<void*>(&resolved), vk::DeviceSize(sizeof(uint64_t)), vk::QueryResultFlagBits::e64);

			// eNotReady is routine - the query simply has not landed yet - and Vulkan
			// leaves the destination untouched in that case. Publishing it anyway would
			// overwrite a good sample with zeros, so keep the previous frame's counters.
			if (queryResult == vk::Result::eSuccess)
				m_LastPipelineStatistics.store(resolved, std::memory_order_relaxed);
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
		if (Renderer::SupportsVariableRateShading() && m_GraphicsState.pipeline)
		{
			m_GraphicsState.shadingRateState.enabled = true;
			if (m_GraphicsState.shadingRateState.pipelinePrimitiveCombiner == nvrhi::ShadingRateCombiner::Passthrough)
				m_GraphicsState.shadingRateState.pipelinePrimitiveCombiner = nvrhi::ShadingRateCombiner::Override;
			if (m_GraphicsState.shadingRateState.imageCombiner == nvrhi::ShadingRateCombiner::Passthrough)
				m_GraphicsState.shadingRateState.imageCombiner = nvrhi::ShadingRateCombiner::Override;
		}
		m_ActiveCommandBuffer->setGraphicsState(m_GraphicsState);
	}

	void RenderCommandBuffer::RT_CommitComputeState()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_ActiveCommandBuffer->setComputeState(m_ComputeState);
	}

	float RenderCommandBuffer::GetExecutionGPUTime() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_QueryEnabled)
			return 0.0f;

		return m_LastGPUWorkTime.load(std::memory_order_relaxed);
	}

	Lux::PipelineStatistics RenderCommandBuffer::GetPipelineStatistics() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_LastPipelineStatistics.load(std::memory_order_relaxed);
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
