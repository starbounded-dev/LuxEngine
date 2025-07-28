#include "sepch.h"
#include "RendererContext.h"

#include "StarEngine/Renderer/RendererAPI.h"

#include "StarEngine/Renderer/Platform/Vulkan/VulkanContext.h"

namespace StarEngine {

	Ref<RendererContext> RendererContext::Create()
	{
		switch (RendererAPI::Current())
		{
		case RendererAPIType::None:    return nullptr;
		case RendererAPIType::Vulkan:  return Ref<VulkanContext>::Create();
		}
		SE_CORE_ASSERT(false, "Unknown RendererAPI");
		return nullptr;
	}

}
