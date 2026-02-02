#include "lpch.h"
#include "Lux/Renderer/VertexArray.h"

#include "Lux/Renderer/Renderer.h"
#include "Platform/OpenGL/OpenGLVertexArray.h"

namespace Lux {

	Ref<VertexArray> VertexArray::Create() {

		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None:	LUX_CORE_ASSERT(false, "RendererAPI::None is currently not supported") return nullptr;
			case RendererAPI::API::OpenGL:  return CreateRef<OpenGLVertexArray>();
		}

		LUX_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}