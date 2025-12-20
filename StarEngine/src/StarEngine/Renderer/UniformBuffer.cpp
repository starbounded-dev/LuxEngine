#include "sepch.h"
#include "UniformBuffer.h"

#include "StarEngine/Renderer/Renderer.h"
#include "StarEngine/Renderer/RendererAPI.h"

namespace StarEngine {

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

		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
		m_Handle = device->createBuffer(bufferDesc);
	}
	
	void UniformBuffer::SetData(const void* data, uint64_t size, uint64_t offset)
	{
		SetData(Buffer(data, size), offset);
	}

	void UniformBuffer::SetData(Buffer buffer, uint64_t offset)
	{
		m_LocalData.Write(buffer);

		Ref<UniformBuffer> instance = this;
		Renderer::Submit([instance, data = m_LocalData, offset]() mutable { instance->RT_SetData(data, offset); });
	}

	void UniformBuffer::RT_SetData(const void* data, uint64_t size, uint64_t offset)
	{
		RT_SetData(Buffer(data, size), offset);
	}

	void UniformBuffer::RT_SetData(Buffer buffer, uint64_t offset)
	{
		if (buffer.Size == 0)
			return;

		if (!m_CommandList)
			m_CommandList = RenderCommandBuffer::Create(1, "StorageBuffer");

		m_CommandList->RT_Begin();
		m_CommandList->GetActive()->writeBuffer(m_Handle, buffer.Data, buffer.Size, offset);
		m_CommandList->RT_End();
		m_CommandList->RT_Submit();
	}

}
