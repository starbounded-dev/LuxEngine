// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "RHITypes.h"

#include <cstddef>

#include <vulkan/vulkan.h>

namespace Lux {

	// Stored raw in shader reflection and ShaderPack files.
	static_assert(sizeof(ShaderStage) == 2);
	static_assert(static_cast<uint16_t>(ShaderStage::Vertex) == 0x0001 && static_cast<uint16_t>(ShaderStage::Hull) == 0x0002
		&& static_cast<uint16_t>(ShaderStage::Domain) == 0x0004 && static_cast<uint16_t>(ShaderStage::Geometry) == 0x0008
		&& static_cast<uint16_t>(ShaderStage::Pixel) == 0x0010 && static_cast<uint16_t>(ShaderStage::Compute) == 0x0020
		&& static_cast<uint16_t>(ShaderStage::Amplification) == 0x0040 && static_cast<uint16_t>(ShaderStage::Mesh) == 0x0080);

	// Indirect-draw buffers are written as DrawIndexedIndirectCommand and read by the GPU with the
	// VkDrawIndexedIndirectCommand layout.
	static_assert(sizeof(DrawIndexedIndirectCommand) == sizeof(VkDrawIndexedIndirectCommand));
	static_assert(offsetof(DrawIndexedIndirectCommand, IndexCount) == offsetof(VkDrawIndexedIndirectCommand, indexCount));
	static_assert(offsetof(DrawIndexedIndirectCommand, InstanceCount) == offsetof(VkDrawIndexedIndirectCommand, instanceCount));
	static_assert(offsetof(DrawIndexedIndirectCommand, FirstIndex) == offsetof(VkDrawIndexedIndirectCommand, firstIndex));
	static_assert(offsetof(DrawIndexedIndirectCommand, VertexOffset) == offsetof(VkDrawIndexedIndirectCommand, vertexOffset));
	static_assert(offsetof(DrawIndexedIndirectCommand, FirstInstance) == offsetof(VkDrawIndexedIndirectCommand, firstInstance));

	// Shader reflection serializes this where it used to write a VkDescriptorBufferInfo.
	static_assert(sizeof(LegacyDescriptorBufferInfo) == sizeof(VkDescriptorBufferInfo));
	static_assert(alignof(LegacyDescriptorBufferInfo) == alignof(VkDescriptorBufferInfo));

	namespace {

		struct ShaderStageName
		{
			ShaderStage Stage;
			std::string_view Name;
		};

		// NVRHI's spellings (utils::ShaderStageToString / ShaderStageFromString): the
		// shader cache (VulkanShaderCache) writes these to disk, so they must match what it holds.
		constexpr ShaderStageName k_ShaderStageNames[] = {
			{ ShaderStage::None,          "None" },
			{ ShaderStage::Compute,       "Compute" },
			{ ShaderStage::Vertex,        "Vertex" },
			{ ShaderStage::Hull,          "Hull" },
			{ ShaderStage::Domain,        "Domain" },
			{ ShaderStage::Geometry,      "Geometry" },
			{ ShaderStage::Pixel,         "Pixel" },
			{ ShaderStage::Amplification, "Amplification" },
			{ ShaderStage::Mesh,          "Mesh" },
			{ ShaderStage::AllGraphics,   "AllGraphics" },
			{ ShaderStage::RayGeneration, "RayGeneration" },
			{ ShaderStage::AnyHit,        "AnyHit" },
			{ ShaderStage::ClosestHit,    "ClosestHit" },
			{ ShaderStage::Miss,          "Miss" },
			{ ShaderStage::Intersection,  "Intersection" },
			{ ShaderStage::Callable,      "Callable" },
			{ ShaderStage::AllRayTracing, "AllRayTracing" },
			{ ShaderStage::All,           "All" },
		};

	}

	const char* ShaderStageToString(ShaderStage stage)
	{
		for (const ShaderStageName& entry : k_ShaderStageNames)
		{
			if (entry.Stage == stage)
				return entry.Name.data();
		}

		return "<INVALID>";
	}

	ShaderStage ShaderStageFromString(std::string_view string)
	{
		for (const ShaderStageName& entry : k_ShaderStageNames)
		{
			if (entry.Name == string)
				return entry.Stage;
		}

		return ShaderStage::None;
	}

}
