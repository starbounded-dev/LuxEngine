// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "UniformBuffer.h"

#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RendererAPI.h"

namespace Lux {

	UniformBuffer::UniformBuffer(uint64_t size, std::string_view debugName)
		: m_Size(size), m_DebugName(debugName)
	{
		m_LocalData.Allocate(size);

		auto bufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setIsConstantBuffer(true)
			.setCpuAccess(nvrhi::CpuAccessMode::Write)
			.setInitialState(nvrhi::ResourceStates::ConstantBuffer)
			.setKeepInitialState(true) // enable fully automatic state tracking
			.setDebugName(m_DebugName.c_str());

		m_Buffer = NRIBuffer::Create(bufferDesc);
		LUX_CORE_VERIFY(m_Buffer, "Failed to create uniform buffer \"{}\" ({} bytes)", m_DebugName, size);
	}

	void UniformBuffer::SetData(Ref<RenderCommandBuffer> cmd, const void* data, uint64_t size, uint64_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		SetData(cmd, Buffer(data, size), offset);
	}

	void UniformBuffer::SetData(Ref<RenderCommandBuffer> cmd, Buffer buffer, uint64_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_LocalData.Write(buffer);

		Ref<UniformBuffer> instance = this;
		Renderer::Submit([instance, data = m_LocalData, offset, cmd]() mutable { instance->RT_SetData(cmd, data, offset); });
	}

	void UniformBuffer::RT_SetData(Ref<RenderCommandBuffer> cmd, const void* data, uint64_t size, uint64_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		RT_SetData(cmd, Buffer(data, size), offset);
	}

	void UniformBuffer::RT_SetData(Ref<RenderCommandBuffer> cmd, Buffer buffer, uint64_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (buffer.Size == 0)
			return;

		LUX_CORE_ASSERT(offset + buffer.Size <= m_Size);

		void* mappedBuffer = m_Buffer.Map(offset, buffer.Size);
		std::memcpy(mappedBuffer, buffer.Data, buffer.Size);
		m_Buffer.Unmap();
	}

}
