#include "lpch.h"
#include "Lux/Renderer/RenderCommand.h"

namespace Lux
{
	Scope<RendererAPI> RenderCommand::s_RendererAPI = RendererAPI::Create();
}