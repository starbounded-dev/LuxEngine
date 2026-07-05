#include "lpch.h"
#include "VertexBuffer.h"

#include "Lux/Core/Application.h"
#include "Lux/Renderer/Renderer.h"

namespace Lux {

	VertexBuffer::VertexBuffer(const Buffer buffer)
		: m_Size(buffer.Size)
	{
		// TODO(Yan): render thread? if yes then we need to copy to m_LocalData...

		auto vertexBufferDesc = nvrhi::BufferDesc()
			.setByteSize(buffer.Size)
			.setIsVertexBuffer(true)
			.setInitialState(nvrhi::ResourceStates::VertexBuffer)
			.setKeepInitialState(true) // enable fully automatic state tracking
			.setDebugName("VertexBuffer");

		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
		m_Handle = device->createBuffer(vertexBufferDesc);

		// Record into the shared upload batch instead of creating and submitting
		// a dedicated command list per buffer (one vkQueueSubmit per mesh causes
		// load hitches, and the list used to be retained for the buffer's whole
		// lifetime).
		Renderer::RecordResourceUpload([&](nvrhi::ICommandList* uploadList)
		{
			uploadList->writeBuffer(m_Handle, buffer.Data, buffer.Size);
		});
	}

	VertexBuffer::VertexBuffer(uint64_t size)
		: m_Size(size)
	{
		m_LocalData.Allocate(size);

		auto vertexBufferDesc = nvrhi::BufferDesc()
			.setByteSize(size)
			.setIsVertexBuffer(true)
			.setInitialState(nvrhi::ResourceStates::VertexBuffer)
			.setKeepInitialState(true) // enable fully automatic state tracking
			.setCpuAccess(nvrhi::CpuAccessMode::Write)
			.setDebugName("VertexBuffer");

		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
		m_Handle = device->createBuffer(vertexBufferDesc);
	}

	void VertexBuffer::SetData(Buffer buffer, uint64_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (buffer.Size == 0)
			return;

		if (!m_CommandList)
			m_CommandList = RenderCommandBuffer::Create(1, "VertexBuffer");

		//m_CommandList->RT_Begin();
		//m_CommandList->GetActive()->writeBuffer(m_Handle, buffer.Data, buffer.Size, offset);
		auto device = Application::GetGraphicsDevice();
		void* mappedBuffer = device->mapBuffer(m_Handle, nvrhi::CpuAccessMode::Write);
		memcpy(mappedBuffer, (uint8_t*)buffer.Data + offset, buffer.Size);
		device->unmapBuffer(m_Handle);
		//m_CommandList->RT_End();
		//m_CommandList->RT_Submit();
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

		m_CommandList->RT_Begin();
		m_CommandList->GetActive()->writeBuffer(m_Handle, buffer.Data, buffer.Size);
		m_CommandList->RT_End();
		m_CommandList->RT_Submit();
	}

	void VertexBuffer::RT_SetData(const void* data, uint64_t size, uint64_t offset)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		RT_SetData(Buffer(data, size), offset);
	}

}
