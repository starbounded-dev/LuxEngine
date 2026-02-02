#include "lpch.h"
#include "Lux/Renderer/Framebuffer.h"

#include "Lux/Renderer/Renderer.h"
#include "Platform/OpenGL/OpenGLFramebuffer.h"

namespace Lux
{
	Ref<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None:    LUX_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
			case RendererAPI::API::OpenGL:  return CreateRef<OpenGLFramebuffer>(spec);
		}

		LUX_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}
