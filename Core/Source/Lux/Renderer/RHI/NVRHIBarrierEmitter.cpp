// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "NVRHIBarrierEmitter.h"

#include "Lux/Renderer/RHI/NVRHIInterop.h"

namespace Lux {

	void NVRHIBarrierEmitter::RequireTexture(const TrackedTexture& texture, const TextureSubresourceRange& range, ResourceState state)
	{
		m_CommandList->setTextureState(static_cast<nvrhi::ITexture*>(texture.Handle), ToNVRHI(range), ToNVRHI(state));
	}

	void NVRHIBarrierEmitter::RequireBuffer(const TrackedBuffer& buffer, ResourceState state)
	{
		m_CommandList->setBufferState(static_cast<nvrhi::IBuffer*>(buffer.Handle), ToNVRHI(state));
	}

	void NVRHIBarrierEmitter::Commit()
	{
		m_CommandList->commitBarriers();
	}

	bool NVRHIBarrierEmitter::QueryTextureState(const TrackedTexture& texture, uint32_t mip, uint32_t layer, ResourceState& outState) const
	{
		outState = FromNVRHI(m_CommandList->getTextureSubresourceState(static_cast<nvrhi::ITexture*>(texture.Handle), layer, mip));
		return true;
	}

	bool NVRHIBarrierEmitter::QueryBufferState(const TrackedBuffer& buffer, ResourceState& outState) const
	{
		outState = FromNVRHI(m_CommandList->getBufferState(static_cast<nvrhi::IBuffer*>(buffer.Handle)));
		return true;
	}

	TrackedTexture DescribeTexture(nvrhi::ITexture* texture)
	{
		TrackedTexture tracked;
		if (!texture)
			return tracked;

		const nvrhi::TextureDesc& desc = texture->getDesc();
		tracked.Handle = texture;
		tracked.MipCount = glm::max(1u, desc.mipLevels);
		tracked.LayerCount = glm::max(1u, desc.arraySize);
		tracked.RestingState = desc.keepInitialState ? FromNVRHI(desc.initialState) : ResourceState::Unknown;
		return tracked;
	}

	TrackedBuffer DescribeBuffer(nvrhi::IBuffer* buffer)
	{
		TrackedBuffer tracked;
		if (!buffer)
			return tracked;

		const nvrhi::BufferDesc& desc = buffer->getDesc();
		tracked.Handle = buffer;
		// NVRHI never transitions volatile or CPU-visible buffers.
		tracked.Untracked = desc.isVolatile || desc.cpuAccess != nvrhi::CpuAccessMode::None;
		const bool modelled = !tracked.Untracked && desc.keepInitialState;
		tracked.RestingState = modelled ? FromNVRHI(desc.initialState) : ResourceState::Unknown;
		return tracked;
	}

}
