// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Core/Ref.h"
#include "Lux/Debug/Profiler.h"

#include "PipelineSpecification.h"
#include "Lux/Renderer/RHI/DescriptorSetGroup.h"
#include "Lux/Renderer/RHI/NRIBarrierEmitter.h"
#include "Lux/Renderer/RHI/RHITypes.h"
#include "Lux/Renderer/RHI/StagingMemory.h"
#include "Lux/Renderer/RendererConfig.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <vulkan/vulkan.h>

#if LUX_ENABLE_PROFILING
#include <tracy/TracyVulkan.hpp>
#endif

namespace nri {
	struct Buffer;
	struct CommandBuffer;
	struct Descriptor;
	struct DescriptorSet;
	struct Fence;
	struct Pipeline;
	struct PipelineLayout;
	struct QueryPool;
	struct Rect;
	struct ShadingRateDesc;
	struct Viewport;
}

namespace Lux {

	class CommandBufferPool;
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
		// Submits what RT_End closed, after the uploads (Renderer::FlushResourceUploads), the
		// cross-queue waits added for its queue (Renderer::QueueWaitForCommandList) and
		// `waitSemaphore`, an NRI wrapper of a binary semaphore; optional. A command buffer that
		// draws to the back buffer passes RHISwapChain::TakeAcquireSemaphore().
		void RT_Submit(nri::Fence* waitSemaphore = nullptr);
		// Blocks until this command buffer's last submission has finished on the GPU.
		bool RT_WaitForCompletion();

		void RT_BeginMarker(const std::string& label);
		void RT_EndMarker();

		// Resource states (NRI migration Phase 4). The tracker places every barrier (NVRHI's automatic
		// barriers are off): NRI passes, draws and dispatches require what they bind, and code that
		// touches a resource outside them requires it first.
		//   RT_Require*:    a copy, clear or write; also re-requires the bindings of the next draw or
		//                   dispatch, as NVRHI did.
		//   RT_Transition*: a transition the code always needed (mip chains, compute->indirect).
		// Resources are named by their tracked descriptions (NRITexture/NRIBuffer::GetTracked,
		// ImageInfo::Tracked); empty ones are ignored.
		void RT_RequireTextureState(const TrackedTexture& texture, const TextureSubresourceRange& range, ResourceState state);
		void RT_RequireBufferState(const TrackedBuffer& buffer, ResourceState state);
		void RT_TransitionTextureState(const TrackedTexture& texture, const TextureSubresourceRange& range, ResourceState state);
		void RT_TransitionBufferState(const TrackedBuffer& buffer, ResourceState state);
		void RT_CommitBarriers();
		// Debug (render graph): collect the handles of every requirement until RT_EndRequirementLog.
		void RT_BeginRequirementLog();
		std::vector<const void*> RT_EndRequirementLog();
		const ResourceStateTracker& RT_GetTracker() const { return m_Tracker; }

		// Raw NRI recording into the open command buffer (NRI migration Phase 9). The segment starts
		// with pending barriers committed. Render thread, between RT_Begin and RT_End. Null, with no
		// segment open, when RT_Begin got no command buffer (logged by the pool).
		nri::CommandBuffer* RT_BeginNRISegment();
		void RT_EndNRISegment();
		// Copies and clears through NRI (NRI migration Phase 13), which cannot record inside a
		// rendering scope: closes an open one (the next draw reopens it),
		// then opens a segment as RT_BeginNRISegment. Require the states first; the segment commits
		// them. Null, with no segment open, as there.
		nri::CommandBuffer* RT_BeginNRITransfer();
		// Render thread, between RT_Begin and RT_End: stages `size` bytes of `data` and records their
		// copy into `buffer` at `offset`, with the buffer required as CopyDest. The staging memory is
		// recycled after this command buffer's submission.
		void RT_WriteBuffer(const NRIBuffer& buffer, const void* data, uint64_t size, uint64_t offset = 0);

		// NRI render passes (NRI migration Phase 10). Render thread.
		//
		// A pass begun with RT_BeginNRIRenderPass records in NRI rendering scopes until
		// RT_EndNRIRenderPass. Work that cannot record inside such a
		// scope (dispatches, copies, clears) closes it first and the next draw reopens
		// it, with the attachments loaded and the pass state rebound: NVRHI's implicit render pass
		// splitting, made explicit. Barriers are the tracker's; pending ones are committed between
		// scopes.
		struct NRIAttachment
		{
			nri::Descriptor* View = nullptr;
			// The texture and subresources the view covers, required as a render target (or depth
			// write) when the pass opens.
			TrackedTexture Texture;
			TextureSubresourceRange Range;
		};

		struct NRIRenderPassDesc
		{
			std::array<NRIAttachment, MaxRenderTargets> ColorAttachments = {};
			// Clears happen when the pass opens; a reopened scope loads.
			std::array<bool, MaxRenderTargets> ClearColor = {};
			std::array<std::array<float, 4>, MaxRenderTargets> ClearColorValues = {};
			uint32_t ColorAttachmentCount = 0;
			NRIAttachment DepthAttachment;
			bool ClearDepth = false;
			float ClearDepthValue = 0.0f;

			nri::PipelineLayout* PipelineLayout = nullptr;
			nri::Pipeline* Pipeline = nullptr;
			// The pass's descriptor sets by NRI set index, required and bound when the pass opens; empty
			// ones are not bound by the pass. Their groups are kept alive until the pass ends.
			std::array<BoundDescriptorSet, MaxDescriptorSets> DescriptorSets = {};
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
		// Viewport origin is top-left (NRI's default, the negative-height VkViewport); the shading
		// rate applies to pipelines with a dynamic one.
		bool RT_BeginNRIRenderPass(const NRIRenderPassDesc& desc, std::string_view name, const nri::Viewport& viewport, const nri::Rect& scissor, const nri::ShadingRateDesc& shadingRate);
		void RT_EndNRIRenderPass();
		// Replaces the open pass's pipeline, layout and descriptor sets, with the draw state that
		// comes with them, by `desc`'s (its attachment fields are ignored), keeping the attachments,
		// viewport and scissor. For a pass's variant pipelines (no culling, alpha tested) without
		// splitting its rendering scope: they must render to the pass's attachment formats.
		void RT_SwitchNRIRenderPassPipeline(const NRIRenderPassDesc& desc);
		bool RT_InNRIRenderPass() const;
		const NRIRenderPassDesc& RT_GetNRIRenderPass() const;
		std::string_view RT_GetNRIRenderPassName() const;
		// Replace the viewport (keeping the scissor rectangle), the scissor rectangle (keeping the
		// viewport) or the shading rate of the open pass.
		void RT_SetNRIViewport(const nri::Viewport& viewport);
		void RT_SetNRIScissor(const nri::Rect& scissor);
		void RT_SetNRIShadingRate(const nri::ShadingRateDesc& shadingRate);

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
			std::array<BoundDescriptorSet, MaxDescriptorSets> DescriptorSets = {};
			const void* RootConstants = nullptr;
			uint32_t RootConstantsSize = 0;
		};
		// Closes an open rendering scope, requires the sets as RT_BeginNRIDraw does, and opens an NRI
		// segment with the pipeline, sets and root constants bound. Record the dispatch into the
		// returned command buffer, then call RT_EndNRISegment. Null, with no segment open, when NRI
		// cannot wrap the command list (logged).
		nri::CommandBuffer* RT_BeginNRIDispatch(const NRIDispatchDesc& desc);


		// The queue this command buffer records/submits on (Graphics by default).
		GPUQueue GetQueue() const { return m_Queue; }
		// The RHIQueueTimeline value of the most recent submission on this buffer's
		// queue. Feed it to Renderer::QueueWaitForCommandList so another queue can
		// wait for this buffer's work to finish (cross-queue sync).
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
		void RT_RequireBuffer(const void*& required, const NRIBuffer* buffer, ResourceState state);
		// The open pass's attachments and sets.
		void RT_RequireNRIRenderPass();
		nri::CommandBuffer* RT_OpenNRIRendering(bool passStart);
		// Binds the pass state into the open rendering scope, forgetting what the draws bound.
		void RT_BindNRIPassState();
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
		NRIBarrierEmitter m_BarrierEmitter;
		// What RT_WriteBuffer copies from; retired at each RT_Submit.
		StagingStream m_Staging{ "Command buffer writes" };
		std::vector<const void*> m_RequirementLog;
		// What the last draws or dispatch required, for NVRHI's automatic-barrier change detection
		// (kept so the barriers stay the ones NVRHI placed): bindings are re-required when they change,
		// after a copy/clear/write requirement (m_BindingStatesDirty; explicit transitions leave it
		// alone, as NVRHI's explicit transitions did), and always when a set has storage uses (that
		// re-require is what places UAV barriers between dispatches). Raw pointers, compared and never
		// dereferenced; reset by RT_Begin.
		struct RequiredBindings
		{
			RequiredBindPoint BindPoint = RequiredBindPoint::None;
			std::array<nri::DescriptorSet*, MaxDescriptorSets> Sets = {};
			const void* VertexBuffer = nullptr;
			const void* IndexBuffer = nullptr;
			const void* IndirectArguments = nullptr;
		};
		RequiredBindings m_RequiredBindings;
		bool m_BindingStatesDirty = true;
		// The command buffer being recorded (between RT_Begin and RT_End; null when the pool failed)
		// and whether a segment is open.
		nri::CommandBuffer* m_NRICommandBuffer = nullptr;
		bool m_InNRISegment = false;
		// The NRI render pass (NRI types, so defined in the .cpp).
		struct NRIRenderState;
		Scope<NRIRenderState> m_NRIRender;

		// NRI command buffers (NRI migration Phase 13), recycled once their submission completes.
		// The slot count is the frame-in-flight slots the queries are kept for.
		Scope<CommandBufferPool> m_CommandBuffers;
		uint32_t m_SlotCount = 1;
		bool m_Recording = false;
		// Closed by RT_End, submitted by RT_Submit.
		nri::CommandBuffer* m_RecordedCommandBuffer = nullptr;
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

		bool m_QueryEnabled = false;

		std::string m_DebugName;


		// GPU timing and pipeline statistics (NRI migration Phase 13), one slot per command list. A
		// slot's pools are reset by its first RT_Begin of a render frame, every RT_End copies what
		// it wrote into Results, and the slot's next frame reads them back, once frame pacing has
		// retired that GPU work (hence one slot per frame in flight).
		static constexpr uint32_t k_MaxTimestamps = 2048;
		// Begin/End pairs per frame with pipeline statistics (Begin(true) splits a frame).
		static constexpr uint32_t k_MaxQuerySegments = 16;
		struct QuerySegment
		{
			uint32_t BeginTimestamp = 0;
			uint32_t EndTimestamp = 0;
			uint32_t Statistics = 0;
			bool Ended = false;
		};
		struct QueryTimer
		{
			std::string Name;
			uint32_t BeginTimestamp = 0;
			uint32_t EndTimestamp = 0;
		};
		struct QuerySlot
		{
			nri::QueryPool* Timestamps = nullptr;
			// Null without device support, and on non-graphics queues.
			nri::QueryPool* Statistics = nullptr;
			// HOST_READBACK: k_MaxTimestamps timestamps, then one statistics record per segment.
			nri::Buffer* Results = nullptr;
			// The render frame the queries belong to.
			uint64_t Frame = UINT64_MAX;
			uint32_t TimestampCount = 0;
			uint32_t CopiedTimestampCount = 0;
			uint32_t StatisticsCount = 0;
			std::vector<QuerySegment> Segments;
			std::vector<QueryTimer> Timers;
		};
		bool CreateQuerySlots(uint32_t count);
		void DestroyQuerySlots();
		void RT_BeginQuerySegment(QuerySlot& slot, bool continueFrame);
		void RT_EndQuerySegment(QuerySlot& slot);
		void RT_ResolveQueries(const QuerySlot& slot);

		std::vector<QuerySlot> m_QuerySlots;
		// The slot and segment of the open command list (null when it measures nothing).
		QuerySlot* m_ActiveQuerySlot = nullptr;
		QuerySegment* m_ActiveSegment = nullptr;
		uint64_t m_StatisticsSize = 0;
		bool m_ReportedTimerOverflow = false;
		// Open timers: name and begin timestamp (UINT32_MAX when not measured).
		std::vector<std::pair<std::string, uint32_t>> m_TimerQueryStack;
		std::unordered_map<std::string, float> m_NamedTimerQueryResults;

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

		// Same publishing rule as m_LastGPUWorkTime: written on the render thread when a frame's
		// queries are read back, read on the main thread. Atomic for
		// the same reason - these seven counters are read as a set, and a torn read
		// would report vertex and fragment totals from different frames.
		std::atomic<PipelineStatistics> m_LastPipelineStatistics = PipelineStatistics{};

	};
}
