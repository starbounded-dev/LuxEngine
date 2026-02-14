#pragma once

#include "RendererTypes.h"

#include "Lux/Core/Log.h"
#include "Lux/Core/Buffer.h"

#include "RenderCommandBuffer.h"

#include "nvrhi/nvrhi.h"

namespace Lux {

	enum class VertexBufferUsage
	{
		None = 0, Static = 1, Dynamic = 2
	};

	class VertexBuffer : public RefCounted
	{
	public:
		static Ref<VertexBuffer> Create(const Buffer buffer) { return Ref<VertexBuffer>::Create(buffer); }
		static Ref<VertexBuffer> Create(uint64_t size) { return Ref<VertexBuffer>::Create(size); }

		void SetData(Buffer buffer, uint64_t offset = 0);
		void SetData(const void* data, uint64_t size, uint64_t offset = 0);

		void RT_SetData(Buffer buffer, uint64_t offset = 0);
		void RT_SetData(const void* data, uint64_t size, uint64_t offset = 0);

		uint64_t GetSize() const { return m_Size; }
		nvrhi::BufferHandle GetHandle() const { return m_Handle; }
	public:
		VertexBuffer(const Buffer buffer);
		VertexBuffer(uint64_t size);

		virtual ~VertexBuffer() = default;
	private:
		nvrhi::BufferHandle m_Handle;
		Ref<RenderCommandBuffer> m_CommandList;

		uint64_t m_Size = 0;
		Buffer m_LocalData; // unused for now
	};



}
