// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "TextureReadback.h"

#include "Lux/Renderer/RenderCommandBuffer.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Renderer/RHI/StagingMemory.h"

#include <cstring>
#include <vector>

namespace Lux {

	void ReadbackTexture(RenderCommandBuffer& commandBuffer, const TrackedTexture& texture, std::span<const TextureSubresource> subresources, Buffer& buffer, std::string_view debugName)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		buffer.Release();
		nri::Texture* nriTexture = static_cast<nri::Texture*>(texture.Handle);
		if (!nriTexture || subresources.empty())
			return;

		// Where each subresource lands in the readback buffer, and how big the packed result is.
		std::vector<TextureCopyLayout> layouts(subresources.size());
		std::vector<uint64_t> offsets(subresources.size());
		uint64_t readbackSize = 0;
		uint64_t packedSize = 0;
		for (size_t i = 0; i < subresources.size(); i++)
		{
			if (!GetTextureCopyLayout(*nriTexture, subresources[i].Mip, subresources[i].Layer, layouts[i]))
				return;
			offsets[i] = (readbackSize + layouts[i].OffsetAlignment - 1) / layouts[i].OffsetAlignment * layouts[i].OffsetAlignment;
			readbackSize = offsets[i] + layouts[i].GetSize();
			packedSize += layouts[i].GetPackedSize();
		}

		const NRIInterface& api = RHIDevice::API();
		nri::BufferDesc readbackDesc = {};
		readbackDesc.size = readbackSize;
		nri::Buffer* readbackBuffer = nullptr;
		if (api.CreateCommittedBuffer(RHIDevice::Get(), nri::MemoryLocation::HOST_READBACK, 0.0f, readbackDesc, readbackBuffer) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "'{}' is not read back: NRI could not create a {} byte readback buffer", debugName, readbackSize);
			return;
		}
		api.SetDebugName(readbackBuffer, "Texture readback");

		commandBuffer.RT_Begin();
		for (const TextureSubresource& subresource : subresources)
			commandBuffer.RT_RequireTextureState(texture, { subresource.Mip, 1, subresource.Layer, 1 }, ResourceState::CopySource);
		nri::CommandBuffer* nriCommandBuffer = commandBuffer.RT_BeginNRITransfer();
		if (nriCommandBuffer)
		{
			for (size_t i = 0; i < layouts.size(); i++)
				CmdReadbackTexture(*nriCommandBuffer, *readbackBuffer, offsets[i], *nriTexture, layouts[i]);

			// A fence wait does not make device writes visible to the host; this barrier does. The
			// tracker has no host state, and NRI takes host access only in global barriers.
			nri::GlobalBarrierDesc toHost = {};
			toHost.before = { nri::AccessBits::COPY_DESTINATION, nri::StageBits::COPY };
			toHost.after = { nri::AccessBits::HOST_READ, nri::StageBits::HOST };
			nri::BarrierDesc barrierDesc = {};
			barrierDesc.globals = &toHost;
			barrierDesc.globalNum = 1;
			api.CmdBarrier(*nriCommandBuffer, barrierDesc);
			commandBuffer.RT_EndNRISegment();
		}
		commandBuffer.RT_End();
		commandBuffer.RT_Submit();

		if (nriCommandBuffer && commandBuffer.RT_WaitForCompletion())
		{
			const uint8_t* source = static_cast<const uint8_t*>(api.MapBuffer(*readbackBuffer, 0, nri::WHOLE_SIZE));
			buffer.Allocate(packedSize);
			uint8_t* destination = buffer.As<uint8_t>();
			for (size_t i = 0; i < layouts.size(); i++)
			{
				const TextureCopyLayout& layout = layouts[i];
				const uint8_t* rows = source + offsets[i];
				for (uint32_t row = 0; row < layout.RowCount * layout.SliceCount; row++)
				{
					std::memcpy(destination, rows + static_cast<size_t>(row) * layout.RowPitch, layout.RowSize);
					destination += layout.RowSize;
				}
			}
			api.UnmapBuffer(*readbackBuffer);
		}
		else
		{
			LUX_CORE_ERROR_TAG("Renderer", "'{}' is not read back: {}", debugName, nriCommandBuffer ? "the GPU wait failed" : "its command buffer has no NRI command buffer");
		}

		// Idle once waited for; freed with the other GPU objects.
		Renderer::SubmitResourceFree([readbackBuffer]()
			{
				RHIDevice::API().DestroyBuffer(readbackBuffer);
			});
	}

}
