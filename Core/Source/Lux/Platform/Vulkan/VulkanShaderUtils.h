#pragma once

#include "Lux/Renderer/Shader.h"

#include "nvrhi/nvrhi.h"

#include <shaderc/shaderc.h>

#include <vulkan/vulkan_core.h>


namespace Lux { namespace ShaderUtils {

		inline static nvrhi::ShaderType PreprocessorStageToShaderStage(const std::string_view stage)
		{
			if (stage == "vert") return nvrhi::ShaderType::Vertex;
			if (stage == "frag") return nvrhi::ShaderType::Pixel;
			if (stage == "comp") return nvrhi::ShaderType::Compute;
			if (stage == "task") return nvrhi::ShaderType::Amplification;
			if (stage == "mesh") return nvrhi::ShaderType::Mesh;
			LUX_CORE_VERIFY(false, "Unknown shader stage.");
			return nvrhi::ShaderType::None;
		}

		inline static std::string_view ShaderStageToShaderMacro(const nvrhi::ShaderType stage)
		{
			if (stage == nvrhi::ShaderType::Vertex)        return "__VERTEX_STAGE__";
			if (stage == nvrhi::ShaderType::Pixel)         return "__FRAGMENT_STAGE__";
			if (stage == nvrhi::ShaderType::Compute)       return "__COMPUTE_STAGE__";
			if (stage == nvrhi::ShaderType::Amplification) return "__TASK_STAGE__";
			if (stage == nvrhi::ShaderType::Mesh)          return "__MESH_STAGE__";
			LUX_CORE_VERIFY(false, "Unknown shader stage.");
			return "";
		}

		inline static SourceLang ShaderLangFromExtension(const std::string_view type)
		{
			if (type == ".glsl")	return SourceLang::GLSL;
			if (type == ".hlsl")	return SourceLang::HLSL;

			LUX_CORE_ASSERT(false);

			return SourceLang::NONE;
		}

		inline static shaderc_shader_kind ShaderStageToShaderC(const nvrhi::ShaderType stage)
		{
			switch (stage)
			{
				case nvrhi::ShaderType::Vertex:        return shaderc_vertex_shader;
				case nvrhi::ShaderType::Pixel:         return shaderc_fragment_shader;
				case nvrhi::ShaderType::Compute:       return shaderc_compute_shader;
				case nvrhi::ShaderType::Amplification: return shaderc_task_shader;
				case nvrhi::ShaderType::Mesh:          return shaderc_mesh_shader;
			}
			LUX_CORE_ASSERT(false);
			return {};
		}

		inline static const char* ShaderStageCachedFileExtension(const nvrhi::ShaderType stage, bool debug)
		{
			if (debug)
			{
				switch (stage)
				{
					case nvrhi::ShaderType::Vertex:        return ".cached_vulkan_debug.vert";
					case nvrhi::ShaderType::Pixel:         return ".cached_vulkan_debug.frag";
					case nvrhi::ShaderType::Compute:       return ".cached_vulkan_debug.comp";
					case nvrhi::ShaderType::Amplification: return ".cached_vulkan_debug.task";
					case nvrhi::ShaderType::Mesh:          return ".cached_vulkan_debug.mesh";
				}
			}
			else
			{
				switch (stage)
				{
					case nvrhi::ShaderType::Vertex:        return ".cached_vulkan.vert";
					case nvrhi::ShaderType::Pixel:         return ".cached_vulkan.frag";
					case nvrhi::ShaderType::Compute:       return ".cached_vulkan.comp";
					case nvrhi::ShaderType::Amplification: return ".cached_vulkan.task";
					case nvrhi::ShaderType::Mesh:          return ".cached_vulkan.mesh";
				}
			}
			LUX_CORE_ASSERT(false);
			return "";
		}

#ifdef LUX_PLATFORM_WINDOWS
		inline static const wchar_t* HLSLShaderProfile(const nvrhi::ShaderType stage)
		{
			switch (stage)
			{
				case nvrhi::ShaderType::Vertex:        return L"vs_6_0";
				case nvrhi::ShaderType::Pixel:         return L"ps_6_0";
				case nvrhi::ShaderType::Compute:       return L"cs_6_0";
				case nvrhi::ShaderType::Amplification: return L"as_6_5";
				case nvrhi::ShaderType::Mesh:          return L"ms_6_5";
			}
			LUX_CORE_ASSERT(false);
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
			LUX_CORE_ASSERT(false);
			return "";
		}
#endif
	}


}
