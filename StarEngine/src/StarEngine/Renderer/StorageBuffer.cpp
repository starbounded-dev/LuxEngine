#include "sepch.h"
#include "StorageBuffer.h"

#include "StarEngine/Renderer/Renderer.h"

namespace StarEngine {

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

	void StorageBuffer::SetData(Buffer buffer, uint32_t offset)
	{
		m_LocalStorage.Write(buffer);

		Ref<StorageBuffer> instance = this;
		Renderer::Submit([instance, offset]() mutable
			{
				instance->RT_SetData(instance->m_LocalStorage, offset);
			});
	}

	void StorageBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
	{
		SetData(Buffer(data, size), offset);
	}

	void StorageBuffer::RT_SetData(Buffer buffer, uint32_t offset)
	{
		if (!m_CommandList)
			m_CommandList = RenderCommandBuffer::Create(1, "StorageBuffer");

		m_CommandList->RT_Begin();
		m_CommandList->GetActive()->writeBuffer(m_Handle, buffer.Data, buffer.Size, offset);
		m_CommandList->RT_End();
		m_CommandList->RT_Submit();
	}

	void StorageBuffer::RT_SetData(const void* data, uint32_t size, uint32_t offset)
	{
		RT_SetData(Buffer(data, size), offset);
	}

	void StorageBuffer::Resize(uint32_t size)
	{
		m_BufferDesc.setByteSize(size);
		Invalidate();
	}

}
