// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/RHI/RHITypes.h"

#include "VulkanShaderCompiler.h"

#include <filesystem>
#include <map>

namespace Lux {

	class VulkanShaderCache
	{
	public:
		static ShaderStage HasChanged(Ref<VulkanShaderCompiler> shader);
	private:
		static void Serialize(const std::map<std::string, std::map<ShaderStage, StageData>>& shaderCache);
		static void Deserialize(std::map<std::string, std::map<ShaderStage, StageData>>& shaderCache);
	};

}
