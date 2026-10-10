// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "RenderCommandBuffer.h"

#include "Lux/Core/Hash.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/DescriptorSetGroup.h"
#include "Lux/Renderer/RHI/NVRHIInterop.h"
#include "Lux/Renderer/RHI/NVRHIWrappers.h"
#include "Lux/Platform/Vulkan/VulkanDeviceManager.h"
#include "Lux/Platform/Vulkan/Debug/Aftermath.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
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

		// Root constants up to this size are compared with the last ones set, so repeats are skipped.
		constexpr uint32_t k_NRIRootConstantCacheSize = 256;

#ifdef LUX_DEBUG
		// The inside-rendering audit (NRI migration Phase 10): pass and reason of every NRI render pass
		// split already reported. Render thread.
		std::unordered_set<uint64_t> s_ReportedNRISplits;
#endif

		// NVRHI's viewport, as NRI's default top-left origin flips it: the same negative-height
		// VkViewport NVRHI records (VKViewportWithDXCoords), so the Y convention is unchanged.
		nri::Viewport ToNRIViewport(const nvrhi::Viewport& viewport)
		{
			return { viewport.minX, viewport.minY, viewport.maxX - viewport.minX, viewport.maxY - viewport.minY, viewport.minZ, viewport.maxZ, false };
		}

		nri::Rect ToNRIRect(const nvrhi::Rect& rect)
		{
			return { static_cast<int16_t>(rect.minX), static_cast<int16_t>(rect.minY),
				static_cast<nri::Dim_t>(std::abs(rect.maxX - rect.minX)), static_cast<nri::Dim_t>(std::abs(rect.maxY - rect.minY)) };
		}

		nri::ShadingRate ToNRIShadingRate(nvrhi::VariableShadingRate rate)
		{
			switch (rate)
			{
				case nvrhi::VariableShadingRate::e1x2:	return nri::ShadingRate::FRAGMENT_SIZE_1X2;
				case nvrhi::VariableShadingRate::e2x1:	return nri::ShadingRate::FRAGMENT_SIZE_2X1;
				case nvrhi::VariableShadingRate::e2x2:	return nri::ShadingRate::FRAGMENT_SIZE_2X2;
				case nvrhi::VariableShadingRate::e2x4:	return nri::ShadingRate::FRAGMENT_SIZE_2X4;
				case nvrhi::VariableShadingRate::e4x2:	return nri::ShadingRate::FRAGMENT_SIZE_4X2;
				case nvrhi::VariableShadingRate::e4x4:	return nri::ShadingRate::FRAGMENT_SIZE_4X4;
				default:								return nri::ShadingRate::FRAGMENT_SIZE_1X1;
			}
		}

		// NRI accepts combiners other than KEEP from shading-rate tier 2 on.
		nri::ShadingRateCombiner ToNRIShadingRateCombiner(nvrhi::ShadingRateCombiner combiner)
		{
			if (RHIDevice::GetDesc().tiers.shadingRate < 2)
				return nri::ShadingRateCombiner::KEEP;

			switch (combiner)
			{
				case nvrhi::ShadingRateCombiner::Override:		return nri::ShadingRateCombiner::REPLACE;
				case nvrhi::ShadingRateCombiner::Min:			return nri::ShadingRateCombiner::MIN;
				case nvrhi::ShadingRateCombiner::Max:			return nri::ShadingRateCombiner::MAX;
				case nvrhi::ShadingRateCombiner::ApplyRelative:	return nri::ShadingRateCombiner::SUM;
				default:										return nri::ShadingRateCombiner::KEEP;
			}
		}

	}

	struct RenderCommandBuffer::NRIRenderState
	{
		bool Active = false;
		// Inside CmdBeginRendering; false while the pass is suspended for NVRHI.
		bool Rendering = false;
		std::string Name;
		uint32_t NameHash = 0;
		NRIRenderPassDesc Desc;
		// The groups of Desc.DescriptorSets: their uses are re-required inside the pass.
		std::array<Ref<const DescriptorSetGroup>, nvrhi::c_MaxBindingLayouts> PassGroups;
		nvrhi::static_vector<nri::Viewport, nvrhi::c_MaxViewports> Viewports;
		nvrhi::static_vector<nri::Rect, nvrhi::c_MaxViewports> Scissors;
		nri::ShadingRateDesc ShadingRate = {};

		// What the open rendering scope has bound; reset whenever a scope opens.
		std::array<nri::DescriptorSet*, nvrhi::c_MaxBindingLayouts> BoundSets = {};
		nri::Buffer* VertexBuffer = nullptr;
		uint32_t VertexStride = 0;
		nri::Buffer* IndexBuffer = nullptr;
		bool IndexBuffer16 = false;
		std::array<uint8_t, k_NRIRootConstantCacheSize> RootConstants = {};
		// 0 when nothing is cached.
		uint32_t RootConstantsSize = 0;
	};

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
		: m_Queue(queue), m_NRIRender(CreateScope<NRIRenderState>()), m_DebugName(debugName)
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

		m_ExplicitBarriers = m_BarrierMode == BarrierMode::Explicit;
		m_ActiveCommandBuffer->setEnableAutomaticBarriers(!m_ExplicitBarriers);
		m_BarrierEmitter.SetCommandList(m_ActiveCommandBuffer);
		m_Tracker.Begin(&m_BarrierEmitter);
		m_RequiredBindings = {};
		m_BindingStatesDirty = true;
		*m_NRIRender = {};

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
		if (m_NRIRender->Active)
		{
			LUX_CORE_ERROR_TAG("Renderer", "NRI render pass '{}' was still open at the end of {}", m_NRIRender->Name, m_DebugName);
			RT_EndNRIRenderPass();
		}

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

		LUX_CORE_ASSERT(!m_InNRISegment, "RT_End inside an NRI segment ({})", m_DebugName);
		DestroyNRIWrapper();

		m_ActiveCommandBuffer->close();

		m_ActiveCommandBuffer = nullptr;

	}

	RenderCommandBuffer::~RenderCommandBuffer()
	{
		DestroyNRIWrapper();
	}

	nri::CommandBuffer* RenderCommandBuffer::RT_BeginNRISegment()
	{
		LUX_CORE_ASSERT(m_ActiveCommandBuffer, "RT_BeginNRISegment outside RT_Begin/RT_End ({})", m_DebugName);
		LUX_CORE_ASSERT(!m_InNRISegment, "Nested NRI segment ({})", m_DebugName);

		if (!m_NRICommandBuffer)
		{
			// Non-owning: NRI never frees a command buffer it did not allocate.
			nri::CommandBufferVKDesc desc = {};
			desc.vkCommandBuffer = m_ActiveCommandBuffer->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);
			desc.queueType = ToNRIQueueType(m_Queue);
			if (RHIDevice::API().CreateCommandBufferVK(RHIDevice::Get(), desc, m_NRICommandBuffer) != nri::Result::SUCCESS)
			{
				LUX_CORE_ERROR_TAG("Renderer", "Failed to wrap command buffer {} in NRI", m_DebugName);
				m_NRICommandBuffer = nullptr;
				return nullptr;
			}
		}

		// Committing through the tracker clears its pending flag too. NVRHI draws and dispatches
		// nothing, so it never has a rendering scope or bound state of its own to clear.
		m_Tracker.Commit();
		m_InNRISegment = true;
		return m_NRICommandBuffer;
	}

	void RenderCommandBuffer::RT_EndNRISegment()
	{
		LUX_CORE_ASSERT(m_InNRISegment, "RT_EndNRISegment without RT_BeginNRISegment ({})", m_DebugName);
		m_InNRISegment = false;
	}

	void RenderCommandBuffer::DestroyNRIWrapper()
	{
		if (!m_NRICommandBuffer)
			return;

		// A wrapper only describes the VkCommandBuffer; nothing on the GPU refers to it.
		RHIDevice::API().DestroyCommandBuffer(m_NRICommandBuffer);
		m_NRICommandBuffer = nullptr;
	}

	nvrhi::CommandListHandle RenderCommandBuffer::GetActive()
	{
		RT_SuspendNRIRendering("an NVRHI command");
		return m_ActiveCommandBuffer;
	}

	bool RenderCommandBuffer::RT_BeginNRIRenderPass(const NRIRenderPassDesc& desc, std::string_view name, const nvrhi::ViewportState& viewport, const nvrhi::VariableRateShadingState& shadingRate)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		NRIRenderState& state = *m_NRIRender;
		LUX_CORE_ASSERT(!state.Active, "NRI render pass '{}' begun inside '{}' ({})", name, state.Name, m_DebugName);
		LUX_CORE_ASSERT(m_ExplicitBarriers, "NRI render passes take their barriers from the tracker ({})", m_DebugName);
		LUX_CORE_ASSERT(desc.PipelineLayout && desc.Pipeline);

		state = {};
		state.Active = true;
		state.Name = name;
		state.NameHash = Hash::GenerateFNVHash(name);
		state.Desc = desc;
		for (size_t i = 0; i < desc.DescriptorSets.size(); i++)
			state.PassGroups[i] = desc.DescriptorSets[i].Group;
		RT_SetNRIViewportState(viewport);
		RT_SetNRIShadingRate(shadingRate);

		// The attachments are required whenever a pass opens; the sets under the draws' change
		// detection, so a draw that binds the pass's own set 0 again requires nothing. The buffers
		// are forgotten, as NVRHI's pass began without any.
		if (m_ExplicitBarriers)
		{
			RT_BeginRequirements(RequiredBindPoint::Graphics);
			m_RequiredBindings.VertexBuffer = nullptr;
			m_RequiredBindings.IndexBuffer = nullptr;
			m_RequiredBindings.IndirectArguments = nullptr;
			RT_RequireNRIRenderPass();
			RT_CrossCheckStates("NRI render pass");
		}

		if (!RT_OpenNRIRendering(true))
		{
			state = {};
			return false;
		}
		return true;
	}

	void RenderCommandBuffer::RT_EndNRIRenderPass()
	{
		if (!m_NRIRender->Active)
			return;

		RT_CloseNRIRendering();
		*m_NRIRender = {};
	}

	bool RenderCommandBuffer::RT_InNRIRenderPass() const
	{
		return m_NRIRender->Active;
	}

	const RenderCommandBuffer::NRIRenderPassDesc& RenderCommandBuffer::RT_GetNRIRenderPass() const
	{
		return m_NRIRender->Desc;
	}

	std::string_view RenderCommandBuffer::RT_GetNRIRenderPassName() const
	{
		return m_NRIRender->Name;
	}

	nri::CommandBuffer* RenderCommandBuffer::RT_OpenNRIRendering(bool passStart)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		NRIRenderState& state = *m_NRIRender;
		nri::CommandBuffer* commandBuffer = RT_BeginNRISegment();
		if (!commandBuffer)
			return nullptr;

		const NRIRenderPassDesc& desc = state.Desc;
		std::array<nri::AttachmentDesc, nvrhi::c_MaxRenderTargets> colors = {};
		for (uint32_t i = 0; i < desc.ColorAttachmentCount; i++)
		{
			const std::array<float, 4>& clear = desc.ClearColorValues[i];
			nri::AttachmentDesc& color = colors[i];
			color.descriptor = desc.ColorAttachments[i].View;
			color.clearValue.color.f = { clear[0], clear[1], clear[2], clear[3] };
			color.loadOp = passStart && desc.ClearColor[i] ? nri::LoadOp::CLEAR : nri::LoadOp::LOAD;
			color.storeOp = nri::StoreOp::STORE;
		}

		nri::RenderingDesc renderingDesc = {};
		renderingDesc.colors = desc.ColorAttachmentCount ? colors.data() : nullptr;
		renderingDesc.colorNum = desc.ColorAttachmentCount;
		renderingDesc.depth.descriptor = desc.DepthAttachment.View;
		renderingDesc.depth.clearValue.depthStencil = { desc.ClearDepthValue, 0 };
		renderingDesc.depth.loadOp = passStart && desc.ClearDepth ? nri::LoadOp::CLEAR : nri::LoadOp::LOAD;
		renderingDesc.depth.storeOp = nri::StoreOp::STORE;

		const NRIInterface& api = RHIDevice::API();
		api.CmdBeginRendering(*commandBuffer, renderingDesc);
		state.Rendering = true;

		// The pass state is rebound in every scope: NVRHI may have bound its own in between.
		if (!state.Viewports.empty())
			api.CmdSetViewports(*commandBuffer, state.Viewports.data(), static_cast<uint32_t>(state.Viewports.size()));
		if (!state.Scissors.empty())
			api.CmdSetScissors(*commandBuffer, state.Scissors.data(), static_cast<uint32_t>(state.Scissors.size()));
		api.CmdSetPipelineLayout(*commandBuffer, nri::BindPoint::GRAPHICS, *desc.PipelineLayout);
		api.CmdSetPipeline(*commandBuffer, *desc.Pipeline);
		// The one raw Vulkan call in the renderer: NRI has no line-width state, and the fork's
		// LUX-1 patch makes it dynamic on line pipelines (CmdSetPipeline just set 1).
		if (desc.LineWidth > 0.0f)
			vk::CommandBuffer(static_cast<VkCommandBuffer>(api.GetCommandBufferNativeObject(commandBuffer))).setLineWidth(desc.LineWidth);
		if (desc.DynamicShadingRate)
			api.CmdSetShadingRate(*commandBuffer, state.ShadingRate);

		state.BoundSets = {};
		state.VertexBuffer = nullptr;
		state.VertexStride = 0;
		state.IndexBuffer = nullptr;
		state.RootConstantsSize = 0;
		for (uint32_t setIndex = 0; setIndex < desc.DescriptorSets.size(); setIndex++)
		{
			if (desc.DescriptorSets[setIndex].Set)
				RT_SetNRIDescriptorSet(setIndex, desc.DescriptorSets[setIndex].Set);
		}
		return commandBuffer;
	}

	void RenderCommandBuffer::RT_CloseNRIRendering()
	{
		NRIRenderState& state = *m_NRIRender;
		if (!state.Rendering)
			return;

		RHIDevice::API().CmdEndRendering(*m_NRICommandBuffer);
		state.Rendering = false;
		RT_EndNRISegment();
	}

	void RenderCommandBuffer::RT_SuspendNRIRendering(const char* reason)
	{
		if (!m_NRIRender->Rendering)
			return;

		RT_CloseNRIRendering();

#ifdef LUX_DEBUG
		// Each split stores the attachments and loads them again; report where passes split, once
		// per pass and reason (reasons are literals, so their addresses identify them).
		const uint64_t key = (static_cast<uint64_t>(m_NRIRender->NameHash) << 32) ^ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(reason));
		if (s_ReportedNRISplits.insert(key).second)
			LUX_CORE_WARN_TAG("Renderer", "NRI render pass '{}' ({}) is split by {} inside it", m_NRIRender->Name, m_DebugName, reason);
#else
		(void)reason;
#endif
	}

	void RenderCommandBuffer::RT_SetNRIViewportState(const nvrhi::ViewportState& viewport)
	{
		NRIRenderState& state = *m_NRIRender;
		state.Viewports.resize(0);
		for (const nvrhi::Viewport& source : viewport.viewports)
			state.Viewports.push_back(ToNRIViewport(source));
		state.Scissors.resize(0);
		for (const nvrhi::Rect& source : viewport.scissorRects)
			state.Scissors.push_back(ToNRIRect(source));

		if (!state.Rendering)
			return;

		const NRIInterface& api = RHIDevice::API();
		if (!state.Viewports.empty())
			api.CmdSetViewports(*m_NRICommandBuffer, state.Viewports.data(), static_cast<uint32_t>(state.Viewports.size()));
		if (!state.Scissors.empty())
			api.CmdSetScissors(*m_NRICommandBuffer, state.Scissors.data(), static_cast<uint32_t>(state.Scissors.size()));
	}

	void RenderCommandBuffer::RT_SetNRIViewport(const nvrhi::Viewport& viewport)
	{
		NRIRenderState& state = *m_NRIRender;
		state.Viewports.resize(1);
		state.Viewports[0] = ToNRIViewport(viewport);
		if (state.Rendering)
			RHIDevice::API().CmdSetViewports(*m_NRICommandBuffer, state.Viewports.data(), 1);
	}

	void RenderCommandBuffer::RT_SetNRIScissor(const nvrhi::Rect& scissor)
	{
		NRIRenderState& state = *m_NRIRender;
		state.Scissors.resize(1);
		state.Scissors[0] = ToNRIRect(scissor);
		if (state.Rendering)
			RHIDevice::API().CmdSetScissors(*m_NRICommandBuffer, state.Scissors.data(), 1);
	}

	void RenderCommandBuffer::RT_SetNRIShadingRate(const nvrhi::VariableRateShadingState& shadingRate)
	{
		NRIRenderState& state = *m_NRIRender;
		state.ShadingRate.shadingRate = ToNRIShadingRate(shadingRate.shadingRate);
		state.ShadingRate.primitiveCombiner = ToNRIShadingRateCombiner(shadingRate.pipelinePrimitiveCombiner);
		state.ShadingRate.attachmentCombiner = ToNRIShadingRateCombiner(shadingRate.imageCombiner);

		if (state.Rendering && state.Desc.DynamicShadingRate)
			RHIDevice::API().CmdSetShadingRate(*m_NRICommandBuffer, state.ShadingRate);
	}

	nri::CommandBuffer* RenderCommandBuffer::RT_BeginNRIDraw(const NRIDrawBindings& bindings)
	{
		NRIRenderState& state = *m_NRIRender;
		if (!state.Active)
			return nullptr;

		const NRIRenderPassDesc& pass = state.Desc;
		const bool hasDrawSet = pass.DrawSetIndex < pass.DescriptorSets.size();
		const BoundDescriptorSet& drawSet = !hasDrawSet ? bindings.DrawSet
			: bindings.DrawSet.Set ? bindings.DrawSet : pass.DescriptorSets[pass.DrawSetIndex];

		if (m_ExplicitBarriers)
		{
			// Forgotten requirements include the pass's: NVRHI re-required its whole graphics state.
			if (RT_BeginRequirements(RequiredBindPoint::Graphics))
				RT_RequireNRIRenderPass();
			if (hasDrawSet)
				RT_RequireSet(pass.DrawSetIndex, drawSet);
			RT_RequireBuffer(m_RequiredBindings.VertexBuffer, bindings.VertexBuffer, ResourceState::VertexBuffer);
			RT_RequireBuffer(m_RequiredBindings.IndexBuffer, bindings.IndexBuffer, ResourceState::IndexBuffer);
			RT_RequireBuffer(m_RequiredBindings.IndirectArguments, bindings.IndirectArguments, ResourceState::IndirectArgument);
			RT_CrossCheckStates("NRI draw");
		}

		// Barriers cannot be recorded inside rendering: commit them between two scopes.
		if (m_Tracker.HasPendingBarriers())
		{
			RT_SuspendNRIRendering("a barrier");
			m_Tracker.Commit();
			RT_CrossCheckStates("NRI draw barriers");
		}

		nri::CommandBuffer* commandBuffer = state.Rendering ? m_NRICommandBuffer : RT_OpenNRIRendering(false);
		if (!commandBuffer)
			return nullptr;

		if (hasDrawSet && drawSet.Set)
			RT_SetNRIDescriptorSet(pass.DrawSetIndex, drawSet.Set);
		if (bindings.VertexBuffer)
			RT_SetNRIVertexBuffer(bindings.VertexBuffer->Get(), pass.VertexStride);
		if (bindings.IndexBuffer)
			RT_SetNRIIndexBuffer(bindings.IndexBuffer->Get(), bindings.IndexBuffer16);
		if (bindings.RootConstants && bindings.RootConstantsSize && pass.RootConstants)
			RT_SetNRIRootConstants(bindings.RootConstants, bindings.RootConstantsSize);
		return commandBuffer;
	}

	nri::CommandBuffer* RenderCommandBuffer::RT_BeginNRIDispatch(const NRIDispatchDesc& desc)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_ASSERT(m_ExplicitBarriers, "NRI dispatches take their barriers from the tracker ({})", m_DebugName);
		LUX_CORE_ASSERT(desc.PipelineLayout && desc.Pipeline);
		RT_SuspendNRIRendering("a compute dispatch");

		if (m_ExplicitBarriers)
		{
			RT_BeginRequirements(RequiredBindPoint::Compute);
			for (uint32_t setIndex = 0; setIndex < desc.DescriptorSets.size(); setIndex++)
				RT_RequireSet(setIndex, desc.DescriptorSets[setIndex]);
			RT_CrossCheckStates("NRI dispatch");
		}

		// The segment starts with the pending barriers committed.
		nri::CommandBuffer* commandBuffer = RT_BeginNRISegment();
		if (!commandBuffer)
			return nullptr;

		const NRIInterface& api = RHIDevice::API();
		api.CmdSetPipelineLayout(*commandBuffer, nri::BindPoint::COMPUTE, *desc.PipelineLayout);
		api.CmdSetPipeline(*commandBuffer, *desc.Pipeline);
		for (uint32_t setIndex = 0; setIndex < desc.DescriptorSets.size(); setIndex++)
		{
			if (!desc.DescriptorSets[setIndex].Set)
				continue;

			nri::SetDescriptorSetDesc setDesc = {};
			setDesc.setIndex = setIndex;
			setDesc.descriptorSet = desc.DescriptorSets[setIndex].Set;
			setDesc.bindPoint = nri::BindPoint::COMPUTE;
			api.CmdSetDescriptorSet(*commandBuffer, setDesc);
		}

		if (desc.RootConstants && desc.RootConstantsSize)
		{
			nri::SetRootConstantsDesc rootConstants = {};
			rootConstants.rootConstantIndex = 0;
			rootConstants.data = desc.RootConstants;
			rootConstants.size = desc.RootConstantsSize;
			rootConstants.bindPoint = nri::BindPoint::COMPUTE;
			api.CmdSetRootConstants(*commandBuffer, rootConstants);
		}
		return commandBuffer;
	}

	void RenderCommandBuffer::RT_SetNRIDescriptorSet(uint32_t setIndex, nri::DescriptorSet* descriptorSet)
	{
		NRIRenderState& state = *m_NRIRender;
		LUX_CORE_ASSERT(state.Rendering && setIndex < state.BoundSets.size());
		if (state.BoundSets[setIndex] == descriptorSet)
			return;

		state.BoundSets[setIndex] = descriptorSet;
		nri::SetDescriptorSetDesc setDesc = {};
		setDesc.setIndex = setIndex;
		setDesc.descriptorSet = descriptorSet;
		setDesc.bindPoint = nri::BindPoint::GRAPHICS;
		RHIDevice::API().CmdSetDescriptorSet(*m_NRICommandBuffer, setDesc);
	}

	void RenderCommandBuffer::RT_SetNRIVertexBuffer(nri::Buffer* buffer, uint32_t stride)
	{
		NRIRenderState& state = *m_NRIRender;
		LUX_CORE_ASSERT(state.Rendering && buffer);
		if (state.VertexBuffer == buffer && state.VertexStride == stride)
			return;

		state.VertexBuffer = buffer;
		state.VertexStride = stride;
		const nri::VertexBufferDesc vertexBuffer = { buffer, 0, stride };
		RHIDevice::API().CmdSetVertexBuffers(*m_NRICommandBuffer, 0, &vertexBuffer, 1);
	}

	void RenderCommandBuffer::RT_SetNRIIndexBuffer(nri::Buffer* buffer, bool use16BitIndices)
	{
		NRIRenderState& state = *m_NRIRender;
		LUX_CORE_ASSERT(state.Rendering && buffer);
		if (state.IndexBuffer == buffer && state.IndexBuffer16 == use16BitIndices)
			return;

		state.IndexBuffer = buffer;
		state.IndexBuffer16 = use16BitIndices;
		RHIDevice::API().CmdSetIndexBuffer(*m_NRICommandBuffer, *buffer, 0, use16BitIndices ? nri::IndexType::UINT16 : nri::IndexType::UINT32);
	}

	void RenderCommandBuffer::RT_SetNRIRootConstants(const void* data, uint32_t size)
	{
		NRIRenderState& state = *m_NRIRender;
		LUX_CORE_ASSERT(state.Rendering && data && size);
		if (size == state.RootConstantsSize && std::memcmp(state.RootConstants.data(), data, size) == 0)
			return;

		if (size <= state.RootConstants.size())
		{
			std::memcpy(state.RootConstants.data(), data, size);
			state.RootConstantsSize = size;
		}
		else
		{
			state.RootConstantsSize = 0;
		}

		nri::SetRootConstantsDesc rootConstants = {};
		rootConstants.rootConstantIndex = 0;
		rootConstants.data = data;
		rootConstants.size = size;
		rootConstants.bindPoint = nri::BindPoint::GRAPHICS;
		RHIDevice::API().CmdSetRootConstants(*m_NRICommandBuffer, rootConstants);
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

	// Labels and checkpoints are legal inside rendering, so markers do not close an NRI scope.
	void RenderCommandBuffer::RT_BeginMarker(const std::string& label)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_ActiveCommandBuffer->beginMarker(label.c_str());
		if (Aftermath::IsEnabled())
			Aftermath::SetCheckpoint(static_cast<VkCommandBuffer>(m_ActiveCommandBuffer->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer)), label);
	}

	void RenderCommandBuffer::RT_EndMarker()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_ActiveCommandBuffer->endMarker();
	}

	bool RenderCommandBuffer::RT_BeginRequirements(RequiredBindPoint bindPoint)
	{
		if (!m_BindingStatesDirty && m_RequiredBindings.BindPoint == bindPoint)
			return false;

		m_RequiredBindings = {};
		m_RequiredBindings.BindPoint = bindPoint;
		m_BindingStatesDirty = false;
		return true;
	}

	void RenderCommandBuffer::RT_RequireSet(uint32_t setIndex, const BoundDescriptorSet& set)
	{
		if (!set.Set || setIndex >= m_RequiredBindings.Sets.size())
			return;

		const DescriptorSetUses* uses = set.Group ? &set.Group->GetUses(set.Instance) : nullptr;
		if (m_RequiredBindings.Sets[setIndex] == set.Set && !(uses && uses->HasStorageUses))
			return;

		m_RequiredBindings.Sets[setIndex] = set.Set;
		if (!uses)
			return;

		for (const DescriptorSetUses::TextureUse& use : uses->Textures)
			m_Tracker.Require(use.Texture, use.Range, use.State);
		for (const DescriptorSetUses::BufferUse& use : uses->Buffers)
			m_Tracker.Require(use.Buffer, use.State);
	}

	void RenderCommandBuffer::RT_RequireBuffer(nvrhi::IBuffer*& required, const NRIBuffer* buffer, ResourceState state)
	{
		nvrhi::IBuffer* handle = buffer ? buffer->GetHandle().Get() : nullptr;
		if (!handle || handle == required)
			return;

		required = handle;
		m_Tracker.Require(DescribeBuffer(handle), state);
	}

	// NVRHI's ICommandList::setResourceStatesForFramebuffer and the pass's binding sets, through the
	// tracker.
	void RenderCommandBuffer::RT_RequireNRIRenderPass()
	{
		const NRIRenderPassDesc& desc = m_NRIRender->Desc;
		for (uint32_t i = 0; i < desc.ColorAttachmentCount; i++)
		{
			const NRIAttachment& attachment = desc.ColorAttachments[i];
			if (attachment.Texture)
				m_Tracker.Require(DescribeTexture(attachment.Texture), attachment.Range, ResourceState::RenderTarget);
		}
		if (desc.DepthAttachment.Texture)
			m_Tracker.Require(DescribeTexture(desc.DepthAttachment.Texture), desc.DepthAttachment.Range, ResourceState::DepthWrite);

		for (uint32_t setIndex = 0; setIndex < desc.DescriptorSets.size(); setIndex++)
			RT_RequireSet(setIndex, desc.DescriptorSets[setIndex]);
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
		if (m_Tracker.HasPendingBarriers())
			RT_SuspendNRIRendering("a barrier");
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

		// NVRHI resets the query pool here, which is illegal inside rendering.
		RT_SuspendNRIRendering("a timer query");
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

		RT_SuspendNRIRendering("a timer query");

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
