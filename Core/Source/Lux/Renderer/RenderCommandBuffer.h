// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Core/Ref.h"
#include "Lux/Debug/Profiler.h"

#include "PipelineSpecification.h"
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

		nvrhi::GraphicsState& GetGraphicsState() { return m_GraphicsState; }
		const nvrhi::GraphicsState& GetGraphicsState() const { return m_GraphicsState; }
		void SetGraphicsState(nvrhi::GraphicsState& graphicsState) { m_GraphicsState = graphicsState; }
		void RT_CommitGraphicsState();

		nvrhi::ComputeState& GetComputeState() { return m_ComputeState; }
		const nvrhi::ComputeState& GetComputeState() const { return m_ComputeState; }
		void SetComputeState(nvrhi::ComputeState& computeState) { m_ComputeState = computeState; }
		void RT_CommitComputeState();

		// Resource states (NRI migration Phase 4). With Renderer.ExplicitBarriers on, NVRHI's automatic
		// barriers are off for this command buffer and every GPU access has to be required here:
		// the commit functions require everything in the graphics/compute/meshlet state, and code that
		// touches a resource outside them (copies, clears, writes) requires it first.
		//   RT_Require*:    a requirement NVRHI makes on its own in automatic mode; emitted only with
		//                   explicit barriers on.
		//   RT_Transition*: a transition the code always needed (mip chains, compute->indirect);
		//                   emitted in both modes.
		void RT_RequireTextureState(nvrhi::ITexture* texture, const TextureSubresourceRange& range, ResourceState state);
		void RT_RequireBufferState(nvrhi::IBuffer* buffer, ResourceState state);
		void RT_TransitionTextureState(nvrhi::ITexture* texture, const TextureSubresourceRange& range, ResourceState state);
		void RT_TransitionBufferState(nvrhi::IBuffer* buffer, ResourceState state);
		// Attachment clears are transfer operations (CopyDest), unlike drawing into them.
		void RT_RequireColorAttachmentClear(nvrhi::IFramebuffer* framebuffer, uint32_t attachmentIndex);
		void RT_RequireDepthAttachmentClear(nvrhi::IFramebuffer* framebuffer);
		void RT_CommitBarriers();
		void RT_CommitMeshletState(const nvrhi::MeshletState& meshletState);
		bool RT_UsesExplicitBarriers() const { return m_ExplicitBarriers; }
		// Debug (render graph): collect the handles of every requirement until RT_EndRequirementLog.
		void RT_BeginRequirementLog();
		std::vector<const void*> RT_EndRequirementLog();
		const ResourceStateTracker& RT_GetTracker() const { return m_Tracker; }
		// Which barriers the next RT_Begin records with.
		enum class BarrierMode : uint8_t
		{
			// Renderer.ExplicitBarriers and Renderer.NRIGraphics decide.
			Settings,
			// NVRHI's automatic barriers, whatever the settings: for work the tracker cannot express
			// on NVRHI, such as readbacks into staging textures (no public state API; NRI Phase 13).
			Automatic,
			// The tracker's, whatever the settings: for work recorded with NRI.
			Explicit,
		};
		void SetBarrierMode(BarrierMode mode) { m_BarrierMode = mode; }

		// NRI recording inside this command buffer (NRI migration Phase 9, plan §2.2 I3/I4). Between
		// RT_BeginNRISegment and RT_EndNRISegment only NRI records, into a non-owning NRI wrapper of
		// the NVRHI command list's VkCommandBuffer. The segment starts with pending barriers committed
		// (barriers stay NVRHI's until Phase 13) and ends with NVRHI's cached state cleared, so NVRHI
		// rebinds its pipeline and sets after the segment. Render thread, between RT_Begin and RT_End.
		// Null, with no segment open, when NRI cannot wrap the command list (logged).
		nri::CommandBuffer* RT_BeginNRISegment();
		void RT_EndNRISegment();

		// NRI render passes (NRI migration Phase 10, Renderer.NRIGraphics). Render thread.
		//
		// A pass begun with RT_BeginNRIRenderPass records through NRI, in NRI rendering scopes inside
		// the NVRHI command list, until RT_EndNRIRenderPass. NVRHI may not record inside such a scope,
		// so every NVRHI entry point here (GetActive, the commits, timer queries) closes it first and
		// the next draw reopens it, with the attachments loaded and the pass state rebound: NVRHI's
		// implicit render pass splitting, made explicit. Barriers are the tracker's (NRI passes need
		// RT_UsesExplicitBarriers); pending ones are committed between scopes.
		struct NRIRenderPassDesc
		{
			std::array<nri::Descriptor*, nvrhi::c_MaxRenderTargets> ColorAttachments = {};
			// Clears happen when the pass opens; a reopened scope loads.
			std::array<bool, nvrhi::c_MaxRenderTargets> ClearColor = {};
			std::array<std::array<float, 4>, nvrhi::c_MaxRenderTargets> ClearColorValues = {};
			uint32_t ColorAttachmentCount = 0;
			nri::Descriptor* DepthAttachment = nullptr;
			bool ClearDepth = false;
			float ClearDepthValue = 0.0f;

			nri::PipelineLayout* PipelineLayout = nullptr;
			nri::Pipeline* Pipeline = nullptr;
			// The pass's descriptor sets by NRI set index; null ones are not bound by the pass.
			std::array<nri::DescriptorSet*, nvrhi::c_MaxBindingLayouts> DescriptorSets = {};
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
		// False, with nothing recorded, when NRI cannot record into this command list (logged).
		bool RT_BeginNRIRenderPass(const NRIRenderPassDesc& desc, std::string_view name, const nvrhi::ViewportState& viewport, const nvrhi::VariableRateShadingState& shadingRate);
		void RT_EndNRIRenderPass();
		bool RT_InNRIRenderPass() const;
		const NRIRenderPassDesc& RT_GetNRIRenderPass() const;
		std::string_view RT_GetNRIRenderPassName() const;
		void RT_SetNRIViewportState(const nvrhi::ViewportState& viewport);
		// Replaces the scissor rectangles with `scissor`, keeping the viewports.
		void RT_SetNRIScissor(const nvrhi::Rect& scissor);
		void RT_SetNRIShadingRate(const nvrhi::VariableRateShadingState& shadingRate);
		// Before a draw: commits pending barriers outside rendering, then (re)opens the rendering
		// scope. The command buffer to draw into, null when none can be opened (logged).
		nri::CommandBuffer* RT_BeginNRIDraw();
		// Bind for the next draws; repeated arguments are skipped. After RT_BeginNRIDraw.
		void RT_SetNRIDescriptorSet(uint32_t setIndex, nri::DescriptorSet* descriptorSet);
		void RT_SetNRIVertexBuffer(nri::Buffer* buffer, uint32_t stride);
		void RT_SetNRIIndexBuffer(nri::Buffer* buffer, bool use16BitIndices = false);
		void RT_SetNRIRootConstants(const void* data, uint32_t size);
		// The explicit-barrier requirements RT_CommitGraphicsState / RT_CommitMeshletState make,
		// without committing the state to NVRHI. No-ops with automatic barriers.
		void RT_RequireGraphicsState();
		void RT_RequireMeshletState(const nvrhi::MeshletState& meshletState);

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
		void RT_RequireBindingSets(const nvrhi::BindingSetVector& bindings, const nvrhi::BindingSetVector& committed);
		void RT_RequireFramebuffer(nvrhi::IFramebuffer* framebuffer);
		void RT_ForgetCommittedState();
		void RT_CrossCheckStates(const char* context);
		void DestroyNRIWrapper();
		nri::CommandBuffer* RT_OpenNRIRendering(bool passStart);
		void RT_CloseNRIRendering();
		// Closes the rendering scope because `reason` cannot be recorded inside it.
		void RT_SuspendNRIRendering(const char* reason);
	private:
		GPUQueue m_Queue = GPUQueue::Graphics;
		uint64_t m_LastExecutionInstance = 0;

		ResourceStateTracker m_Tracker;
		NVRHIBarrierEmitter m_BarrierEmitter;
		bool m_ExplicitBarriers = false;
		BarrierMode m_BarrierMode = BarrierMode::Settings;
		std::vector<const void*> m_RequirementLog;
		// Mirrors NVRHI's automatic-barrier change detection, so explicit mode emits the same
		// barriers: bound sets are re-required when they change, after a copy/clear/write requirement
		// (m_BindingStatesDirty; explicit transitions leave it alone, as NVRHI's setTextureState does),
		// and always when they hold UAV bindings (that re-require is what places UAV barriers between
		// dispatches). Raw pointers, compared and never dereferenced.
		bool m_BindingStatesDirty = true;
		// The NRI wrapper of the open command list (created by the first segment after RT_Begin,
		// destroyed by RT_End) and whether a segment is open.
		nri::CommandBuffer* m_NRICommandBuffer = nullptr;
		bool m_InNRISegment = false;
		// The NRI render pass (NRI types, so defined in the .cpp).
		struct NRIRenderState;
		Scope<NRIRenderState> m_NRIRender;
		nvrhi::GraphicsState m_CommittedGraphicsState;
		nvrhi::ComputeState m_CommittedComputeState;
		nvrhi::MeshletState m_CommittedMeshletState;

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

		nvrhi::GraphicsState m_GraphicsState;
		nvrhi::ComputeState m_ComputeState;

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
