// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/RHI/RHITypes.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Lux {

	// A texture as the tracker sees it, described once when the texture is created (NRITexture,
	// swapchain images). Handle is the nri::Texture*; only the barrier emitter dereferences it.
	struct TrackedTexture
	{
		void* Handle = nullptr;
		uint32_t MipCount = 1;
		uint32_t LayerCount = 1;
		// The state the texture is in between command buffers. Every texture has one: a texture
		// without it cannot be tracked (logged, its requirements ignored).
		ResourceState RestingState = ResourceState::Unknown;
	};

	// Described once when the buffer is created (NRIBuffer). Handle is the nri::Buffer*.
	struct TrackedBuffer
	{
		void* Handle = nullptr;
		// Unknown for an untracked buffer.
		ResourceState RestingState = ResourceState::Unknown;
		// CPU-visible: written through Map and read straight from host memory, never transitioned
		// (the queue submit makes host writes visible), so requiring it needs no barrier.
		bool Untracked = false;
	};

	// Records the transitions the tracker works out (NRIBarrierEmitter). `before` is the state the
	// subresources are in now; `range` is resolved (no AllMips / AllLayers).
	class IBarrierEmitter
	{
	public:
		virtual ~IBarrierEmitter() = default;

		virtual void TransitionTexture(const TrackedTexture& texture, const TextureSubresourceRange& range, ResourceState before, ResourceState after) = 0;
		virtual void TransitionBuffer(const TrackedBuffer& buffer, ResourceState before, ResourceState after) = 0;
		virtual void Commit() = 0;
	};

	// Per command buffer: which state every resource touched so far is in, at subresource
	// granularity once a partial range has been required. Every resource starts the command buffer
	// in its resting state and End() returns it there, so the next command buffer, on any queue,
	// starts from resting states again.
	//
	// UnorderedAccess -> UnorderedAccess counts as a transition (a UAV barrier), as in NVRHI.
	//
	// Threading: render thread only, owned by one RenderCommandBuffer.
	class ResourceStateTracker
	{
	public:
		struct Stats
		{
			uint32_t Requirements = 0;
			uint32_t Transitions = 0;
		};

		// Starts a command buffer. The model is empty: resources enter it at their resting state.
		void Begin(IBarrierEmitter* emitter);
		// Returns every modelled resource to its resting state, commits, and forgets the model.
		void End();

		void Require(const TrackedTexture& texture, TextureSubresourceRange range, ResourceState state);
		void Require(const TrackedBuffer& buffer, ResourceState state);
		void Commit();
		// True when a requirement since the last Commit made a transition. NRI render passes commit
		// those outside their rendering scopes (NRI migration Phase 10).
		bool HasPendingBarriers() const { return m_PendingBarriers; }

		// Debug: while set, every required texture/buffer handle is appended to `log` (the render
		// graph checks a pass's requirements against its declared accesses). Survives Begin().
		void SetRequirementLog(std::vector<const void*>* log) { m_RequirementLog = log; }

		// Unknown when the resource is not modelled in this command buffer.
		ResourceState GetState(const void* textureHandle, uint32_t mip, uint32_t layer) const;
		ResourceState GetState(const void* bufferHandle) const;
		const Stats& GetStats() const { return m_Stats; }

	private:
		struct TextureEntry
		{
			TrackedTexture Info;
			ResourceState Whole = ResourceState::Unknown;
			// Empty while the whole texture shares one state; mip-major per layer otherwise.
			std::vector<ResourceState> Subresources;
		};

		struct BufferEntry
		{
			TrackedBuffer Info;
			ResourceState State = ResourceState::Unknown;
		};

		ResourceState GetSubresourceState(const TextureEntry& entry, uint32_t mip, uint32_t layer) const;

	private:
		IBarrierEmitter* m_Emitter = nullptr;
		std::vector<const void*>* m_RequirementLog = nullptr;
		std::unordered_map<const void*, TextureEntry> m_Textures;
		std::unordered_map<const void*, BufferEntry> m_Buffers;
		Stats m_Stats;
		bool m_PendingBarriers = false;
	};

}
