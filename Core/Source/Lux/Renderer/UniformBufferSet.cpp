#include "lpch.h"
#include "UniformBufferSet.h"

#include "Lux/Renderer/Renderer.h"

namespace Lux {

	UniformBufferSet::UniformBufferSet(uint32_t size, uint32_t framesInFlight)
		: m_FramesInFlight(framesInFlight)
	{
		if (framesInFlight == 0)
			m_FramesInFlight = Renderer::GetConfig().FramesInFlight;

		for (uint32_t frame = 0; frame < m_FramesInFlight; frame++)
			m_UniformBuffers[frame] = UniformBuffer::Create(size);
	}

	Ref<UniformBuffer> UniformBufferSet::Get()
	{
		uint32_t frame = Renderer::GetCurrentFrameIndex();
		return Get(frame);
	}

	Ref<UniformBuffer> UniformBufferSet::RT_Get()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		uint32_t frame = Renderer::RT_GetCurrentFrameIndex();
		return Get(frame);
	}

	Ref<UniformBuffer> UniformBufferSet::Get(uint32_t frame)
	{
		LUX_CORE_ASSERT(m_UniformBuffers.find(frame) != m_UniformBuffers.end());
		return m_UniformBuffers.at(frame);
	}

	void UniformBufferSet::Set(Ref<UniformBuffer> uniformBuffer, uint32_t frame)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_UniformBuffers[frame] = uniformBuffer;
	}

	uint64_t UniformBufferSet::GetAllocatedSize() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		uint64_t size = 0;
		for (const auto& [frame, uniformBuffer] : m_UniformBuffers)
		{
			if (uniformBuffer)
				size += uniformBuffer->GetSize();
		}
		return size;
	}

}
