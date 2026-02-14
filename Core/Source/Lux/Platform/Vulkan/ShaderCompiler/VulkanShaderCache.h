#pragma once

#include "nvrhi/nvrhi.h"

#include "VulkanShaderCompiler.h"

#include <filesystem>
#include <map>

namespace Lux {

	class VulkanShaderCache
	{
	public:
		static nvrhi::ShaderType HasChanged(Ref<VulkanShaderCompiler> shader);
	private:
		static void Serialize(const std::map<std::string, std::map<nvrhi::ShaderType, StageData>>& shaderCache);
		static void Deserialize(std::map<std::string, std::map<nvrhi::ShaderType, StageData>>& shaderCache);
	};

}
