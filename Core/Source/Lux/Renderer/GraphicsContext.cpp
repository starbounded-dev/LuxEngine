#include "lpch.h"
#include "Lux/Renderer/GraphicsContext.h"

#include "Lux/Renderer/Renderer.h"
#include "Platform/OpenGL/OpenGLContext.h"

namespace Lux
{
	Scope<GraphicsContext> GraphicsContext::Create(void* window)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None:    LUX_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
		case RendererAPI::API::OpenGL:  return CreateScope<OpenGLContext>(static_cast<GLFWwindow*>(window));
		}
		LUX_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}