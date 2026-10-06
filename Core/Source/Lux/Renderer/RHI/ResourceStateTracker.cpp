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

		// Same layout as NVRHI's calcSubresource, so per-subresource comparisons line up.
		uint32_t SubresourceIndex(const TrackedTexture& texture, uint32_t mip, uint32_t layer)
		{
			return mip + layer * texture.MipCount;
		}

		// Render thread only. Process-wide so a recurring mismatch is reported once, not every frame.
		std::unordered_set<const void*> s_ReportedMismatches;

	}

	void ResourceStateTracker::Begin(IBarrierEmitter* emitter)
	{
		m_Emitter = emitter;
		m_Textures.clear();
		m_Buffers.clear();
		m_TexturesToCheck.clear();
		m_BuffersToCheck.clear();
		m_Stats = {};
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
		m_TexturesToCheck.clear();
		m_BuffersToCheck.clear();
	}

	void ResourceStateTracker::Require(const TrackedTexture& texture, TextureSubresourceRange range, ResourceState state)
	{
		if (!texture.Handle || !m_Emitter)
			return;

		m_Stats.Requirements++;
		m_Emitter->RequireTexture(texture, range, state);

		// Without a resting state the starting state is unknown, so the backend owns the bookkeeping.
		if (texture.RestingState == ResourceState::Unknown)
			return;

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
			if (entry.Whole != state || IsUnorderedAccess(state))
				m_Stats.Transitions++;
			entry.Whole = state;
		}
		else
		{
			if (entry.Subresources.empty())
				entry.Subresources.assign(static_cast<size_t>(texture.MipCount) * texture.LayerCount, entry.Whole);

			for (uint32_t layer = baseLayer; layer < baseLayer + layerCount; layer++)
			{
				for (uint32_t mip = baseMip; mip < baseMip + mipCount; mip++)
				{
					ResourceState& current = entry.Subresources[SubresourceIndex(texture, mip, layer)];
					if (current != state || IsUnorderedAccess(state))
						m_Stats.Transitions++;
					current = state;
				}
			}
		}

		m_TexturesToCheck.push_back(texture.Handle);
	}

	void ResourceStateTracker::Require(const TrackedBuffer& buffer, ResourceState state)
	{
		if (!buffer.Handle || !m_Emitter)
			return;

		m_Stats.Requirements++;
		m_Emitter->RequireBuffer(buffer, state);

		if (buffer.RestingState == ResourceState::Unknown)
			return;

		auto [it, inserted] = m_Buffers.try_emplace(buffer.Handle);
		BufferEntry& entry = it->second;
		if (inserted)
		{
			entry.Info = buffer;
			entry.State = buffer.RestingState;
		}

		if (entry.State != state || IsUnorderedAccess(state))
			m_Stats.Transitions++;
		entry.State = state;

		m_BuffersToCheck.push_back(buffer.Handle);
	}

	void ResourceStateTracker::Commit()
	{
		if (m_Emitter)
			m_Emitter->Commit();
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

	void ResourceStateTracker::CrossCheck(const char* context)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_Emitter)
			return;

		for (const void* handle : m_TexturesToCheck)
		{
			auto it = m_Textures.find(handle);
			if (it == m_Textures.end() || s_ReportedMismatches.contains(handle))
				continue;

			const TextureEntry& entry = it->second;
			for (uint32_t layer = 0; layer < entry.Info.LayerCount; layer++)
			{
				bool mismatch = false;
				for (uint32_t mip = 0; mip < entry.Info.MipCount; mip++)
				{
					ResourceState backendState;
					if (!m_Emitter->QueryTextureState(entry.Info, mip, layer, backendState))
						break;

					const ResourceState trackedState = GetSubresourceState(entry, mip, layer);
					if (backendState != trackedState)
					{
						LUX_CORE_ERROR_TAG("Renderer", "Tracker/NVRHI state mismatch in {}: texture {} mip {} layer {} is 0x{:X} in the tracker, 0x{:X} in NVRHI",
							context, handle, mip, layer, static_cast<uint32_t>(trackedState), static_cast<uint32_t>(backendState));
						s_ReportedMismatches.insert(handle);
						mismatch = true;
						break;
					}
				}
				if (mismatch)
					break;
			}
		}

		for (const void* handle : m_BuffersToCheck)
		{
			auto it = m_Buffers.find(handle);
			if (it == m_Buffers.end() || s_ReportedMismatches.contains(handle))
				continue;

			ResourceState backendState;
			if (!m_Emitter->QueryBufferState(it->second.Info, backendState))
				continue;

			if (backendState != it->second.State)
			{
				LUX_CORE_ERROR_TAG("Renderer", "Tracker/NVRHI state mismatch in {}: buffer {} is 0x{:X} in the tracker, 0x{:X} in NVRHI",
					context, handle, static_cast<uint32_t>(it->second.State), static_cast<uint32_t>(backendState));
				s_ReportedMismatches.insert(handle);
			}
		}

		m_TexturesToCheck.clear();
		m_BuffersToCheck.clear();
	}

}
