#pragma once

#include "StarEngine/Core/Ref.h"
#include "StarEngine/Core/Buffer.h"

#include "RendererTypes.h"
#include "RenderCommandBuffer.h"

#include "nvrhi/nvrhi.h"

namespace StarEngine {

	class IndexBuffer : public RefCounted
	{
	public:
		static Ref<IndexBuffer> Create(const Buffer buffer) { return Ref<IndexBuffer>::Create(buffer); }
		static Ref<IndexBuffer> Create(uint64_t size) { return Ref<IndexBuffer>::Create(size); }

		void SetData(void* buffer, uint64_t size, uint64_t offset = 0);

		uint64_t GetSize() const { return m_Size; }
		uint32_t GetCount() const { return (uint32_t)(m_Size / sizeof(uint32_t)); }

		nvrhi::BufferHandle GetHandle() const { return m_Handle; }
	public:
		IndexBuffer(const Buffer buffer);
		IndexBuffer(uint64_t size);

		virtual ~IndexBuffer() = default;
	private:
		nvrhi::BufferHandle m_Handle;
		Ref<RenderCommandBuffer> m_CommandList;

		uint64_t m_Size = 0;
	};

}

