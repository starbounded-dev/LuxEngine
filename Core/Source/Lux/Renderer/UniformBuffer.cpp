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

		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();
		m_Handle = device->createBuffer(bufferDesc);
	}

	void UniformBuffer::SetData(Ref<RenderCommandBuffer> cmd, const void* data, uint64_t size, uint64_t offset)
	{
		SetData(cmd, Buffer(data, size), offset);
	}

	void UniformBuffer::SetData(Ref<RenderCommandBuffer> cmd, Buffer buffer, uint64_t offset)
	{
		m_LocalData.Write(buffer);

		Ref<UniformBuffer> instance = this;
		Renderer::Submit([instance, data = m_LocalData, offset, cmd]() mutable { instance->RT_SetData(cmd, data, offset); });
	}

	void UniformBuffer::RT_SetData(Ref<RenderCommandBuffer> cmd, const void* data, uint64_t size, uint64_t offset)
	{
		RT_SetData(cmd, Buffer(data, size), offset);
	}

	void UniformBuffer::RT_SetData(Ref<RenderCommandBuffer> cmd, Buffer buffer, uint64_t offset)
	{
		if (buffer.Size == 0)
			return;

		cmd->GetActive()->writeBuffer(m_Handle, buffer.Data, buffer.Size, offset);

	}

}
