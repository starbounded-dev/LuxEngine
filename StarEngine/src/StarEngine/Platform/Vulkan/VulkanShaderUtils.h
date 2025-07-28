#pragma once

#include "StarEngine/Renderer/Shader.h"

#include "nvrhi/nvrhi.h"

#include <shaderc/shaderc.h>

#include <vulkan/vulkan_core.h>


namespace StarEngine { namespace ShaderUtils {

#if 0
		inline static std::string_view VKStageToShaderMacro(const VkShaderStageFlagBits stage)
		{
			if (stage == VK_SHADER_STAGE_VERTEX_BIT)   return "__VERTEX_STAGE__";
			if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) return "__FRAGMENT_STAGE__";
			if (stage == VK_SHADER_STAGE_COMPUTE_BIT)  return "__COMPUTE_STAGE__";
			SE_CORE_VERIFY(false, "Unknown shader stage.");
			return "";
		}

		inline static std::string_view StageToShaderMacro(const std::string_view stage)
		{
			if (stage == "vert") return "__VERTEX_STAGE__";
			if (stage == "frag") return "__FRAGMENT_STAGE__";
			if (stage == "comp") return "__COMPUTE_STAGE__";
			SE_CORE_VERIFY(false, "Unknown shader stage.");
			return "";
		}

		inline static VkShaderStageFlagBits StageToVKShaderStage(const std::string_view stage)
		{
			if (stage == "vert") return VK_SHADER_STAGE_VERTEX_BIT;
			if (stage == "frag") return VK_SHADER_STAGE_FRAGMENT_BIT;
			if (stage == "comp") return VK_SHADER_STAGE_COMPUTE_BIT;
			SE_CORE_VERIFY(false, "Unknown shader stage.");
			return VK_SHADER_STAGE_ALL;
		}
#endif
		inline static nvrhi::ShaderType PreprocessorStageToShaderStage(const std::string_view stage)
		{
			if (stage == "vert") return nvrhi::ShaderType::Vertex;
			if (stage == "frag") return nvrhi::ShaderType::Pixel;
			if (stage == "comp") return nvrhi::ShaderType::Compute;
			SE_CORE_VERIFY(false, "Unknown shader stage.");
			return nvrhi::ShaderType::None;
		}

		inline static std::string_view ShaderStageToShaderMacro(const nvrhi::ShaderType stage)
		{
			if (stage == nvrhi::ShaderType::Vertex)  return "__VERTEX_STAGE__";
			if (stage == nvrhi::ShaderType::Pixel)   return "__FRAGMENT_STAGE__";
			if (stage == nvrhi::ShaderType::Compute) return "__COMPUTE_STAGE__";
			SE_CORE_VERIFY(false, "Unknown shader stage.");
			return "";
		}

		inline static SourceLang ShaderLangFromExtension(const std::string_view type)
		{
			if (type == ".glsl")	return SourceLang::GLSL;
			if (type == ".hlsl")	return SourceLang::HLSL;

			SE_CORE_ASSERT(false);

			return SourceLang::NONE;
		}

		inline static shaderc_shader_kind ShaderStageToShaderC(const nvrhi::ShaderType stage)
		{
			switch (stage)
			{
				case nvrhi::ShaderType::Vertex:  return shaderc_vertex_shader;
				case nvrhi::ShaderType::Pixel:   return shaderc_fragment_shader;
				case nvrhi::ShaderType::Compute: return shaderc_compute_shader;
			}
			SE_CORE_ASSERT(false);
			return {};
		}

		inline static const char* ShaderStageCachedFileExtension(const nvrhi::ShaderType stage, bool debug)
		{
			if (debug)
			{
				switch (stage)
				{
					case nvrhi::ShaderType::Vertex:  return ".cached_vulkan_debug.vert";
					case nvrhi::ShaderType::Pixel:   return ".cached_vulkan_debug.frag";
					case nvrhi::ShaderType::Compute: return ".cached_vulkan_debug.comp";
				}
			}
			else
			{
				switch (stage)
				{
					case nvrhi::ShaderType::Vertex:  return ".cached_vulkan.vert";
					case nvrhi::ShaderType::Pixel:   return ".cached_vulkan.frag";
					case nvrhi::ShaderType::Compute: return ".cached_vulkan.comp";
				}
			}
			SE_CORE_ASSERT(false);
			return "";
		}

#ifdef SE_PLATFORM_WINDOWS
		inline static const wchar_t* HLSLShaderProfile(const nvrhi::ShaderType stage)
		{
			switch (stage)
			{
				case nvrhi::ShaderType::Vertex:   return L"vs_6_0";
				case nvrhi::ShaderType::Pixel:    return L"ps_6_0";
				case nvrhi::ShaderType::Compute:  return L"cs_6_0";
			}
			SE_CORE_ASSERT(false);
			return L"";
		}
#else
		inline static const char* HLSLShaderProfile(const nvrhi::ShaderType stage)
		{
			switch (stage)
			{
				case nvrhi::ShaderType::Vertex:  return "vs_6_0";
				case nvrhi::ShaderType::Pixel:   return "ps_6_0";
				case nvrhi::ShaderType::Compute: return "cs_6_0";
			}
			SE_CORE_ASSERT(false);
			return "";
		}
#endif
	}


}
