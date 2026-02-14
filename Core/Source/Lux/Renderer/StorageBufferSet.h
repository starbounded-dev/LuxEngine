#pragma once

#include "StorageBuffer.h"

namespace Lux {

	class StorageBufferSet : public RefCounted
	{
	public:
		static Ref<StorageBufferSet> Create(const StorageBufferSpecification& specification, uint32_t size, uint32_t framesInFlight = 0)
		{
			return Ref<StorageBufferSet>::Create(specification, size, framesInFlight);
		}

		void Resize(uint32_t newSize);

		Ref<StorageBuffer> Get();
		Ref<StorageBuffer> Get(uint32_t frame);
		Ref<StorageBuffer> RT_Get();

		void Set(Ref<StorageBuffer> storageBuffer, uint32_t frame = 0);
	public:
		StorageBufferSet(const StorageBufferSpecification& specification, uint32_t size, uint32_t framesInFlight);
		virtual ~StorageBufferSet() = default;
	private:
		StorageBufferSpecification m_Specification;
		uint32_t m_FramesInFlight = 0;
		std::map<uint32_t, Ref<StorageBuffer>> m_StorageBuffers;
	};

}
