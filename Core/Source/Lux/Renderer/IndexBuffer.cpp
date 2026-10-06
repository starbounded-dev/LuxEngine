// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "IndexBuffer.h"

#include "Lux/Core/Application.h"
#include "Lux/Renderer/Renderer.h"

namespace Lux {

	IndexBuffer::IndexBuffer(const Buffer buffer)
		: m_Size(buffer.Size)
	{
		// TODO(Yan): render thread? if yes then we need to copy to m_LocalData...

		auto indexBufferDesc = nvrhi::BufferDesc()
			.setByteSize(buffer.Size)
			.setIsIndexBuffer(true)
			.setInitialState(nvrhi::ResourceStates::IndexBuffer)
			.setKeepInitialState(true) // enable fully automatic state tracking
			.setDebugName("IndexBuffer");

		m_Buffer = NRIBuffer::Create(indexBufferDesc);
		LUX_CORE_VERIFY(m_Buffer, "Failed to create a {} byte index buffer", buffer.Size);

		// Shared upload batch — see VertexBuffer(Buffer) for rationale.
		Renderer::RecordResourceUpload([&](nvrhi::ICommandList* uploadList)
		{
			uploadList->writeBuffer(m_Buffer.GetHandle(), buffer.Data, buffer.Size);
		});
	}

	IndexBuffer::IndexBuffer(uint64_t size)
		: m_Size(size)
	{
		auto indexBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setIsIndexBuffer(true)
			.setInitialState(nvrhi::ResourceStates::IndexBuffer)
			.setKeepInitialState(true) // enable fully automatic state tracking
			.setDebugName("IndexBuffer");

		m_Buffer = NRIBuffer::Create(indexBufferDesc);
		LUX_CORE_VERIFY(m_Buffer, "Failed to create a {} byte index buffer", size);
	}

	void IndexBuffer::SetData(void* buffer, uint64_t size, uint64_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_VERIFY(false, "Not implemented!");
	}

}
