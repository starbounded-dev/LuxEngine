// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ResourceStateTracker.h"

#include <unordered_set>

namespace Lux {

	namespace {

		bool IsUnorderedAccess(ResourceState state)
		{
			return (state & ResourceState::UnorderedAccess) != 0;
		}

		// Mip-major per layer.
		uint32_t SubresourceIndex(const TrackedTexture& texture, uint32_t mip, uint32_t layer)
		{
			return mip + layer * texture.MipCount;
		}

		// A transition is needed to change state, and from UnorderedAccess to itself (the UAV barrier
		// between two writes).
		bool NeedsTransition(ResourceState current, ResourceState state)
		{
			return current != state || IsUnorderedAccess(state);
		}

		// Render thread only. Process-wide so a recurring problem is reported once, not every frame.
		std::unordered_set<const void*> s_ReportedUntrackable;

		void ReportUntrackable(const void* handle, const char* kind)
		{
			if (s_ReportedUntrackable.insert(handle).second)
				LUX_CORE_ERROR_TAG("Renderer", "Resource state tracker: {} {} has no resting state; its requirements are ignored", kind, handle);
		}

	}

	void ResourceStateTracker::Begin(IBarrierEmitter* emitter)
	{
		m_Emitter = emitter;
		m_Textures.clear();
		m_Buffers.clear();
		m_Stats = {};
		m_PendingBarriers = false;
	}

	void ResourceStateTracker::End()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// Copy out first: Require below updates the maps being walked.
		std::vector<TrackedTexture> textures;
		textures.reserve(m_Textures.size());
		for (const auto& [handle, entry] : m_Textures)
			textures.push_back(entry.Info);

		std::vector<TrackedBuffer> buffers;
		buffers.reserve(m_Buffers.size());
		for (const auto& [handle, entry] : m_Buffers)
			buffers.push_back(entry.Info);

		for (const TrackedTexture& texture : textures)
			Require(texture, AllSubresources, texture.RestingState);
		for (const TrackedBuffer& buffer : buffers)
			Require(buffer, buffer.RestingState);

		Commit();

		m_Textures.clear();
		m_Buffers.clear();
	}

	void ResourceStateTracker::Require(const TrackedTexture& texture, TextureSubresourceRange range, ResourceState state)
	{
		if (!texture.Handle || !m_Emitter)
			return;

		m_Stats.Requirements++;
		if (m_RequirementLog)
			m_RequirementLog->push_back(texture.Handle);

		// Without a resting state the starting state is unknown.
		if (texture.RestingState == ResourceState::Unknown)
		{
			ReportUntrackable(texture.Handle, "texture");
			return;
		}

		const uint32_t baseMip = glm::min(range.BaseMip, texture.MipCount);
		const uint32_t mipCount = range.MipCount == TextureSubresourceRange::AllMips ? texture.MipCount - baseMip : glm::min(range.MipCount, texture.MipCount - baseMip);
		const uint32_t baseLayer = glm::min(range.BaseLayer, texture.LayerCount);
		const uint32_t layerCount = range.LayerCount == TextureSubresourceRange::AllLayers ? texture.LayerCount - baseLayer : glm::min(range.LayerCount, texture.LayerCount - baseLayer);
		if (mipCount == 0 || layerCount == 0)
			return;

		auto [it, inserted] = m_Textures.try_emplace(texture.Handle);
		TextureEntry& entry = it->second;
		if (inserted)
		{
			entry.Info = texture;
			entry.Whole = texture.RestingState;
		}

		const bool wholeTexture = baseMip == 0 && mipCount == texture.MipCount && baseLayer == 0 && layerCount == texture.LayerCount;
		if (wholeTexture && entry.Subresources.empty())
		{
			if (NeedsTransition(entry.Whole, state))
			{
				m_Emitter->TransitionTexture(entry.Info, { 0, texture.MipCount, 0, texture.LayerCount }, entry.Whole, state);
				m_Stats.Transitions++;
				m_PendingBarriers = true;
			}
			entry.Whole = state;
			return;
		}

		if (entry.Subresources.empty())
			entry.Subresources.assign(static_cast<size_t>(texture.MipCount) * texture.LayerCount, entry.Whole);

		// One transition per run of mips of a layer that are in the same state.
		for (uint32_t layer = baseLayer; layer < baseLayer + layerCount; layer++)
		{
			uint32_t mip = baseMip;
			while (mip < baseMip + mipCount)
			{
				const ResourceState before = entry.Subresources[SubresourceIndex(texture, mip, layer)];
				uint32_t runEnd = mip + 1;
				while (runEnd < baseMip + mipCount && entry.Subresources[SubresourceIndex(texture, runEnd, layer)] == before)
					runEnd++;

				if (NeedsTransition(before, state))
				{
					m_Emitter->TransitionTexture(entry.Info, { mip, runEnd - mip, layer, 1 }, before, state);
					m_Stats.Transitions++;
					m_PendingBarriers = true;
				}
				for (uint32_t runMip = mip; runMip < runEnd; runMip++)
					entry.Subresources[SubresourceIndex(texture, runMip, layer)] = state;
				mip = runEnd;
			}
		}
	}

	void ResourceStateTracker::Require(const TrackedBuffer& buffer, ResourceState state)
	{
		if (!buffer.Handle || !m_Emitter)
			return;

		m_Stats.Requirements++;
		if (m_RequirementLog)
			m_RequirementLog->push_back(buffer.Handle);

		if (buffer.Untracked)
			return;
		if (buffer.RestingState == ResourceState::Unknown)
		{
			ReportUntrackable(buffer.Handle, "buffer");
			return;
		}

		auto [it, inserted] = m_Buffers.try_emplace(buffer.Handle);
		BufferEntry& entry = it->second;
		if (inserted)
		{
			entry.Info = buffer;
			entry.State = buffer.RestingState;
		}

		if (NeedsTransition(entry.State, state))
		{
			m_Emitter->TransitionBuffer(entry.Info, entry.State, state);
			m_Stats.Transitions++;
			m_PendingBarriers = true;
		}
		entry.State = state;
	}

	void ResourceStateTracker::Commit()
	{
		if (m_Emitter)
			m_Emitter->Commit();
		m_PendingBarriers = false;
	}

	ResourceState ResourceStateTracker::GetSubresourceState(const TextureEntry& entry, uint32_t mip, uint32_t layer) const
	{
		if (entry.Subresources.empty())
			return entry.Whole;
		if (mip >= entry.Info.MipCount || layer >= entry.Info.LayerCount)
			return ResourceState::Unknown;
		return entry.Subresources[SubresourceIndex(entry.Info, mip, layer)];
	}

	ResourceState ResourceStateTracker::GetState(const void* textureHandle, uint32_t mip, uint32_t layer) const
	{
		auto it = m_Textures.find(textureHandle);
		return it != m_Textures.end() ? GetSubresourceState(it->second, mip, layer) : ResourceState::Unknown;
	}

	ResourceState ResourceStateTracker::GetState(const void* bufferHandle) const
	{
		auto it = m_Buffers.find(bufferHandle);
		return it != m_Buffers.end() ? it->second.State : ResourceState::Unknown;
	}

}
