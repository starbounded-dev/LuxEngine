#include "sepch.h"
#include "IndexBuffer.h"

#include "StarEngine/Core/Application.h"

namespace StarEngine {
	
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

		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
		m_Handle = device->createBuffer(indexBufferDesc);

		m_CommandList = RenderCommandBuffer::Create(1, "IndexBuffer");
		m_CommandList->RT_Begin();
		m_CommandList->GetActive()->writeBuffer(m_Handle, buffer.Data, buffer.Size);
		m_CommandList->RT_End();
		m_CommandList->RT_Submit();
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

		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
		m_Handle = device->createBuffer(indexBufferDesc);
	}

	void IndexBuffer::SetData(void* buffer, uint64_t size, uint64_t offset)
	{
		SE_CORE_VERIFY(false, "Not implemented!");
	}

}
