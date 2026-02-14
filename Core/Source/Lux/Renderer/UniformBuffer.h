#pragma once

#include "Lux/Core/Ref.h"
#include "Lux/Core/Buffer.h"

#include "RenderCommandBuffer.h"

#include "nvrhi/nvrhi.h"

namespace Lux {

	class UniformBuffer : public RefCounted
	{
	public:
		static Ref<UniformBuffer> Create(uint64_t size, std::string_view debugName = "UniformBuffer")
		{
			return Ref<UniformBuffer>::Create(size, debugName);
		}

		nvrhi::BufferHandle GetHandle() const { return m_Handle; }

		void SetData(Ref<RenderCommandBuffer> cmd, Buffer buffer, uint64_t offset = 0);
		void SetData(Ref<RenderCommandBuffer> cmd, const void* data, uint64_t size, uint64_t offset = 0);

		void RT_SetData(Ref<RenderCommandBuffer> cmd, Buffer buffer, uint64_t offset = 0);
		void RT_SetData(Ref<RenderCommandBuffer> cmd, const void* data, uint64_t size, uint64_t offset = 0);
	public:
		UniformBuffer(uint64_t size, std::string_view debugName = "UniformBuffer");
		virtual ~UniformBuffer() = default;
	private:
		uint64_t m_Size = 0;
		std::string m_DebugName;

		nvrhi::BufferHandle m_Handle;

		Buffer m_LocalData;
	};

}
