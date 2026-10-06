// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "RenderCommandBuffer.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/NVRHIInterop.h"
#include "Lux/Platform/Vulkan/VulkanDeviceManager.h"
#include "Lux/Platform/Vulkan/Debug/Aftermath.h"
#include "Lux/Platform/Vulkan/VulkanSwapChain.h"

#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace Lux {

	static std::mutex s_GraphicsQueueMutex;

	// Exponential smoothing weight for the newest GPU timer sample, so the profiling
	// panels do not flicker on single-frame outliers. Matches the weight already used
	// for the named per-pass queries below, which keeps the frame-level total directly
	// comparable to the sum of the per-pass timings displayed next to it.
	static constexpr float kGPUTimeSmoothing = 0.1f;

	namespace {

		bool HasUAVBindings(const nvrhi::BindingSetDesc& desc)
		{
			for (const nvrhi::BindingSetItem& item : desc.bindings)
			{
				switch (item.type)
				{
					case nvrhi::ResourceType::Texture_UAV:
					case nvrhi::ResourceType::TypedBuffer_UAV:
					case nvrhi::ResourceType::StructuredBuffer_UAV:
					case nvrhi::ResourceType::RawBuffer_UAV:
					case nvrhi::ResourceType::SamplerFeedbackTexture_UAV:
						return true;
					default:
						break;
				}
			}
			return false;
		}

		bool SameVertexBuffers(const nvrhi::GraphicsState& lhs, const nvrhi::GraphicsState& rhs)
		{
			if (lhs.vertexBuffers.size() != rhs.vertexBuffers.size())
				return false;
			for (size_t i = 0; i < lhs.vertexBuffers.size(); i++)
			{
				if (lhs.vertexBuffers[i].buffer != rhs.vertexBuffers[i].buffer)
					return false;
			}
			return true;
		}

	}

#if LUX_ENABLE_PROFILING
	// Null until the device manager has built the context, and permanently null if that
	// failed - every caller must tolerate it rather than assume GPU zones are available.
	static TracyVkCtx GetTracyGPUContext()
	{
		auto* deviceManager = static_cast<VulkanDeviceManager*>(Application::Get().GetGraphicsDeviceManager());
		return deviceManager ? deviceManager->GetGPUProfilerContext() : nullptr;
	}
#endif

	RenderCommandBuffer::RenderCommandBuffer(uint32_t count, bool enableQueries, const std::string& debugName, GPUQueue queue)
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
		clParams.setQueueType(ToNVRHI(m_Queue));

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
				m_TimerQueries.push_back({ device->createTimerQuery() });
				m_TimerSegmentCounts.push_back(0);

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

	void RenderCommandBuffer::Begin(bool continueFrame)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Ref<RenderCommandBuffer> instance = this;
		Renderer::Submit([instance, continueFrame]() mutable {
			instance->RT_Begin(continueFrame);
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

	void RenderCommandBuffer::RT_Begin(bool continueFrame)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		uint32_t commandBufferIndex = Renderer::RT_GetCurrentFrameIndex();
		commandBufferIndex %= m_CommandLists.size();

		m_ActiveCommandBuffer = m_CommandLists[commandBufferIndex];

		m_ActiveCommandBuffer->open();

		// Read once per command buffer; the setting itself is latched once per frame.
		m_ExplicitBarriers = !m_AutomaticBarriersOnly && Renderer::RT_ExplicitBarriersEnabled();
		m_ActiveCommandBuffer->setEnableAutomaticBarriers(!m_ExplicitBarriers);
		m_BarrierEmitter.SetCommandList(m_ActiveCommandBuffer);
		m_Tracker.Begin(&m_BarrierEmitter);
		RT_ForgetCommittedState();

		auto device = Application::GetGraphicsDevice();

		if (m_QueryEnabled)
		{
			std::vector<nvrhi::TimerQueryHandle>& segmentQueries = m_TimerQueries[commandBufferIndex];
			uint32_t& segmentCount = m_TimerSegmentCounts[commandBufferIndex];

			// A new frame on this index: publish the previous frame's total (every segment it
			// recorded) BEFORE the queries are reset for reuse. If any segment is not ready yet
			// the previous value stays in place; the panels then show the last resolved frame
			// rather than a partial sum.
			bool frameResolved = !continueFrame && segmentCount > 0;
			float frameTimeInMs = 0.0f;
			for (uint32_t segment = 0; frameResolved && segment < segmentCount; segment++)
			{
				if (device->pollTimerQuery(segmentQueries[segment]))
					frameTimeInMs += device->getTimerQueryTime(segmentQueries[segment]) * 1000.0f;
				else
					frameResolved = false;
			}

			if (frameResolved)
			{
				const float previous = m_LastGPUWorkTime.load(std::memory_order_relaxed);

				// Seed on the first resolved sample, otherwise the average crawls up from
				// zero over dozens of frames and reads as a bogus sub-millisecond frame.
				const float smoothed = previous > 0.0f
					? frameTimeInMs * kGPUTimeSmoothing + previous * (1.0f - kGPUTimeSmoothing)
					: frameTimeInMs;

				m_LastGPUWorkTime.store(smoothed, std::memory_order_relaxed);
			}

			if (!continueFrame)
				segmentCount = 0;
			if (segmentCount == segmentQueries.size())
				segmentQueries.push_back(device->createTimerQuery());
			m_ActiveTimerQuery = segmentQueries[segmentCount++];

			// Reset and begin this segment's timer query
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

#if LUX_ENABLE_PROFILING
			// Any zone still open at the end of the buffer is a begin/end mismatch. It has
			// to be closed HERE, while the command buffer is still recording: ~VkCtxScope
			// issues a vkCmdWriteTimestamp, which is illegal once close() has run.
			m_TracyGPUZones.clear();

			// Tracy resolves its timestamps here, once per submitted segment of this
			// buffer. Must be outside a render pass, which the endTimerQuery above
			// guarantees, and must precede close().
			if (TracyVkCtx tracyContext = GetTracyGPUContext())
			{
				VkCommandBuffer vkCommandBuffer = VkCommandBuffer(m_ActiveCommandBuffer->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer));
				TracyVkCollect(tracyContext, vkCommandBuffer);
			}
#endif

			m_ActiveTimerQuery = nullptr;
		}

		// NVRHI's close() would restore keepInitialState resources too; the tracker does it so its
		// model ends where the next command buffer starts, which the NRI emitter will rely on.
		if (m_ExplicitBarriers)
			m_Tracker.End();

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
				// The matching Tracy zones were already closed in RT_End, which is the last
				// point at which the command buffer was still recording. Nothing to do here.
			}
		}

		// TODO(Yan): fences

		uint32_t commandBufferIndex = Renderer::RT_GetCurrentFrameIndex();
		commandBufferIndex %= m_CommandLists.size();

		LockQueue();
		if (waitSemaphore)
		{
			auto vulkanDevice = static_cast<nvrhi::vulkan::IDevice*>(device);
			vulkanDevice->queueWaitForSemaphore(ToNVRHI(m_Queue), waitSemaphore, 0);
		}

		// If asset uploads were flushed on the dedicated transfer queue, make this
		// queue wait for that copy to complete before it reads the uploaded mesh/
		// texture data. No-op in graphics-queue fallback mode (nothing pending).
		uint64_t uploadInstance = 0;
		if (Renderer::ConsumePendingUpload(m_Queue, uploadInstance))
			Renderer::QueueWaitForCommandList(m_Queue, GPUQueue::Copy, uploadInstance);

		// Execute on this buffer's queue (Graphics unless this is a compute
		// command buffer) and keep the returned instance id so another queue can
		// wait on it via Renderer::QueueWaitForCommandList.
		m_LastExecutionInstance = device->executeCommandList(m_CommandLists[commandBufferIndex], ToNVRHI(m_Queue));
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
		auto device = static_cast<nvrhi::vulkan::IDevice*>(Application::GetGraphicsDevice());
		LockQueue();
		device->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, waitSemaphore, 0);
		UnlockQueue();
	}

	void RenderCommandBuffer::RT_BeginMarker(const std::string& label)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		nvrhi::CommandListHandle commandList = GetActive();
		commandList->beginMarker(label.c_str());
		if (Aftermath::IsEnabled())
			Aftermath::SetCheckpoint(static_cast<VkCommandBuffer>(commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer)), label);
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

		if (m_ExplicitBarriers)
		{
			// The same requirements NVRHI's insertGraphicsResourceBarriers makes, under the same
			// change detection (see m_BindingStatesDirty).
			RT_RequireBindingSets(m_GraphicsState.bindings, m_CommittedGraphicsState.bindings);

			nvrhi::IBuffer* indexBuffer = m_GraphicsState.indexBuffer.buffer;
			if (indexBuffer && (m_BindingStatesDirty || indexBuffer != m_CommittedGraphicsState.indexBuffer.buffer))
				m_Tracker.Require(DescribeBuffer(indexBuffer), ResourceState::IndexBuffer);

			if (m_BindingStatesDirty || !SameVertexBuffers(m_GraphicsState, m_CommittedGraphicsState))
			{
				for (const nvrhi::VertexBufferBinding& vertexBuffer : m_GraphicsState.vertexBuffers)
					m_Tracker.Require(DescribeBuffer(vertexBuffer.buffer), ResourceState::VertexBuffer);
			}

			if (m_BindingStatesDirty || m_GraphicsState.framebuffer != m_CommittedGraphicsState.framebuffer)
				RT_RequireFramebuffer(m_GraphicsState.framebuffer);

			nvrhi::IBuffer* indirectParams = m_GraphicsState.indirectParams;
			if (indirectParams && (m_BindingStatesDirty || indirectParams != m_CommittedGraphicsState.indirectParams))
				m_Tracker.Require(DescribeBuffer(indirectParams), ResourceState::IndirectArgument);

			m_BindingStatesDirty = false;
			m_CommittedGraphicsState = m_GraphicsState;
			m_CommittedComputeState = {};
			m_CommittedMeshletState = {};
			RT_CrossCheckStates("graphics state");
		}

		m_ActiveCommandBuffer->setGraphicsState(m_GraphicsState);
	}

	void RenderCommandBuffer::RT_CommitComputeState()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_ExplicitBarriers)
		{
			RT_RequireBindingSets(m_ComputeState.bindings, m_CommittedComputeState.bindings);

			nvrhi::IBuffer* indirectParams = m_ComputeState.indirectParams;
			if (indirectParams && (m_BindingStatesDirty || indirectParams != m_CommittedComputeState.indirectParams))
				m_Tracker.Require(DescribeBuffer(indirectParams), ResourceState::IndirectArgument);

			m_BindingStatesDirty = false;
			m_CommittedComputeState = m_ComputeState;
			m_CommittedGraphicsState = {};
			m_CommittedMeshletState = {};
			RT_CrossCheckStates("compute state");
		}

		m_ActiveCommandBuffer->setComputeState(m_ComputeState);
	}

	void RenderCommandBuffer::RT_CommitMeshletState(const nvrhi::MeshletState& meshletState)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_ExplicitBarriers)
		{
			RT_RequireBindingSets(meshletState.bindings, m_CommittedMeshletState.bindings);

			if (m_BindingStatesDirty || meshletState.framebuffer != m_CommittedMeshletState.framebuffer)
				RT_RequireFramebuffer(meshletState.framebuffer);

			nvrhi::IBuffer* indirectParams = meshletState.indirectParams;
			if (indirectParams && (m_BindingStatesDirty || indirectParams != m_CommittedMeshletState.indirectParams))
				m_Tracker.Require(DescribeBuffer(indirectParams), ResourceState::IndirectArgument);

			m_BindingStatesDirty = false;
			m_CommittedMeshletState = meshletState;
			m_CommittedGraphicsState = {};
			m_CommittedComputeState = {};
			RT_CrossCheckStates("meshlet state");
		}

		m_ActiveCommandBuffer->setMeshletState(meshletState);
	}

	void RenderCommandBuffer::RT_RequireBindingSets(const nvrhi::BindingSetVector& bindings, const nvrhi::BindingSetVector& committed)
	{
		for (size_t i = 0; i < bindings.size(); i++)
		{
			nvrhi::IBindingSet* bindingSet = bindings[i];
			const nvrhi::BindingSetDesc* desc = bindingSet ? bindingSet->getDesc() : nullptr;
			// Bindless descriptor tables have no desc: untracked, as in NVRHI. Their textures are in
			// their resting ShaderResource state whenever a command buffer starts or ends.
			if (!desc)
				continue;

			const bool changed = m_BindingStatesDirty || i >= committed.size() || committed[i] != bindingSet;
			if (!changed && !HasUAVBindings(*desc))
				continue;

			for (const nvrhi::BindingSetItem& item : desc->bindings)
			{
				switch (item.type)
				{
					case nvrhi::ResourceType::Texture_SRV:
						m_Tracker.Require(DescribeTexture(static_cast<nvrhi::ITexture*>(item.resourceHandle)), FromNVRHI(item.subresources), ResourceState::ShaderResource);
						break;
					case nvrhi::ResourceType::Texture_UAV:
						m_Tracker.Require(DescribeTexture(static_cast<nvrhi::ITexture*>(item.resourceHandle)), FromNVRHI(item.subresources), ResourceState::UnorderedAccess);
						break;
					case nvrhi::ResourceType::TypedBuffer_SRV:
					case nvrhi::ResourceType::StructuredBuffer_SRV:
					case nvrhi::ResourceType::RawBuffer_SRV:
						m_Tracker.Require(DescribeBuffer(static_cast<nvrhi::IBuffer*>(item.resourceHandle)), ResourceState::ShaderResource);
						break;
					case nvrhi::ResourceType::TypedBuffer_UAV:
					case nvrhi::ResourceType::StructuredBuffer_UAV:
					case nvrhi::ResourceType::RawBuffer_UAV:
						m_Tracker.Require(DescribeBuffer(static_cast<nvrhi::IBuffer*>(item.resourceHandle)), ResourceState::UnorderedAccess);
						break;
					case nvrhi::ResourceType::ConstantBuffer:
						m_Tracker.Require(DescribeBuffer(static_cast<nvrhi::IBuffer*>(item.resourceHandle)), ResourceState::ConstantBuffer);
						break;
					default:
						break;
				}
			}
		}
	}

	// NVRHI's ICommandList::setResourceStatesForFramebuffer, through the tracker.
	void RenderCommandBuffer::RT_RequireFramebuffer(nvrhi::IFramebuffer* framebuffer)
	{
		if (!framebuffer)
			return;

		const nvrhi::FramebufferDesc& desc = framebuffer->getDesc();
		for (const nvrhi::FramebufferAttachment& attachment : desc.colorAttachments)
			m_Tracker.Require(DescribeTexture(attachment.texture), FromNVRHI(attachment.subresources), ResourceState::RenderTarget);

		if (desc.depthAttachment.valid())
		{
			m_Tracker.Require(DescribeTexture(desc.depthAttachment.texture), FromNVRHI(desc.depthAttachment.subresources),
				desc.depthAttachment.isReadOnly ? ResourceState::DepthRead : ResourceState::DepthWrite);
		}

		if (desc.shadingRateAttachment.valid())
			m_Tracker.Require(DescribeTexture(desc.shadingRateAttachment.texture), FromNVRHI(desc.shadingRateAttachment.subresources), ResourceState::ShadingRateSurface);
	}

	void RenderCommandBuffer::RT_ForgetCommittedState()
	{
		m_BindingStatesDirty = true;
		m_CommittedGraphicsState = {};
		m_CommittedComputeState = {};
		m_CommittedMeshletState = {};
	}

	void RenderCommandBuffer::RT_CrossCheckStates(const char* context)
	{
#ifdef LUX_DEBUG
		m_Tracker.CrossCheck(context);
#endif
	}

	void RenderCommandBuffer::RT_RequireTextureState(nvrhi::ITexture* texture, const TextureSubresourceRange& range, ResourceState state)
	{
		if (!m_ExplicitBarriers)
			return;
		m_Tracker.Require(DescribeTexture(texture), range, state);
		m_BindingStatesDirty = true;
	}

	void RenderCommandBuffer::RT_RequireBufferState(nvrhi::IBuffer* buffer, ResourceState state)
	{
		if (!m_ExplicitBarriers)
			return;
		m_Tracker.Require(DescribeBuffer(buffer), state);
		m_BindingStatesDirty = true;
	}

	// Unlike RT_Require*, a transition does not mark bound sets dirty: NVRHI's setTextureState does
	// not either (only copies, clears and writes do), and explicit mode mirrors automatic mode.
	void RenderCommandBuffer::RT_TransitionTextureState(nvrhi::ITexture* texture, const TextureSubresourceRange& range, ResourceState state)
	{
		m_Tracker.Require(DescribeTexture(texture), range, state);
	}

	void RenderCommandBuffer::RT_TransitionBufferState(nvrhi::IBuffer* buffer, ResourceState state)
	{
		m_Tracker.Require(DescribeBuffer(buffer), state);
	}

	void RenderCommandBuffer::RT_RequireColorAttachmentClear(nvrhi::IFramebuffer* framebuffer, uint32_t attachmentIndex)
	{
		if (!m_ExplicitBarriers || !framebuffer || attachmentIndex >= framebuffer->getDesc().colorAttachments.size())
			return;
		const nvrhi::FramebufferAttachment& attachment = framebuffer->getDesc().colorAttachments[attachmentIndex];
		RT_RequireTextureState(attachment.texture, FromNVRHI(attachment.subresources), ResourceState::CopyDest);
	}

	void RenderCommandBuffer::RT_RequireDepthAttachmentClear(nvrhi::IFramebuffer* framebuffer)
	{
		if (!m_ExplicitBarriers || !framebuffer || !framebuffer->getDesc().depthAttachment.valid())
			return;
		const nvrhi::FramebufferAttachment& attachment = framebuffer->getDesc().depthAttachment;
		RT_RequireTextureState(attachment.texture, FromNVRHI(attachment.subresources), ResourceState::CopyDest);
	}

	void RenderCommandBuffer::RT_BeginRequirementLog()
	{
		m_RequirementLog.clear();
		m_Tracker.SetRequirementLog(&m_RequirementLog);
	}

	std::vector<const void*> RenderCommandBuffer::RT_EndRequirementLog()
	{
		m_Tracker.SetRequirementLog(nullptr);
		return std::move(m_RequirementLog);
	}

	void RenderCommandBuffer::RT_CommitBarriers()
	{
		m_Tracker.Commit();
		if (m_ExplicitBarriers)
			RT_CrossCheckStates("explicit barriers");
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

#if LUX_ENABLE_PROFILING
		// Opened after nvrhi's beginTimerQuery (which closes any active render pass) so
		// the Tracy zone and the engine's own timer bracket the same span.
		if (TracyVkCtx tracyContext = GetTracyGPUContext())
		{
			VkCommandBuffer vkCommandBuffer = VkCommandBuffer(m_ActiveCommandBuffer->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer));
			m_TracyGPUZones.push_back(CreateScope<tracy::VkCtxScope>(
				tracyContext,
				uint32_t(__LINE__), __FILE__, std::strlen(__FILE__),
				m_DebugName.c_str(), m_DebugName.size(),
				name.c_str(), name.size(),
				vkCommandBuffer, true));
		}
#endif

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

#if LUX_ENABLE_PROFILING
		// Popped alongside the name rather than after the lookup below, so a missing
		// query cannot leave a Tracy zone open and desynchronise the two stacks.
		// Destroying the scope is what writes the closing timestamp.
		if (!m_TracyGPUZones.empty())
			m_TracyGPUZones.pop_back();
#endif

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
