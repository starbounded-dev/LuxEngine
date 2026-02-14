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
			.setInitialState(nvrhi::ResourceStates::UnorderedAccess)
			.setKeepInitialState(true) // enable fully automatic state tracking
			.setCpuAccess(m_Specification.GPUOnly ? nvrhi::CpuAccessMode::None : nvrhi::CpuAccessMode::Write)
			.setDebugName(m_Specification.DebugName);

		Invalidate();
	}

	void StorageBuffer::Invalidate()
	{
		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
		m_Handle = device->createBuffer(m_BufferDesc);

		if (!m_Specification.GPUOnly)
			m_LocalStorage.Reallocate(m_BufferDesc.byteSize);
	}

	void StorageBuffer::SetData(Ref<RenderCommandBuffer> cmd, Buffer buffer, uint32_t offset)
	{
		m_LocalStorage.Write(buffer);

		Ref<StorageBuffer> instance = this;
		Renderer::Submit([instance, offset, cmd]() mutable
			{
				instance->RT_SetData(cmd, instance->m_LocalStorage, offset);
			});
	}

	void StorageBuffer::SetData(Ref<RenderCommandBuffer> cmd, const void* data, uint32_t size, uint32_t offset)
	{
		SetData(cmd, Buffer(data, size), offset);
	}

	void StorageBuffer::RT_SetData(Ref<RenderCommandBuffer> cmd, Buffer buffer, uint32_t offset)
	{
		if (buffer.Size == 0)
			return;

		cmd->GetActive()->writeBuffer(m_Handle, buffer.Data, buffer.Size, offset);
	}

	void StorageBuffer::RT_SetData(Ref<RenderCommandBuffer> cmd, const void* data, uint32_t size, uint32_t offset)
	{
		RT_SetData(cmd, Buffer(data, size), offset);
	}

	void StorageBuffer::Resize(uint32_t size)
	{
		m_BufferDesc.setByteSize(size);
		Invalidate();
	}

}
