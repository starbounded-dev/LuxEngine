#include "sepch.h"
#include "StarEngine/Renderer/RenderCommand.h"

namespace StarEngine
{
	Scope<RendererAPI> RenderCommand::s_RendererAPI = RendererAPI::Create();
}