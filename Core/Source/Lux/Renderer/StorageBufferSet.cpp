#include "lpch.h"
#include "StorageBufferSet.h"

#include "Lux/Renderer/Renderer.h"

namespace Lux {

	StorageBufferSet::StorageBufferSet(const StorageBufferSpecification& specification, uint32_t size, uint32_t framesInFlight)
		: m_Specification(specification), m_FramesInFlight(framesInFlight)
	{
		if (framesInFlight == 0)
			m_FramesInFlight = Renderer::GetConfig().FramesInFlight;

		for (uint32_t frame = 0; frame < m_FramesInFlight; frame++)
			m_StorageBuffers[frame] = StorageBuffer::Create(size, specification);
	}

	void StorageBufferSet::Resize(uint32_t newSize)
	{
		for (uint32_t frame = 0; frame < m_FramesInFlight; frame++)
			m_StorageBuffers.at(frame)->Resize(newSize);
	}

	Ref<StorageBuffer> StorageBufferSet::Get()
	{
		uint32_t frame = Renderer::GetCurrentFrameIndex();
		return Get(frame);
	}

	Ref<StorageBuffer> StorageBufferSet::RT_Get()
	{
		uint32_t frame = Renderer::RT_GetCurrentFrameIndex();
		return Get(frame);
	}

	Ref<StorageBuffer> StorageBufferSet::Get(uint32_t frame)
	{
		LUX_CORE_ASSERT(m_StorageBuffers.find(frame) != m_StorageBuffers.end());
		return m_StorageBuffers.at(frame);
	}

	void StorageBufferSet::Set(Ref<StorageBuffer> storageBuffer, uint32_t frame)
	{
		m_StorageBuffers[frame] = storageBuffer;
	}

}
