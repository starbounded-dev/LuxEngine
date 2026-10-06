// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/Shader.h"

#include "Lux/Renderer/RHI/RHITypes.h"

#include <shaderc/shaderc.h>

#include <vulkan/vulkan_core.h>


namespace Lux { namespace ShaderUtils {

		inline static ShaderStage PreprocessorStageToShaderStage(const std::string_view stage)
		{
			if (stage == "vert") return ShaderStage::Vertex;
			if (stage == "frag") return ShaderStage::Pixel;
			if (stage == "comp") return ShaderStage::Compute;
			if (stage == "task") return ShaderStage::Amplification;
			if (stage == "mesh") return ShaderStage::Mesh;
			LUX_CORE_VERIFY(false, "Unknown shader stage.");
			return ShaderStage::None;
		}

		inline static std::string_view ShaderStageToShaderMacro(const ShaderStage stage)
		{
			if (stage == ShaderStage::Vertex)        return "__VERTEX_STAGE__";
			if (stage == ShaderStage::Pixel)         return "__FRAGMENT_STAGE__";
			if (stage == ShaderStage::Compute)       return "__COMPUTE_STAGE__";
			if (stage == ShaderStage::Amplification) return "__TASK_STAGE__";
			if (stage == ShaderStage::Mesh)          return "__MESH_STAGE__";
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

		inline static shaderc_shader_kind ShaderStageToShaderC(const ShaderStage stage)
		{
			switch (stage)
			{
				case ShaderStage::Vertex:        return shaderc_vertex_shader;
				case ShaderStage::Pixel:         return shaderc_fragment_shader;
				case ShaderStage::Compute:       return shaderc_compute_shader;
				case ShaderStage::Amplification: return shaderc_task_shader;
				case ShaderStage::Mesh:          return shaderc_mesh_shader;
			}
			LUX_CORE_ASSERT(false);
			return {};
		}

		inline static const char* ShaderStageCachedFileExtension(const ShaderStage stage, bool debug)
		{
			if (debug)
			{
				switch (stage)
				{
					case ShaderStage::Vertex:        return ".cached_vulkan_debug.vert";
					case ShaderStage::Pixel:         return ".cached_vulkan_debug.frag";
					case ShaderStage::Compute:       return ".cached_vulkan_debug.comp";
					case ShaderStage::Amplification: return ".cached_vulkan_debug.task";
					case ShaderStage::Mesh:          return ".cached_vulkan_debug.mesh";
				}
			}
			else
			{
				switch (stage)
				{
					case ShaderStage::Vertex:        return ".cached_vulkan.vert";
					case ShaderStage::Pixel:         return ".cached_vulkan.frag";
					case ShaderStage::Compute:       return ".cached_vulkan.comp";
					case ShaderStage::Amplification: return ".cached_vulkan.task";
					case ShaderStage::Mesh:          return ".cached_vulkan.mesh";
				}
			}
			LUX_CORE_ASSERT(false);
			return "";
		}

#ifdef LUX_PLATFORM_WINDOWS
		inline static const wchar_t* HLSLShaderProfile(const ShaderStage stage)
		{
			switch (stage)
			{
				case ShaderStage::Vertex:        return L"vs_6_0";
				case ShaderStage::Pixel:         return L"ps_6_0";
				case ShaderStage::Compute:       return L"cs_6_0";
				case ShaderStage::Amplification: return L"as_6_5";
				case ShaderStage::Mesh:          return L"ms_6_5";
			}
			LUX_CORE_ASSERT(false);
			return L"";
		}
#else
		inline static const char* HLSLShaderProfile(const ShaderStage stage)
		{
			switch (stage)
			{
				case ShaderStage::Vertex:  return "vs_6_0";
				case ShaderStage::Pixel:   return "ps_6_0";
				case ShaderStage::Compute: return "cs_6_0";
			}
			LUX_CORE_ASSERT(false);
			return "";
		}
#endif
	}


}
