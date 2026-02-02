#include "lpch.h"
#include "Lux/Renderer/Buffer.h"

#include "Lux/Renderer/Renderer.h"

#include "Platform/OpenGL/OpenGLBuffer.h"

namespace Lux {

	Ref<VertexBuffer> VertexBuffer::Create(uint32_t size)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None:	LUX_CORE_ASSERT(false, "Renderer API::None is currently not supported"); return nullptr;
			case RendererAPI::API::OpenGL:	return CreateRef<OpenGLVertexBuffer>(size);
		}

		LUX_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

	Ref<VertexBuffer> VertexBuffer::Create(float* vertices, uint32_t size) {

		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None:	LUX_CORE_ASSERT(false, "RendererAPI::None is currently not supported") return nullptr;
			case RendererAPI::API::OpenGL:  return CreateRef<OpenGLVertexBuffer>(vertices, size);
		}

		LUX_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

	Ref<IndexBuffer> IndexBuffer::Create(uint32_t* indices, uint32_t size) {

		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None:	LUX_CORE_ASSERT(false, "RendererAPI::None is currently not supported") return nullptr;
			case RendererAPI::API::OpenGL:  return CreateRef<OpenGLIndexBuffer>(indices, size);
		}

		LUX_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
};