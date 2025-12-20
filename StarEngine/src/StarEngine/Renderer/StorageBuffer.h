#pragma once

#include "StarEngine/Core/Ref.h"
#include "StarEngine/Core/Buffer.h"

#include "RenderCommandBuffer.h"

#include "nvrhi/nvrhi.h"

namespace StarEngine {

	struct StorageBufferSpecification
	{
		bool GPUOnly = true;
		std::string DebugName = "StorageBuffer";
	};

	class StorageBuffer : public RefCounted
	{
	public:
		static Ref<StorageBuffer> Create(uint32_t size, const StorageBufferSpecification& specification)
			{ return Ref<StorageBuffer>::Create(size, specification); }

		void Invalidate();

		nvrhi::BufferHandle GetHandle() const { return m_Handle; }

		void SetData(Buffer buffer, uint32_t offset = 0);
		void SetData(const void* data, uint32_t size, uint32_t offset = 0);
		void RT_SetData(Buffer buffer, uint32_t offset = 0);
		void RT_SetData(const void* data, uint32_t size, uint32_t offset = 0);
		void Resize(uint32_t size);
	public:
		StorageBuffer(uint32_t size, const StorageBufferSpecification& specification);
		~StorageBuffer() = default;
	private:
		StorageBufferSpecification m_Specification;

		nvrhi::BufferHandle m_Handle;
		nvrhi::BufferDesc m_BufferDesc;

		Ref<RenderCommandBuffer> m_CommandList;

		Buffer m_LocalStorage;
	};

}
