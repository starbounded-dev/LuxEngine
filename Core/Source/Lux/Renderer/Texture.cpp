#include "lpch.h"
#include "Lux/Renderer/Texture.h"

#include "Lux/Renderer/Renderer.h"
#include "Platform/OpenGL/OpenGLTexture.h"

namespace Lux
{
	Ref<Texture2D> Texture2D::Create(const TextureSpecification& specification, Buffer data)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None:    LUX_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
		case RendererAPI::API::OpenGL:  return CreateRef<OpenGLTexture2D>(specification, data);
		}

		LUX_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

}
