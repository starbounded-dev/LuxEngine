#include "sepch.h"
#include "UniformBufferSet.h"

#include "StarEngine/Renderer/Renderer.h"

namespace StarEngine {
	
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
		uint32_t frame = Renderer::RT_GetCurrentFrameIndex();
		return Get(frame);
	}

	Ref<UniformBuffer> UniformBufferSet::Get(uint32_t frame)
	{
		SE_CORE_ASSERT(m_UniformBuffers.find(frame) != m_UniformBuffers.end());
		return m_UniformBuffers.at(frame);
	}

	void UniformBufferSet::Set(Ref<UniformBuffer> uniformBuffer, uint32_t frame)
	{
		m_UniformBuffers[frame] = uniformBuffer;
	}

	
}
