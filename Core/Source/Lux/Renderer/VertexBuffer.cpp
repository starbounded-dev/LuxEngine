// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "VertexBuffer.h"

#include "Lux/Core/Application.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/UploadContext.h"

namespace Lux {

	VertexBuffer::VertexBuffer(const Buffer buffer)
		: m_Size(buffer.Size)
	{
		// TODO(Yan): render thread? if yes then we need to copy to m_LocalData...

		RHIBufferDesc vertexBufferDesc;
		vertexBufferDesc.Size = buffer.Size;
		vertexBufferDesc.IsVertexBuffer = true;
		// Raw SRV so the mesh-shader path can fetch vertices directly.
		vertexBufferDesc.CanHaveRawViews = true;
		vertexBufferDesc.RestingState = ResourceState::VertexBuffer;
		vertexBufferDesc.DebugName = "VertexBuffer";

		m_Buffer = NRIBuffer::Create(vertexBufferDesc);
		LUX_CORE_VERIFY(m_Buffer, "Failed to create a {} byte vertex buffer", buffer.Size);

		// Record into the shared upload batch instead of creating and submitting
		// a dedicated command list per buffer (one vkQueueSubmit per mesh causes
		// load hitches, and the list used to be retained for the buffer's whole
		// lifetime).
		Renderer::RecordResourceUpload([&](UploadContext& upload)
		{
			upload.WriteBuffer(m_Buffer, buffer.Data, buffer.Size);
		});
	}

	VertexBuffer::VertexBuffer(uint64_t size)
		: m_Size(size)
	{
		m_LocalData.Allocate(size);

		RHIBufferDesc vertexBufferDesc;
		vertexBufferDesc.Size = size;
		vertexBufferDesc.IsVertexBuffer = true;
		vertexBufferDesc.Access = CPUAccess::Write;
		vertexBufferDesc.DebugName = "VertexBuffer";

		m_Buffer = NRIBuffer::Create(vertexBufferDesc);
		LUX_CORE_VERIFY(m_Buffer, "Failed to create a {} byte vertex buffer", size);
	}

	void VertexBuffer::SetData(Buffer buffer, uint64_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (buffer.Size == 0)
			return;

		LUX_CORE_ASSERT(offset + buffer.Size <= m_Size);
		void* mappedBuffer = m_Buffer.Map(offset, buffer.Size);
		std::memcpy(mappedBuffer, buffer.Data, buffer.Size);
		m_Buffer.Unmap();
	}

	void VertexBuffer::SetData(const void* data, uint64_t size, uint64_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		SetData(Buffer(data, size), offset);
	}

	void VertexBuffer::RT_SetData(Buffer buffer, uint64_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (buffer.Size == 0)
			return;

		if (!m_CommandList)
			m_CommandList = RenderCommandBuffer::Create(1, "VertexBuffer");

		LUX_CORE_ASSERT(offset + buffer.Size <= m_Size);
		m_CommandList->RT_Begin();
		m_CommandList->RT_WriteBuffer(m_Buffer, buffer.Data, buffer.Size, offset);
		m_CommandList->RT_End();
		m_CommandList->RT_Submit();
	}

	void VertexBuffer::RT_SetData(const void* data, uint64_t size, uint64_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		RT_SetData(Buffer(data, size), offset);
	}

}
