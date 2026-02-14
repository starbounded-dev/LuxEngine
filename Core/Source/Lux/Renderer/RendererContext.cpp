#include "lpch.h"
#include "RendererContext.h"

#include "Lux/Renderer/RendererAPI.h"

#include "Lux/Platform/Vulkan/VulkanContext.h"

namespace Lux {

	Ref<RendererContext> RendererContext::Create()
	{
		switch (RendererAPI::Current())
		{
			case RendererAPIType::None:    return nullptr;
			case RendererAPIType::Vulkan:  return Ref<VulkanContext>::Create();
		}
		LUX_CORE_ASSERT(false, "Unknown RendererAPI");
		return nullptr;
	}

}