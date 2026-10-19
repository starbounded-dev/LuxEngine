// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "NRIBarrierEmitter.h"

#include "Lux/Renderer/RHI/RHIDevice.h"

#include <atomic>

namespace Lux {

	namespace {

		struct StateMapping
		{
			nri::AccessBits Access = nri::AccessBits::NONE;
			nri::Layout Layout = nri::Layout::UNDEFINED;
			// ALL waits on (or halts) every command, NONE on none.
			nri::StageBits Stages = nri::StageBits::NONE;
		};

		// One state bit, as NVRHI's Vulkan backend maps it: shader, constant-buffer and present states
		// use all commands. NRI's COPY stage covers copies only, not the clears and fills
		// (vkCmdClearColorImage, vkCmdFillBuffer, vkCmdUpdateBuffer) NVRHI's transfer stage did, so the
		// copy states use all commands too. Storage images are cleared in UnorderedAccess
		// (CmdClearStorage, a transfer write on Vulkan), so that state adds CLEAR_STORAGE access.
		StateMapping MapStateBit(ResourceState bit)
		{
			using Access = nri::AccessBits;
			using Layout = nri::Layout;
			using Stages = nri::StageBits;
			switch (bit)
			{
				case ResourceState::Common:				return { Access::NONE, Layout::UNDEFINED, Stages::NONE };
				case ResourceState::ConstantBuffer:		return { Access::CONSTANT_BUFFER, Layout::UNDEFINED, Stages::ALL };
				case ResourceState::VertexBuffer:		return { Access::VERTEX_BUFFER, Layout::UNDEFINED, Stages::VERTEX_SHADER };
				case ResourceState::IndexBuffer:		return { Access::INDEX_BUFFER, Layout::UNDEFINED, Stages::INDEX_INPUT };
				case ResourceState::IndirectArgument:	return { Access::ARGUMENT_BUFFER, Layout::UNDEFINED, Stages::INDIRECT };
				case ResourceState::ShaderResource:		return { Access::SHADER_RESOURCE, Layout::SHADER_RESOURCE, Stages::ALL };
				case ResourceState::UnorderedAccess:	return { Access::SHADER_RESOURCE_STORAGE | Access::CLEAR_STORAGE, Layout::SHADER_RESOURCE_STORAGE, Stages::ALL };
				case ResourceState::RenderTarget:		return { Access::COLOR_ATTACHMENT, Layout::COLOR_ATTACHMENT, Stages::COLOR_ATTACHMENT };
				case ResourceState::DepthWrite:			return { Access::DEPTH_STENCIL_ATTACHMENT, Layout::DEPTH_STENCIL_ATTACHMENT, Stages::DEPTH_STENCIL_ATTACHMENT };
				case ResourceState::DepthRead:			return { Access::DEPTH_STENCIL_ATTACHMENT_READ, Layout::DEPTH_STENCIL_READONLY, Stages::DEPTH_STENCIL_ATTACHMENT };
				case ResourceState::CopyDest:			return { Access::COPY_DESTINATION, Layout::COPY_DESTINATION, Stages::ALL };
				case ResourceState::CopySource:			return { Access::COPY_SOURCE, Layout::COPY_SOURCE, Stages::ALL };
				case ResourceState::ResolveDest:		return { Access::RESOLVE_DESTINATION, Layout::RESOLVE_DESTINATION, Stages::RESOLVE };
				case ResourceState::ResolveSource:		return { Access::RESOLVE_SOURCE, Layout::RESOLVE_SOURCE, Stages::RESOLVE };
				case ResourceState::Present:			return { Access::NONE, Layout::PRESENT, Stages::ALL };
				case ResourceState::ShadingRateSurface:	return { Access::SHADING_RATE_ATTACHMENT, Layout::SHADING_RATE_ATTACHMENT, Stages::SHADING_RATE_ATTACHMENT };
				default:
					// Stream-out, acceleration-structure, micromap and cooperative-vector states: Lux uses none.
					LUX_CORE_ASSERT(false, "Resource state 0x{:X} has no NRI mapping", static_cast<uint32_t>(bit));
					return {};
			}
		}

		// A state that may combine several bits: their access and stages together, and the layout of
		// the highest bit that has one (as NVRHI's convertResourceState). Present as a destination has
		// no access and no stages (the release semaphore orders it).
		nri::AccessLayoutStage MapState(ResourceState state, bool destination)
		{
			uint32_t access = 0;
			uint32_t stages = 0;
			bool allStages = false;
			nri::Layout layout = nri::Layout::UNDEFINED;
			for (uint32_t bits = static_cast<uint32_t>(state); bits != 0; bits &= bits - 1)
			{
				const StateMapping mapping = MapStateBit(static_cast<ResourceState>(bits & (~bits + 1)));
				access |= static_cast<uint32_t>(mapping.Access);
				if (mapping.Stages == nri::StageBits::ALL)
					allStages = true;
				else if (mapping.Stages != nri::StageBits::NONE)
					stages |= static_cast<uint32_t>(mapping.Stages);
				if (mapping.Layout != nri::Layout::UNDEFINED)
					layout = mapping.Layout;
			}

			if (destination && layout == nri::Layout::PRESENT)
				return { nri::AccessBits::NONE, nri::Layout::PRESENT, nri::StageBits::NONE };

			const nri::StageBits stageBits = allStages ? nri::StageBits::ALL : (stages ? static_cast<nri::StageBits>(stages) : nri::StageBits::NONE);
			return { static_cast<nri::AccessBits>(access), layout, stageBits };
		}

		// What a copy-queue barrier may name (vkCmdPipelineBarrier2 on a transfer-only family): copy
		// accesses only, and ALL stages, which Vulkan expands to that queue's transfer stages.
		nri::AccessLayoutStage RestrictToCopyQueue(nri::AccessLayoutStage state)
		{
			constexpr uint32_t copyAccess = static_cast<uint32_t>(nri::AccessBits::COPY_SOURCE) | static_cast<uint32_t>(nri::AccessBits::COPY_DESTINATION);
			state.access = static_cast<nri::AccessBits>(static_cast<uint32_t>(state.access) & copyAccess);
			if (state.stages != nri::StageBits::NONE)
				state.stages = nri::StageBits::ALL;
			return state;
		}

		// Reported once, so a broken frame is visible without flooding the log.
		std::atomic<bool> s_ReportedMissingCommandBuffer = false;

	}

	NRIBarrierEmitter::NRIBarrierEmitter() = default;
	NRIBarrierEmitter::~NRIBarrierEmitter() = default;

	void NRIBarrierEmitter::TransitionTexture(const TrackedTexture& texture, const TextureSubresourceRange& range, ResourceState before, ResourceState after)
	{
		nri::AccessLayoutStage source = MapState(before, false);
		nri::AccessLayoutStage destination = MapState(after, true);
		if (m_Queue == GPUQueue::Copy)
		{
			source = RestrictToCopyQueue(source);
			destination = RestrictToCopyQueue(destination);
		}

		nri::TextureBarrierDesc& barrier = m_Textures.emplace_back();
		barrier.texture = static_cast<nri::Texture*>(texture.Handle);
		barrier.before = source;
		barrier.after = destination;
		barrier.mipOffset = static_cast<nri::Dim_t>(range.BaseMip);
		barrier.mipNum = static_cast<nri::Dim_t>(range.MipCount);
		barrier.layerOffset = static_cast<nri::Dim_t>(range.BaseLayer);
		barrier.layerNum = static_cast<nri::Dim_t>(range.LayerCount);
		barrier.planes = nri::PlaneBits::ALL;
	}

	void NRIBarrierEmitter::TransitionBuffer(const TrackedBuffer& buffer, ResourceState before, ResourceState after)
	{
		nri::AccessLayoutStage source = MapState(before, false);
		nri::AccessLayoutStage destination = MapState(after, true);
		if (m_Queue == GPUQueue::Copy)
		{
			source = RestrictToCopyQueue(source);
			destination = RestrictToCopyQueue(destination);
		}

		nri::BufferBarrierDesc& barrier = m_Buffers.emplace_back();
		barrier.buffer = static_cast<nri::Buffer*>(buffer.Handle);
		barrier.before = { source.access, source.stages };
		barrier.after = { destination.access, destination.stages };
	}

	void NRIBarrierEmitter::Commit()
	{
		if (m_Textures.empty() && m_Buffers.empty())
			return;

		if (!m_CommandBuffer)
		{
			if (!s_ReportedMissingCommandBuffer.exchange(true))
				LUX_CORE_ERROR_TAG("Renderer", "NRI barriers dropped: the command buffer has no NRI command buffer");
		}
		else
		{
			nri::BarrierDesc barrierDesc = {};
			barrierDesc.textures = m_Textures.data();
			barrierDesc.textureNum = static_cast<uint32_t>(m_Textures.size());
			barrierDesc.buffers = m_Buffers.data();
			barrierDesc.bufferNum = static_cast<uint32_t>(m_Buffers.size());
			RHIDevice::API().CmdBarrier(*m_CommandBuffer, barrierDesc);
		}

		m_Textures.clear();
		m_Buffers.clear();
	}

}
