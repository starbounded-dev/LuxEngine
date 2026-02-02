#include "sepch.h"
#include "StarEngine/Renderer/GraphicsContext.h"

#include "StarEngine/Renderer/Renderer.h"
#include "Platform/OpenGL/OpenGLContext.h"

namespace StarEngine
{
	Scope<GraphicsContext> GraphicsContext::Create(void* window)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None:    SE_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
		case RendererAPI::API::OpenGL:  return CreateScope<OpenGLContext>(static_cast<GLFWwindow*>(window));
		}
		SE_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}