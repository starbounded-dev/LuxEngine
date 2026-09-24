// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "RendererContext.h"

#include "Lux/Renderer/RendererAPI.h"

#include "Lux/Platform/Vulkan/VulkanContext.h"

namespace Lux {

	Ref<RendererContext> RendererContext::Create()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		switch (RendererAPI::Current())
		{
			case RendererAPIType::None:    return nullptr;
			case RendererAPIType::Vulkan:  return Ref<VulkanContext>::Create();
		}
		LUX_CORE_ASSERT(false, "Unknown RendererAPI");
		return nullptr;
	}

}