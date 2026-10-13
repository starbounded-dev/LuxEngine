// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "UploadContext.h"

#include "Lux/Renderer/RHI/RHIResources.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Renderer/RHI/StagingMemory.h"

namespace Lux {

	namespace {
		// vkCmdCopyBuffer takes any offset; 16 keeps staged buffer data vector-aligned.
		constexpr uint64_t k_BufferCopyAlignment = 16;
	}

	UploadContext::UploadContext(nri::CommandBuffer& commandBuffer, ResourceStateTracker& tracker, IBarrierEmitter& emitter, StagingStream& staging)
		: m_CommandBuffer(commandBuffer), m_Tracker(tracker), m_Emitter(emitter), m_Staging(staging)
	{
	}

	void UploadContext::WriteBuffer(const NRIBuffer& buffer, const void* data, uint64_t size, uint64_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!buffer || !data || size == 0)
			return;

		const StagingStream::Allocation source = m_Staging.Write(data, size, k_BufferCopyAlignment);
		if (!source)
			return;

		m_Tracker.Require(buffer.GetTracked(), ResourceState::CopyDest);
		m_Tracker.Commit();
		RHIDevice::API().CmdCopyBuffer(m_CommandBuffer, *buffer.Get(), offset, *source.Buffer, source.Offset, size);
	}

	void UploadContext::WriteTexture(const TrackedTexture& texture, uint32_t layer, uint32_t mip, const void* data, uint64_t rowPitch)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		nri::Texture* nriTexture = static_cast<nri::Texture*>(texture.Handle);
		if (!nriTexture)
			return;

		StagedTextureData staged;
		if (!StageTextureData(m_Staging, *nriTexture, layer, mip, data, rowPitch, staged))
			return;

		m_Tracker.Require(texture, { mip, 1, layer, 1 }, ResourceState::CopyDest);
		m_Tracker.Commit();
		CmdUploadStagedTexture(m_CommandBuffer, *nriTexture, staged);
	}

	void UploadContext::InitializeTexture(const TrackedTexture& texture)
	{
		// Textures without a resting state are not tracked (the tracker logs them on first use).
		if (!texture.Handle || texture.RestingState == ResourceState::Unknown)
			return;

		// Straight to the emitter, committed alone: the tracker never sees the undefined state, and
		// a later barrier on the texture must not share this one's barrier batch.
		m_Emitter.TransitionTexture(texture, { 0, texture.MipCount, 0, texture.LayerCount }, ResourceState::Unknown, texture.RestingState);
		m_Emitter.Commit();
	}

}
