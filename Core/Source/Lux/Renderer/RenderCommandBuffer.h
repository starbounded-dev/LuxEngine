// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Core/Ref.h"
#include "Lux/Debug/Profiler.h"

#include "PipelineSpecification.h"
#include "Lux/Renderer/RHI/DescriptorSetGroup.h"
#include "Lux/Renderer/RHI/NVRHIBarrierEmitter.h"
#include "Lux/Renderer/RHI/RHITypes.h"
#include "Lux/Renderer/RendererConfig.h"

#include "nvrhi/nvrhi.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

//pipeline queries require direct vulkan access, this can toggle that
#define CMD_BUFFER_USE_VULKAN_QUERIES
#include <vulkan/vulkan.h>

#if LUX_ENABLE_PROFILING
#include <tracy/TracyVulkan.hpp>
#endif

namespace nri {
	struct Buffer;
	struct CommandBuffer;
	struct Descriptor;
	struct DescriptorSet;
	struct Pipeline;
	struct PipelineLayout;
}

namespace Lux {

	class NRIBuffer;

	class RenderCommandBuffer : public RefCounted
	{
	public:
		static Ref<RenderCommandBuffer> Create(uint32_t count = 0, const std::string& debugName = "", bool enableQueries = false, GPUQueue queue = GPUQueue::Graphics) { return Ref<RenderCommandBuffer>::Create(count, enableQueries, debugName, queue); }

		// continueFrame: this Begin resumes a frame that was already begun, ended and submitted
		// mid-frame (e.g. to interleave another command buffer). Its GPU time is added to the
		// frame's total instead of starting a new frame measurement.
		void Begin(bool continueFrame = false);
		void End();
		void Submit();

		void RT_Begin(bool continueFrame = false);
		void RT_End();
		void RT_Submit();
		void RT_Submit(VkSemaphore waitSemaphore);

		void RT_Wait(VkSemaphore waitSemaphore);

		void RT_BeginMarker(const std::string& label);
		void RT_EndMarker();

		// Resource states (NRI migration Phase 4). The tracker places every barrier (NVRHI's automatic
		// barriers are off): NRI passes, draws and dispatches require what they bind, and code that
		// touches a resource outside them requires it first.
		//   RT_Require*:    a copy, clear or write; also re-requires the bindings of the next draw or
		//                   dispatch, as NVRHI did. Emitted only with explicit barriers.
		//   RT_Transition*: a transition the code always needed (mip chains, compute->indirect).
		void RT_RequireTextureState(nvrhi::ITexture* texture, const TextureSubresourceRange& range, ResourceState state);
		void RT_RequireBufferState(nvrhi::IBuffer* buffer, ResourceState state);
		void RT_TransitionTextureState(nvrhi::ITexture* texture, const TextureSubresourceRange& range, ResourceState state);
		void RT_TransitionBufferState(nvrhi::IBuffer* buffer, ResourceState state);
		void RT_CommitBarriers();
		bool RT_UsesExplicitBarriers() const { return m_ExplicitBarriers; }
		// Debug (render graph): collect the handles of every requirement until RT_EndRequirementLog.
		void RT_BeginRequirementLog();
		std::vector<const void*> RT_EndRequirementLog();
		const ResourceStateTracker& RT_GetTracker() const { return m_Tracker; }
		// Which barriers the next RT_Begin records with.
		enum class BarrierMode : uint8_t
		{
			// The tracker's: everything recorded with NRI needs them.
			Explicit,
			// NVRHI's automatic barriers, for work the tracker cannot express on NVRHI: readbacks into
			// staging textures (no public state API; NRI Phase 13). Nothing may be drawn or dispatched.
			Automatic,
		};
		void SetBarrierMode(BarrierMode mode) { m_BarrierMode = mode; }

		// NRI recording inside this command buffer (NRI migration Phase 9, plan §2.2 I3/I4). Between
		// RT_BeginNRISegment and RT_EndNRISegment only NRI records, into a non-owning NRI wrapper of
		// the NVRHI command list's VkCommandBuffer. The segment starts with pending barriers committed
		// (emitted through NVRHI until Phase 13). Render thread, between RT_Begin and RT_End. Null, with
		// no segment open, when NRI cannot wrap the command list (logged).
		nri::CommandBuffer* RT_BeginNRISegment();
		void RT_EndNRISegment();

		// NRI render passes (NRI migration Phase 10). Render thread.
		//
		// A pass begun with RT_BeginNRIRenderPass records through NRI, in NRI rendering scopes inside
		// the NVRHI command list, until RT_EndNRIRenderPass. NVRHI may not record inside such a scope,
		// so every NVRHI entry point here (GetActive, dispatches, timer queries) closes it first and
		// the next draw reopens it, with the attachments loaded and the pass state rebound: NVRHI's
		// implicit render pass splitting, made explicit. Barriers are the tracker's (NRI passes need
		// RT_UsesExplicitBarriers); pending ones are committed between scopes.
		struct NRIAttachment
		{
			nri::Descriptor* View = nullptr;
			// The texture and subresources the view covers, required as a render target (or depth
			// write) when the pass opens.
			nvrhi::ITexture* Texture = nullptr;
			TextureSubresourceRange Range;
		};

		struct NRIRenderPassDesc
		{
			std::array<NRIAttachment, nvrhi::c_MaxRenderTargets> ColorAttachments = {};
			// Clears happen when the pass opens; a reopened scope loads.
			std::array<bool, nvrhi::c_MaxRenderTargets> ClearColor = {};
			std::array<std::array<float, 4>, nvrhi::c_MaxRenderTargets> ClearColorValues = {};
			uint32_t ColorAttachmentCount = 0;
			NRIAttachment DepthAttachment;
			bool ClearDepth = false;
			float ClearDepthValue = 0.0f;

			nri::PipelineLayout* PipelineLayout = nullptr;
			nri::Pipeline* Pipeline = nullptr;
			// The pass's descriptor sets by NRI set index, required and bound when the pass opens; empty
			// ones are not bound by the pass. Their groups are kept alive until the pass ends.
			std::array<BoundDescriptorSet, nvrhi::c_MaxBindingLayouts> DescriptorSets = {};
			// Width of line pipelines (0: the pipeline's static width) and whether the pipeline takes
			// its shading rate dynamically.
			float LineWidth = 0.0f;
			bool DynamicShadingRate = false;

			// For the draws: the NRI set index of set 0 (UINT32_MAX when the shader has none), the
			// stride of vertex buffer 0 and whether the layout has root constants.
			uint32_t DrawSetIndex = UINT32_MAX;
			uint32_t VertexStride = 0;
			bool RootConstants = false;
		};
		// Requires the attachments and the pass's sets, then opens the pass. False, with nothing
		// recorded, when NRI cannot record into this command list (logged).
		bool RT_BeginNRIRenderPass(const NRIRenderPassDesc& desc, std::string_view name, const nvrhi::ViewportState& viewport, const nvrhi::VariableRateShadingState& shadingRate);
		void RT_EndNRIRenderPass();
		bool RT_InNRIRenderPass() const;
		const NRIRenderPassDesc& RT_GetNRIRenderPass() const;
		std::string_view RT_GetNRIRenderPassName() const;
		void RT_SetNRIViewportState(const nvrhi::ViewportState& viewport);
		// Replace the viewports with `viewport` (keeping the scissor rectangles), or the scissor
		// rectangles with `scissor` (keeping the viewports).
		void RT_SetNRIViewport(const nvrhi::Viewport& viewport);
		void RT_SetNRIScissor(const nvrhi::Rect& scissor);
		void RT_SetNRIShadingRate(const nvrhi::VariableRateShadingState& shadingRate);

		// What one draw in the open NRI render pass binds.
		struct NRIDrawBindings
		{
			// Bound at the pass's DrawSetIndex; an empty one leaves the pass's own set there.
			BoundDescriptorSet DrawSet;
			const NRIBuffer* VertexBuffer = nullptr;
			const NRIBuffer* IndexBuffer = nullptr;
			bool IndexBuffer16 = false;
			const NRIBuffer* IndirectArguments = nullptr;
			// Ignored when the pass's layout has no root constants.
			const void* RootConstants = nullptr;
			uint32_t RootConstantsSize = 0;
		};
		// Before a draw: requires what it binds (re-required when it changes, after a copy, clear or
		// write, and always for sets with storage uses, as NVRHI's automatic barriers did), commits
		// pending barriers outside rendering, (re)opens the rendering scope and binds; repeats are
		// skipped. The command buffer to draw into; null, with nothing bound, when no scope can be
		// opened (logged).
		nri::CommandBuffer* RT_BeginNRIDraw(const NRIDrawBindings& bindings);

		// One NRI compute dispatch (NRI migration Phase 9).
		struct NRIDispatchDesc
		{
			nri::PipelineLayout* PipelineLayout = nullptr;
			nri::Pipeline* Pipeline = nullptr;
			// By NRI set index; empty ones are not bound.
			std::array<BoundDescriptorSet, nvrhi::c_MaxBindingLayouts> DescriptorSets = {};
			const void* RootConstants = nullptr;
			uint32_t RootConstantsSize = 0;
		};
		// Closes an open rendering scope, requires the sets as RT_BeginNRIDraw does, and opens an NRI
		// segment with the pipeline, sets and root constants bound. Record the dispatch into the
		// returned command buffer, then call RT_EndNRISegment. Null, with no segment open, when NRI
		// cannot wrap the command list (logged).
		nri::CommandBuffer* RT_BeginNRIDispatch(const NRIDispatchDesc& desc);

		// The NVRHI command list, for recording NVRHI commands. Closes an open NRI rendering scope
		// first (see RT_BeginNRIRenderPass).
		nvrhi::CommandListHandle GetActive();
		nvrhi::CommandListHandle Get(uint32_t index = 0) const { LUX_CORE_VERIFY(index < m_CommandLists.size());  return m_CommandLists[index]; }

		// The queue this command buffer records/submits on (Graphics by default).
		GPUQueue GetQueue() const { return m_Queue; }
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
		RenderCommandBuffer(uint32_t count, bool enableQueries, const std::string& debugName, GPUQueue queue = GPUQueue::Graphics);
		virtual ~RenderCommandBuffer();
	private:
		enum class RequiredBindPoint : uint8_t { None, Graphics, Compute };
		// Starts the requirements of a draw or dispatch. True when what was required before has been
		// forgotten: the bind point changed, or a copy, clear or write happened since (NVRHI forgot
		// its committed state in both cases).
		bool RT_BeginRequirements(RequiredBindPoint bindPoint);
		void RT_RequireSet(uint32_t setIndex, const BoundDescriptorSet& set);
		void RT_RequireBuffer(nvrhi::IBuffer*& required, const NRIBuffer* buffer, ResourceState state);
		// The open pass's attachments and sets.
		void RT_RequireNRIRenderPass();
		void RT_CrossCheckStates(const char* context);
		void DestroyNRIWrapper();
		nri::CommandBuffer* RT_OpenNRIRendering(bool passStart);
		void RT_CloseNRIRendering();
		// Closes the rendering scope because `reason` cannot be recorded inside it.
		void RT_SuspendNRIRendering(const char* reason);
		// Bind into the open rendering scope; repeated arguments are skipped.
		void RT_SetNRIDescriptorSet(uint32_t setIndex, nri::DescriptorSet* descriptorSet);
		void RT_SetNRIVertexBuffer(nri::Buffer* buffer, uint32_t stride);
		void RT_SetNRIIndexBuffer(nri::Buffer* buffer, bool use16BitIndices);
		void RT_SetNRIRootConstants(const void* data, uint32_t size);
	private:
		GPUQueue m_Queue = GPUQueue::Graphics;
		uint64_t m_LastExecutionInstance = 0;

		ResourceStateTracker m_Tracker;
		NVRHIBarrierEmitter m_BarrierEmitter;
		bool m_ExplicitBarriers = false;
		BarrierMode m_BarrierMode = BarrierMode::Explicit;
		std::vector<const void*> m_RequirementLog;
		// What the last draws or dispatch required, for NVRHI's automatic-barrier change detection
		// (kept so the barriers stay the ones NVRHI placed): bindings are re-required when they change,
		// after a copy/clear/write requirement (m_BindingStatesDirty; explicit transitions leave it
		// alone, as NVRHI's setTextureState did), and always when a set has storage uses (that
		// re-require is what places UAV barriers between dispatches). Raw pointers, compared and never
		// dereferenced; reset by RT_Begin.
		struct RequiredBindings
		{
			RequiredBindPoint BindPoint = RequiredBindPoint::None;
			std::array<nri::DescriptorSet*, nvrhi::c_MaxBindingLayouts> Sets = {};
			nvrhi::IBuffer* VertexBuffer = nullptr;
			nvrhi::IBuffer* IndexBuffer = nullptr;
			nvrhi::IBuffer* IndirectArguments = nullptr;
		};
		RequiredBindings m_RequiredBindings;
		bool m_BindingStatesDirty = true;
		// The NRI wrapper of the open command list (created by the first segment after RT_Begin,
		// destroyed by RT_End) and whether a segment is open.
		nri::CommandBuffer* m_NRICommandBuffer = nullptr;
		bool m_InNRISegment = false;
		// The NRI render pass (NRI types, so defined in the .cpp).
		struct NRIRenderState;
		Scope<NRIRenderState> m_NRIRender;

		nvrhi::static_vector<nvrhi::CommandListHandle, RendererConfig::MaxFramesInFlight> m_CommandLists;
		// Frame-level timer queries: one per submitted segment of the frame, per frame index.
		// A frame split by Begin(true) records several segments; the published time is their sum.
		nvrhi::static_vector<std::vector<nvrhi::TimerQueryHandle>, RendererConfig::MaxFramesInFlight> m_TimerQueries;
		nvrhi::static_vector<uint32_t, RendererConfig::MaxFramesInFlight> m_TimerSegmentCounts;

		// Published by the render thread, read by the main thread (profiling panels).
		//
		// Deliberately a single latest value rather than a per-frame slot. The render
		// thread indexes its command lists and query pools by
		// Renderer::RT_GetCurrentFrameIndex() - the render frame slot - while every
		// caller of the getters below is on the main thread, holding
		// Application::m_CurrentFrameIndex. Those are two different sequences that need
		// not line up (the render thread runs a frame behind), so handing a value between
		// the threads via either index reads a foreign slot. That is what previously
		// reported a ~0.01 ms frame time against multi-millisecond pass timings, when the
		// render-thread index was still the (non-monotonic) back-buffer index.
		//
		// This mirrors m_NamedTimerQueryResults, which is keyed by name only and has
		// always reported correctly for exactly this reason.
		std::atomic<float> m_LastGPUWorkTime = 0.0f;

		bool m_QueryEnabled;

		std::string m_DebugName;

		nvrhi::CommandListHandle m_ActiveCommandBuffer;
		nvrhi::TimerQueryHandle m_ActiveTimerQuery;

		std::string lastpop;

		// String-based timer query storage - allocated on demand
		nvrhi::static_vector<std::unordered_map<std::string, nvrhi::TimerQueryHandle>, RendererConfig::MaxFramesInFlight> m_NamedTimerQueries;
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
