// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "StorageBuffer.h"

#include "Lux/Renderer/Renderer.h"

namespace Lux {

	StorageBuffer::StorageBuffer(uint32_t size, const StorageBufferSpecification& specification)
		: m_Specification(specification)
	{
		m_BufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setCanHaveRawViews(true)
			.setCanHaveUAVs(true)
			.setIsDrawIndirectArgs(m_Specification.DrawIndirect)
			.setInitialState(nvrhi::ResourceStates::UnorderedAccess)
			.setKeepInitialState(true) // enable fully automatic state tracking
			.setCpuAccess(m_Specification.GPUOnly ? nvrhi::CpuAccessMode::None : nvrhi::CpuAccessMode::Write)
			.setDebugName(m_Specification.DebugName);

		Invalidate();
	}

	void StorageBuffer::Invalidate()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// Replacing m_Buffer hands the old one to the GPU deletion queue.
		m_Buffer = NRIBuffer::Create(m_BufferDesc);
		LUX_CORE_VERIFY(m_Buffer, "Failed to create storage buffer \"{}\" ({} bytes)", m_Specification.DebugName, m_BufferDesc.byteSize);

		if (!m_Specification.GPUOnly)
			m_LocalStorage.Reallocate(m_BufferDesc.byteSize);
	}

	void StorageBuffer::SetData(Ref<RenderCommandBuffer> cmd, Buffer buffer, uint32_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_LocalStorage.Write(buffer);

		Ref<StorageBuffer> instance = this;
		Renderer::Submit([instance, offset, cmd]() mutable
			{
				instance->RT_SetData(cmd, instance->m_LocalStorage, offset);
			});
	}

	void StorageBuffer::SetData(Ref<RenderCommandBuffer> cmd, const void* data, uint32_t size, uint32_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		SetData(cmd, Buffer(data, size), offset);
	}

	void StorageBuffer::RT_SetData(Ref<RenderCommandBuffer> cmd, Buffer buffer, uint32_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (buffer.Size == 0)
			return;

		LUX_CORE_ASSERT(offset + buffer.Size <= m_BufferDesc.byteSize);

		if (m_Specification.GPUOnly)
		{
			cmd->RT_RequireBufferState(m_Buffer.GetHandle(), ResourceState::CopyDest);
			cmd->GetActive()->writeBuffer(m_Buffer.GetHandle(), buffer.Data, buffer.Size, offset);
			return;
		}

		void* mappedBuffer = m_Buffer.Map(offset, buffer.Size);
		std::memcpy(mappedBuffer, buffer.Data, buffer.Size);
		m_Buffer.Unmap();
	}

	void StorageBuffer::RT_SetData(Ref<RenderCommandBuffer> cmd, const void* data, uint32_t size, uint32_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		RT_SetData(cmd, Buffer(data, size), offset);
	}

	void StorageBuffer::Resize(uint32_t size)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_BufferDesc.setByteSize(size);
		Invalidate();
	}

}
