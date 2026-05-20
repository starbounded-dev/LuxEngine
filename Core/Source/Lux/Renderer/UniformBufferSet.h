#pragma once

#include "UniformBuffer.h"

namespace Lux {

	class UniformBufferSet : public RefCounted
	{
	public:
		static Ref<UniformBufferSet> Create(uint32_t size, uint32_t framesInFlight = 0) { return Ref<UniformBufferSet>::Create(size, framesInFlight); }

		Ref<UniformBuffer> Get();
		Ref<UniformBuffer> RT_Get();

		Ref<UniformBuffer> Get(uint32_t frame);

		void Set(Ref<UniformBuffer> uniformBuffer, uint32_t frame = 0);
		uint64_t GetAllocatedSize() const;
		uint32_t GetBufferCount() const { return static_cast<uint32_t>(m_UniformBuffers.size()); }
	public:
		UniformBufferSet(uint32_t size, uint32_t framesInFlight);
		virtual ~UniformBufferSet() = default;
	private:
		uint32_t m_FramesInFlight = 0;
		std::map<uint32_t, Ref<UniformBuffer>> m_UniformBuffers;
	};

}
