// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "RenderCommandBuffer.h"

#include "Lux/Core/Hash.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/DescriptorSetGroup.h"
#include "Lux/Renderer/RHI/RHIResources.h"
#include "Lux/Platform/Vulkan/VulkanDevice.h"
#include "Lux/Platform/Vulkan/Debug/Aftermath.h"
#include "Lux/Renderer/RHI/CommandBufferPool.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Renderer/RHI/RHIQueueTimeline.h"

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

	}

	struct RenderCommandBuffer::NRIRenderState
	{
		bool Active = false;
		// Inside CmdBeginRendering; false while the pass is suspended (a copy, clear or dispatch).
		bool Rendering = false;
		std::string Name;
		uint32_t NameHash = 0;
		NRIRenderPassDesc Desc;
		// The groups of Desc.DescriptorSets: their uses are re-required inside the pass.
		std::array<Ref<const DescriptorSetGroup>, MaxDescriptorSets> PassGroups;
		nri::Viewport Viewport = {};
		nri::Rect Scissor = {};
		nri::ShadingRateDesc ShadingRate = {};

		// What the open rendering scope has bound; reset whenever a scope opens.
		std::array<nri::DescriptorSet*, MaxDescriptorSets> BoundSets = {};
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
		auto* deviceManager = Application::Get().GetGraphicsDeviceManager();
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

		// A queue the device does not have records on graphics instead; waits on the missing queue
		// are then dropped (RHIQueueTimeline::AddWait), and graphics runs in order anyway.
		if (!RHIQueueTimeline::HasQueue(m_Queue))
		{
			LUX_CORE_WARN_TAG("Renderer", "{}: the device has no {} queue; recording on graphics", m_DebugName, static_cast<int>(m_Queue));
			m_Queue = GPUQueue::Graphics;
		}

		// One command buffer per slot to start with; the pool grows if they are all in flight.
		m_SlotCount = count;
		m_CommandBuffers = CreateScope<CommandBufferPool>(m_Queue, count, m_DebugName);

#ifdef LUX_DIST
		// GPU timer/statistics queries exist to feed the editor's profiling
		// panels; shipping builds skip the per-frame query begin/end/poll cost.
		enableQueries = false;
#endif
		// A slot's results are read when it is next begun, which frame pacing makes safe only for
		// one slot per frame in flight.
		if (enableQueries && count < Renderer::GetConfig().FramesInFlight)
		{
			LUX_CORE_WARN_TAG("Renderer", "{}: GPU queries need one command list per frame in flight; disabled", m_DebugName);
			enableQueries = false;
		}
		m_QueryEnabled = enableQueries && CreateQuerySlots(count);
	}

	bool RenderCommandBuffer::CreateQuerySlots(uint32_t count)
	{
		const NRIInterface& api = RHIDevice::API();
		const nri::DeviceDesc& deviceDesc = RHIDevice::GetDesc();
		if (!deviceDesc.features.timestamp)
		{
			LUX_CORE_WARN_TAG("Renderer", "{}: the device has no timestamp queries; GPU timing disabled", m_DebugName);
			return false;
		}

		// Pipeline statistics count graphics work, so only graphics queues take them.
		const bool statistics = deviceDesc.features.pipelineStatistics && m_Queue == GPUQueue::Graphics;
		m_QuerySlots.resize(count);
		for (QuerySlot& slot : m_QuerySlots)
		{
			bool created = api.CreateQueryPool(RHIDevice::Get(), { nri::QueryType::TIMESTAMP, k_MaxTimestamps }, slot.Timestamps) == nri::Result::SUCCESS;
			if (created && statistics)
				created = api.CreateQueryPool(RHIDevice::Get(), { nri::QueryType::PIPELINE_STATISTICS, k_MaxQuerySegments }, slot.Statistics) == nri::Result::SUCCESS;
			if (created && slot.Statistics)
				m_StatisticsSize = api.GetQuerySize(*slot.Statistics);

			nri::BufferDesc resultsDesc = {};
			resultsDesc.size = k_MaxTimestamps * sizeof(uint64_t) + k_MaxQuerySegments * m_StatisticsSize;
			if (created)
				created = api.CreateCommittedBuffer(RHIDevice::Get(), nri::MemoryLocation::HOST_READBACK, 0.0f, resultsDesc, slot.Results) == nri::Result::SUCCESS;
			if (!created)
			{
				LUX_CORE_ERROR_TAG("Renderer", "{}: NRI could not create the GPU query pools; GPU timing disabled", m_DebugName);
				DestroyQuerySlots();
				return false;
			}
		}
		return true;
	}

	void RenderCommandBuffer::DestroyQuerySlots()
	{
		for (QuerySlot& slot : m_QuerySlots)
		{
			Renderer::SubmitResourceFree([timestamps = slot.Timestamps, statistics = slot.Statistics, results = slot.Results]()
				{
					const NRIInterface& api = RHIDevice::API();
					if (timestamps)
						api.DestroyQueryPool(timestamps);
					if (statistics)
						api.DestroyQueryPool(statistics);
					if (results)
						api.DestroyBuffer(results);
				});
		}
		m_QuerySlots.clear();
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
		LUX_CORE_ASSERT(!m_Recording, "RT_Begin inside RT_Begin/RT_End ({})", m_DebugName);
		const uint32_t commandBufferIndex = Renderer::RT_GetCurrentFrameIndex() % m_SlotCount;

		// Everything records into this NRI command buffer; null (logged by the pool) records nothing.
		m_NRICommandBuffer = m_CommandBuffers->Begin();
		m_Recording = true;
		m_BarrierEmitter.SetCommandBuffer(m_NRICommandBuffer);
		m_Tracker.Begin(&m_BarrierEmitter);
		m_RequiredBindings = {};
		m_BindingStatesDirty = true;
		*m_NRIRender = {};

		m_ActiveQuerySlot = nullptr;
		m_ActiveSegment = nullptr;
		if (m_QueryEnabled && m_NRICommandBuffer)
			RT_BeginQuerySegment(m_QuerySlots[commandBufferIndex], continueFrame);

		RT_BeginMarker(m_DebugName);
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

		if (!m_TimerQueryStack.empty())
		{
			LUX_CORE_WARN_TAG("Renderer", "{}: timer query '{}' was not ended", m_DebugName, m_TimerQueryStack.back().first);
			m_TimerQueryStack.clear();
		}
		if (m_ActiveQuerySlot)
			RT_EndQuerySegment(*m_ActiveQuerySlot);

#if LUX_ENABLE_PROFILING
		// Any zone still open at the end of the buffer is a begin/end mismatch. It has to be closed
		// HERE, while the command buffer is still recording: ~VkCtxScope issues a
		// vkCmdWriteTimestamp, which is illegal once recording has ended.
		m_TracyGPUZones.clear();

		// Tracy resolves its timestamps here, once per submitted segment of this buffer: outside a
		// render pass (the pass ended above) and before recording ends.
		if (m_QueryEnabled && m_NRICommandBuffer)
		{
			if (TracyVkCtx tracyContext = GetTracyGPUContext())
				TracyVkCollect(tracyContext, static_cast<VkCommandBuffer>(RHIDevice::API().GetCommandBufferNativeObject(m_NRICommandBuffer)));
		}
#endif

		// Every resource the command buffer touched returns to its resting state, where the next
		// command buffer, on any queue, starts.
		m_Tracker.End();

		LUX_CORE_ASSERT(!m_InNRISegment, "RT_End inside an NRI segment ({})", m_DebugName);
		m_BarrierEmitter.SetCommandBuffer(nullptr);
		if (m_NRICommandBuffer && m_CommandBuffers->End(*m_NRICommandBuffer))
			m_RecordedCommandBuffer = m_NRICommandBuffer;
		m_NRICommandBuffer = nullptr;
		m_Recording = false;
	}

	RenderCommandBuffer::~RenderCommandBuffer()
	{
		DestroyQuerySlots();
	}

	nri::CommandBuffer* RenderCommandBuffer::RT_BeginNRISegment()
	{
		LUX_CORE_ASSERT(m_Recording, "RT_BeginNRISegment outside RT_Begin/RT_End ({})", m_DebugName);
		LUX_CORE_ASSERT(!m_InNRISegment, "Nested NRI segment ({})", m_DebugName);

		// RT_Begin logged a failed wrap.
		if (!m_NRICommandBuffer)
			return nullptr;

		// Committing through the tracker clears its pending flag too.
		m_Tracker.Commit();
		m_InNRISegment = true;
		return m_NRICommandBuffer;
	}

	void RenderCommandBuffer::RT_EndNRISegment()
	{
		LUX_CORE_ASSERT(m_InNRISegment, "RT_EndNRISegment without RT_BeginNRISegment ({})", m_DebugName);
		m_InNRISegment = false;
	}

	nri::CommandBuffer* RenderCommandBuffer::RT_BeginNRITransfer()
	{
		RT_SuspendNRIRendering("a copy or clear");
		return RT_BeginNRISegment();
	}

	void RenderCommandBuffer::RT_WriteBuffer(const NRIBuffer& buffer, const void* data, uint64_t size, uint64_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!buffer || !data || size == 0)
			return;

		// vkCmdCopyBuffer takes any offset; 16 keeps the staged data vector-aligned.
		const StagingStream::Allocation source = m_Staging.Write(data, size, 16);
		if (!source)
			return;

		RT_RequireBufferState(buffer.GetTracked(), ResourceState::CopyDest);
		nri::CommandBuffer* commandBuffer = RT_BeginNRITransfer();
		if (!commandBuffer)
			return;

		RHIDevice::API().CmdCopyBuffer(*commandBuffer, *buffer.Get(), offset, *source.Buffer, source.Offset, size);
		RT_EndNRISegment();
	}

	bool RenderCommandBuffer::RT_BeginNRIRenderPass(const NRIRenderPassDesc& desc, std::string_view name, const nri::Viewport& viewport, const nri::Rect& scissor, const nri::ShadingRateDesc& shadingRate)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		NRIRenderState& state = *m_NRIRender;
		LUX_CORE_ASSERT(!state.Active, "NRI render pass '{}' begun inside '{}' ({})", name, state.Name, m_DebugName);
		LUX_CORE_ASSERT(desc.PipelineLayout && desc.Pipeline);

		state = {};
		state.Active = true;
		state.Name = name;
		state.NameHash = Hash::GenerateFNVHash(name);
		state.Desc = desc;
		for (size_t i = 0; i < desc.DescriptorSets.size(); i++)
			state.PassGroups[i] = desc.DescriptorSets[i].Group;
		state.Viewport = viewport;
		state.Scissor = scissor;
		state.ShadingRate = shadingRate;

		// The attachments are required whenever a pass opens; the sets under the draws' change
		// detection, so a draw that binds the pass's own set 0 again requires nothing. The buffers
		// are forgotten, as NVRHI's pass began without any.
		RT_BeginRequirements(RequiredBindPoint::Graphics);
		m_RequiredBindings.VertexBuffer = nullptr;
		m_RequiredBindings.IndexBuffer = nullptr;
		m_RequiredBindings.IndirectArguments = nullptr;
		RT_RequireNRIRenderPass();

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
		std::array<nri::AttachmentDesc, MaxRenderTargets> colors = {};
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

		// The pass state is rebound in every scope: a dispatch in between binds its own.
		api.CmdSetViewports(*commandBuffer, &state.Viewport, 1);
		api.CmdSetScissors(*commandBuffer, &state.Scissor, 1);
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

	void RenderCommandBuffer::RT_SetNRIViewport(const nri::Viewport& viewport)
	{
		NRIRenderState& state = *m_NRIRender;
		state.Viewport = viewport;
		if (state.Rendering)
			RHIDevice::API().CmdSetViewports(*m_NRICommandBuffer, &state.Viewport, 1);
	}

	void RenderCommandBuffer::RT_SetNRIScissor(const nri::Rect& scissor)
	{
		NRIRenderState& state = *m_NRIRender;
		state.Scissor = scissor;
		if (state.Rendering)
			RHIDevice::API().CmdSetScissors(*m_NRICommandBuffer, &state.Scissor, 1);
	}

	void RenderCommandBuffer::RT_SetNRIShadingRate(const nri::ShadingRateDesc& shadingRate)
	{
		NRIRenderState& state = *m_NRIRender;
		state.ShadingRate = shadingRate;
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

		// Forgotten requirements include the pass's: NVRHI re-required its whole graphics state.
		if (RT_BeginRequirements(RequiredBindPoint::Graphics))
			RT_RequireNRIRenderPass();
		if (hasDrawSet)
			RT_RequireSet(pass.DrawSetIndex, drawSet);
		RT_RequireBuffer(m_RequiredBindings.VertexBuffer, bindings.VertexBuffer, ResourceState::VertexBuffer);
		RT_RequireBuffer(m_RequiredBindings.IndexBuffer, bindings.IndexBuffer, ResourceState::IndexBuffer);
		RT_RequireBuffer(m_RequiredBindings.IndirectArguments, bindings.IndirectArguments, ResourceState::IndirectArgument);

		// Barriers cannot be recorded inside rendering: commit them between two scopes.
		if (m_Tracker.HasPendingBarriers())
		{
			RT_SuspendNRIRendering("a barrier");
			m_Tracker.Commit();
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
		LUX_CORE_ASSERT(desc.PipelineLayout && desc.Pipeline);
		RT_SuspendNRIRendering("a compute dispatch");

		RT_BeginRequirements(RequiredBindPoint::Compute);
		for (uint32_t setIndex = 0; setIndex < desc.DescriptorSets.size(); setIndex++)
			RT_RequireSet(setIndex, desc.DescriptorSets[setIndex]);

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

	void RenderCommandBuffer::RT_Submit(nri::Fence* waitSemaphore)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_ASSERT(!m_Recording, "RT_Submit inside RT_Begin/RT_End ({})", m_DebugName);

		// Flush batched resource uploads first: anything this command list may
		// consume (mesh buffers, texture data) must reach the queue ahead of it.
		// Cheap no-op when nothing is pending.
		Renderer::FlushResourceUploads();

		// If asset uploads were flushed on the dedicated transfer queue, make this
		// queue wait for that copy to complete before it reads the uploaded mesh/
		// texture data. No-op in graphics-queue fallback mode (nothing pending).
		uint64_t uploadInstance = 0;
		if (Renderer::ConsumePendingUpload(m_Queue, uploadInstance))
			Renderer::QueueWaitForCommandList(m_Queue, GPUQueue::Copy, uploadInstance);

		// A recording that failed submits nothing, but the waits still go out (an acquired
		// swapchain image's semaphore must be waited for), so the queue sees a submission anyway.
		nri::CommandBuffer* commandBuffer = std::exchange(m_RecordedCommandBuffer, nullptr);
		RHIQueueTimeline::Submission submission;
		if (commandBuffer)
			submission.CommandBuffers = { &commandBuffer, 1 };
		submission.WaitSemaphore = waitSemaphore;
		const uint64_t value = RHIQueueTimeline::Submit(m_Queue, submission);
		if (commandBuffer && value)
			m_CommandBuffers->MarkSubmitted(*commandBuffer, value);
		// The value another queue waits for (Renderer::QueueWaitForCommandList).
		if (value)
			m_LastExecutionInstance = value;

		// The writes' staging memory comes back once this frame retires.
		m_Staging.Retire();
	}

	bool RenderCommandBuffer::RT_WaitForCompletion()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		RHIQueueTimeline::Wait(m_Queue, m_LastExecutionInstance);
		return true;
	}

	// Labels and checkpoints are legal inside rendering, so markers do not close an NRI scope.
	void RenderCommandBuffer::RT_BeginMarker(const std::string& label)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_NRICommandBuffer)
			return;
		const NRIInterface& api = RHIDevice::API();
		api.CmdBeginAnnotation(*m_NRICommandBuffer, label.c_str(), nri::BGRA_UNUSED);
		if (Aftermath::IsEnabled())
			Aftermath::SetCheckpoint(static_cast<VkCommandBuffer>(api.GetCommandBufferNativeObject(m_NRICommandBuffer)), label);
	}

	void RenderCommandBuffer::RT_EndMarker()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_NRICommandBuffer)
			RHIDevice::API().CmdEndAnnotation(*m_NRICommandBuffer);
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

	void RenderCommandBuffer::RT_RequireBuffer(const void*& required, const NRIBuffer* buffer, ResourceState state)
	{
		if (!buffer || !buffer->GetTracked().Handle || buffer->GetTracked().Handle == required)
			return;

		required = buffer->GetTracked().Handle;
		m_Tracker.Require(buffer->GetTracked(), state);
	}

	// The attachment states NVRHI's setResourceStatesForFramebuffer required, and the pass's sets.
	void RenderCommandBuffer::RT_RequireNRIRenderPass()
	{
		const NRIRenderPassDesc& desc = m_NRIRender->Desc;
		for (uint32_t i = 0; i < desc.ColorAttachmentCount; i++)
		{
			const NRIAttachment& attachment = desc.ColorAttachments[i];
			m_Tracker.Require(attachment.Texture, attachment.Range, ResourceState::RenderTarget);
		}
		if (desc.DepthAttachment.Texture.Handle)
			m_Tracker.Require(desc.DepthAttachment.Texture, desc.DepthAttachment.Range, ResourceState::DepthWrite);

		for (uint32_t setIndex = 0; setIndex < desc.DescriptorSets.size(); setIndex++)
			RT_RequireSet(setIndex, desc.DescriptorSets[setIndex]);
	}

	void RenderCommandBuffer::RT_RequireTextureState(const TrackedTexture& texture, const TextureSubresourceRange& range, ResourceState state)
	{
		m_Tracker.Require(texture, range, state);
		m_BindingStatesDirty = true;
	}

	void RenderCommandBuffer::RT_RequireBufferState(const TrackedBuffer& buffer, ResourceState state)
	{
		m_Tracker.Require(buffer, state);
		m_BindingStatesDirty = true;
	}

	// Unlike RT_Require*, a transition does not mark bound sets dirty: NVRHI's explicit state
	// transitions did not either (only copies, clears and writes did), and the tracker keeps
	// NVRHI's barriers.
	void RenderCommandBuffer::RT_TransitionTextureState(const TrackedTexture& texture, const TextureSubresourceRange& range, ResourceState state)
	{
		m_Tracker.Require(texture, range, state);
	}

	void RenderCommandBuffer::RT_TransitionBufferState(const TrackedBuffer& buffer, ResourceState state)
	{
		m_Tracker.Require(buffer, state);
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

	void RenderCommandBuffer::RT_BeginQuerySegment(QuerySlot& slot, bool continueFrame)
	{
		const NRIInterface& api = RHIDevice::API();
		const uint64_t frame = Renderer::RT_GetFrameNumber();
		if (slot.Frame != frame)
		{
			// The slot's last frame has retired (one slot per frame in flight): publish it, then
			// reset the pools for this frame. Outside rendering, ahead of every query of the frame.
			RT_ResolveQueries(slot);
			slot.Frame = frame;
			slot.TimestampCount = 0;
			slot.CopiedTimestampCount = 0;
			slot.StatisticsCount = 0;
			slot.Segments.clear();
			slot.Timers.clear();
			api.CmdResetQueries(*m_NRICommandBuffer, *slot.Timestamps, 0, k_MaxTimestamps);
			if (slot.Statistics)
				api.CmdResetQueries(*m_NRICommandBuffer, *slot.Statistics, 0, k_MaxQuerySegments);
		}
		else if (!continueFrame)
		{
			// A new measurement within the frame: the earlier segments no longer count.
			slot.Segments.clear();
		}

		m_ActiveQuerySlot = &slot;
		// Two timestamps per segment are always kept for it; past the limits the segment is not
		// measured.
		if (slot.TimestampCount + 2 > k_MaxTimestamps || slot.StatisticsCount == k_MaxQuerySegments)
		{
			m_ActiveSegment = nullptr;
			return;
		}

		QuerySegment& segment = slot.Segments.emplace_back();
		segment.BeginTimestamp = slot.TimestampCount++;
		segment.EndTimestamp = slot.TimestampCount++;
		segment.Statistics = slot.StatisticsCount++;
		m_ActiveSegment = &segment;
		api.CmdEndQuery(*m_NRICommandBuffer, *slot.Timestamps, segment.BeginTimestamp);
		if (slot.Statistics)
			api.CmdBeginQuery(*m_NRICommandBuffer, *slot.Statistics, segment.Statistics);
	}

	void RenderCommandBuffer::RT_EndQuerySegment(QuerySlot& slot)
	{
		const NRIInterface& api = RHIDevice::API();
		if (m_ActiveSegment)
		{
			if (slot.Statistics)
				api.CmdEndQuery(*m_NRICommandBuffer, *slot.Statistics, m_ActiveSegment->Statistics);
			api.CmdEndQuery(*m_NRICommandBuffer, *slot.Timestamps, m_ActiveSegment->EndTimestamp);
			if (slot.Statistics)
				api.CmdCopyQueries(*m_NRICommandBuffer, *slot.Statistics, m_ActiveSegment->Statistics, 1, *slot.Results, k_MaxTimestamps * sizeof(uint64_t) + m_ActiveSegment->Statistics * m_StatisticsSize);
			m_ActiveSegment->Ended = true;
		}

		// Everything this command list wrote: the segment's timestamps and its timers'.
		if (slot.TimestampCount > slot.CopiedTimestampCount)
		{
			api.CmdCopyQueries(*m_NRICommandBuffer, *slot.Timestamps, slot.CopiedTimestampCount, slot.TimestampCount - slot.CopiedTimestampCount,
				*slot.Results, slot.CopiedTimestampCount * sizeof(uint64_t));
			slot.CopiedTimestampCount = slot.TimestampCount;
		}

		// A fence wait does not make device writes visible to the host; this barrier does (the
		// tracker has no host state, and NRI takes host access only in global barriers).
		nri::GlobalBarrierDesc toHost = {};
		toHost.before = { nri::AccessBits::COPY_DESTINATION, nri::StageBits::COPY };
		toHost.after = { nri::AccessBits::HOST_READ, nri::StageBits::HOST };
		nri::BarrierDesc barrierDesc = {};
		barrierDesc.globals = &toHost;
		barrierDesc.globalNum = 1;
		api.CmdBarrier(*m_NRICommandBuffer, barrierDesc);

		m_ActiveSegment = nullptr;
		m_ActiveQuerySlot = nullptr;
	}

	void RenderCommandBuffer::RT_ResolveQueries(const QuerySlot& slot)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (slot.Frame == UINT64_MAX || (slot.Segments.empty() && slot.Timers.empty()))
			return;

		const NRIInterface& api = RHIDevice::API();
		const uint8_t* results = static_cast<const uint8_t*>(api.MapBuffer(*slot.Results, 0, nri::WHOLE_SIZE));
		const uint64_t* timestamps = reinterpret_cast<const uint64_t*>(results);
		const double millisecondsPerTick = 1000.0 / static_cast<double>(RHIDevice::GetDesc().other.timestampFrequencyHz);
		auto elapsed = [&](uint32_t begin, uint32_t end)
			{
				return static_cast<float>(static_cast<double>(timestamps[end] - timestamps[begin]) * millisecondsPerTick);
			};

		// The frame total is the sum of its segments (Begin(true) splits a frame).
		float frameTime = 0.0f;
		bool measured = false;
		PipelineStatistics statistics;
		for (const QuerySegment& segment : slot.Segments)
		{
			if (!segment.Ended)
				continue;
			frameTime += elapsed(segment.BeginTimestamp, segment.EndTimestamp);
			measured = true;

			if (slot.Statistics)
			{
				nri::PipelineStatisticsDesc counters;
				std::memcpy(&counters, results + k_MaxTimestamps * sizeof(uint64_t) + segment.Statistics * m_StatisticsSize, sizeof(counters));
				statistics.InputAssemblyVertices += counters.inputVertexNum;
				statistics.InputAssemblyPrimitives += counters.inputPrimitiveNum;
				statistics.VertexShaderInvocations += counters.vertexShaderInvocationNum;
				statistics.ClippingInvocations += counters.rasterizerInPrimitiveNum;
				statistics.ClippingPrimitives += counters.rasterizerOutPrimitiveNum;
				statistics.FragmentShaderInvocations += counters.fragmentShaderInvocationNum;
				statistics.ComputeShaderInvocations += counters.computeShaderInvocationNum;
			}
		}

		if (measured)
		{
			// Seed on the first sample, otherwise the average crawls up from zero over dozens of
			// frames and reads as a bogus sub-millisecond frame.
			const float previous = m_LastGPUWorkTime.load(std::memory_order_relaxed);
			m_LastGPUWorkTime.store(previous > 0.0f ? frameTime * kGPUTimeSmoothing + previous * (1.0f - kGPUTimeSmoothing) : frameTime, std::memory_order_relaxed);
			if (slot.Statistics)
				m_LastPipelineStatistics.store(statistics, std::memory_order_relaxed);
		}

		// A timer used more than once in the frame reports its total.
		std::unordered_map<std::string_view, float> frameTimers;
		for (const QueryTimer& timer : slot.Timers)
			frameTimers[timer.Name] += elapsed(timer.BeginTimestamp, timer.EndTimestamp);
		for (const auto& [name, time] : frameTimers)
		{
			auto [it, inserted] = m_NamedTimerQueryResults.try_emplace(std::string(name), time);
			if (!inserted)
				it->second = time * kGPUTimeSmoothing + it->second * (1.0f - kGPUTimeSmoothing);
		}

		api.UnmapBuffer(*slot.Results);
	}

	void RenderCommandBuffer::RT_BeginTimerQuery(const std::string& name)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_ActiveQuerySlot)
			return;

		// Timestamps are legal inside rendering, so a timer does not split an NRI render pass. One
		// timestamp stays free for each open timer's end.
		QuerySlot& slot = *m_ActiveQuerySlot;
		uint32_t begin = UINT32_MAX;
		if (slot.TimestampCount + 2 + m_TimerQueryStack.size() <= k_MaxTimestamps)
		{
			begin = slot.TimestampCount++;
			RHIDevice::API().CmdEndQuery(*m_NRICommandBuffer, *slot.Timestamps, begin);
		}
		else if (!m_ReportedTimerOverflow)
		{
			LUX_CORE_WARN_TAG("Renderer", "{}: more than {} GPU timestamps in a frame; '{}' and later timers are not measured", m_DebugName, k_MaxTimestamps, name);
			m_ReportedTimerOverflow = true;
		}

#if LUX_ENABLE_PROFILING
		// The Tracy zone brackets the same span as the engine's own timer.
		if (TracyVkCtx tracyContext = GetTracyGPUContext())
		{
			VkCommandBuffer vkCommandBuffer = static_cast<VkCommandBuffer>(RHIDevice::API().GetCommandBufferNativeObject(m_NRICommandBuffer));
			m_TracyGPUZones.push_back(CreateScope<tracy::VkCtxScope>(
				tracyContext,
				uint32_t(__LINE__), __FILE__, std::strlen(__FILE__),
				m_DebugName.c_str(), m_DebugName.size(),
				name.c_str(), name.size(),
				vkCommandBuffer, true));
		}
#endif

		m_TimerQueryStack.emplace_back(name, begin);
	}

	void RenderCommandBuffer::RT_EndTimerQuery()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_ActiveQuerySlot)
			return;

		if (m_TimerQueryStack.empty())
		{
			LUX_CORE_WARN_TAG("Renderer", "{}: a timer query ended that was not begun", m_DebugName);
			return;
		}

		auto [name, begin] = std::move(m_TimerQueryStack.back());
		m_TimerQueryStack.pop_back();

#if LUX_ENABLE_PROFILING
		// Popped alongside the name, so the two stacks stay in step. Destroying the scope writes
		// the closing timestamp.
		if (!m_TracyGPUZones.empty())
			m_TracyGPUZones.pop_back();
#endif

		if (begin == UINT32_MAX)
			return;

		QuerySlot& slot = *m_ActiveQuerySlot;
		const uint32_t end = slot.TimestampCount++;
		RHIDevice::API().CmdEndQuery(*m_NRICommandBuffer, *slot.Timestamps, end);
		slot.Timers.push_back({ std::move(name), begin, end });
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
