// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/RHI/RHITypes.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Lux {

	// A texture as the tracker sees it. Handle is the backend object (nvrhi::ITexture* until NRI
	// Phase 13); only the barrier emitter dereferences it.
	struct TrackedTexture
	{
		void* Handle = nullptr;
		uint32_t MipCount = 1;
		uint32_t LayerCount = 1;
		// The state the texture is in between command buffers. Unknown when the backend tracks the
		// texture's state itself (no resting state): requirements are still forwarded, not modelled.
		ResourceState RestingState = ResourceState::Unknown;
	};

	struct TrackedBuffer
	{
		void* Handle = nullptr;
		// Unknown for buffers whose state is not tracked at all (volatile or CPU-visible).
		ResourceState RestingState = ResourceState::Unknown;
	};

	// Turns requirements into backend barriers. NVRHIBarrierEmitter until NRI Phase 13.
	class IBarrierEmitter
	{
	public:
		virtual ~IBarrierEmitter() = default;

		virtual void RequireTexture(const TrackedTexture& texture, const TextureSubresourceRange& range, ResourceState state) = 0;
		virtual void RequireBuffer(const TrackedBuffer& buffer, ResourceState state) = 0;
		virtual void Commit() = 0;

		// Debug cross-check: the backend's own view of a state, when it keeps one.
		virtual bool QueryTextureState(const TrackedTexture& texture, uint32_t mip, uint32_t layer, ResourceState& outState) const { return false; }
		virtual bool QueryBufferState(const TrackedBuffer& buffer, ResourceState& outState) const { return false; }
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

		// Unknown when the resource is not modelled in this command buffer.
		ResourceState GetState(const void* textureHandle, uint32_t mip, uint32_t layer) const;
		ResourceState GetState(const void* bufferHandle) const;
		const Stats& GetStats() const { return m_Stats; }

		// Debug: compares the model with the emitter's backend state for every resource touched since
		// the last check and logs the first mismatch per resource.
		void CrossCheck(const char* context);

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
		std::unordered_map<const void*, TextureEntry> m_Textures;
		std::unordered_map<const void*, BufferEntry> m_Buffers;
		std::vector<const void*> m_TexturesToCheck;
		std::vector<const void*> m_BuffersToCheck;
		Stats m_Stats;
	};

}
