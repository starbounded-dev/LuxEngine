// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "VulkanShader.h"

#include "VulkanShaderUtils.h"
#include "Debug/Aftermath.h"

#if LUX_HAS_SHADER_COMPILER
#include "ShaderCompiler/VulkanShaderCompiler.h"
#endif

#include "Lux/Core/Application.h"
#include "Lux/Core/Hash.h"
#include "Lux/ImGui/ImGuiCore.h"
#include "Lux/Renderer/BindlessTextureTable.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/RHI/NVRHIInterop.h"
#include "Lux/Renderer/RHI/RHIDevice.h"
#include "Lux/Utilities/StringUtils.h"

#include <algorithm>
#include <filesystem>
#include <format>

namespace Lux {

	namespace {

		const std::vector<uint32_t> s_NoSPIRV;

		nri::StageBits ToNRIStageBits(ShaderStage stage)
		{
			switch (stage)
			{
				case ShaderStage::Vertex:			return nri::StageBits::VERTEX_SHADER;
				case ShaderStage::Hull:				return nri::StageBits::TESS_CONTROL_SHADER;
				case ShaderStage::Domain:			return nri::StageBits::TESS_EVALUATION_SHADER;
				case ShaderStage::Geometry:			return nri::StageBits::GEOMETRY_SHADER;
				case ShaderStage::Pixel:			return nri::StageBits::FRAGMENT_SHADER;
				case ShaderStage::Compute:			return nri::StageBits::COMPUTE_SHADER;
				case ShaderStage::Amplification:	return nri::StageBits::TASK_SHADER;
				case ShaderStage::Mesh:				return nri::StageBits::MESH_SHADER;
				default:							break;
			}
			LUX_CORE_ASSERT(false, "Unknown shader stage");
			return nri::StageBits::ALL;
		}

	}

	VulkanShader::VulkanShader(const std::string& path, bool forceCompile, bool disableOptimization)
		: m_AssetPath(path), m_DisableOptimization(disableOptimization)
	{
		// TODO: This should be more "general"
		size_t found = path.find_last_of("/\\");
		m_Name = found != std::string::npos ? path.substr(found + 1) : path;
		found = m_Name.find_last_of('.');
		m_Name = found != std::string::npos ? m_Name.substr(0, found) : m_Name;

		Reload(forceCompile);
	}

	VulkanShader::~VulkanShader()
	{
		ReleaseNRIPipelineLayout();
	}

	void VulkanShader::Release()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		ReleaseNRIPipelineLayout();
	}

	void VulkanShader::ReleaseNRIPipelineLayout()
	{
		if (!m_NRIPipelineLayout)
			return;

		Renderer::SubmitResourceFree([layout = std::exchange(m_NRIPipelineLayout, nullptr)]()
			{
				RHIDevice::API().DestroyPipelineLayout(layout);
			});
		m_NRISetIndices.fill(k_NoNRISet);
		for (std::vector<NRIRange>& ranges : m_NRIRanges)
			ranges.clear();
		m_NRIRootConstantSize = 0;
	}

	uint32_t VulkanShader::GetNRIRangeIndex(uint32_t set, uint32_t binding) const
	{
		if (set >= m_NRIRanges.size())
			return k_NoNRISet;

		const std::vector<NRIRange>& ranges = m_NRIRanges[set];
		auto it = std::lower_bound(ranges.begin(), ranges.end(), binding, [](const NRIRange& range, uint32_t value) { return range.Binding < value; });
		return it != ranges.end() && it->Binding == binding ? static_cast<uint32_t>(it - ranges.begin()) : k_NoNRISet;
	}

	bool VulkanShader::IsNRISetCompatible(uint32_t set, const VulkanShader& other) const
	{
		if (set >= m_NRISetIndices.size() || m_NRISetIndices[set] == k_NoNRISet || m_NRISetIndices[set] != other.m_NRISetIndices[set])
			return false;
		if (set == 0 && m_NRIRootConstantSize != other.m_NRIRootConstantSize)
			return false;
		return m_NRIRanges[set] == other.m_NRIRanges[set];
	}

	nri::DescriptorPoolDesc VulkanShader::GetNRIPoolDesc(uint32_t set, uint32_t instanceCount) const
	{
		nri::DescriptorPoolDesc poolDesc = {};
		poolDesc.descriptorSetMaxNum = instanceCount;
		if (set >= m_ReflectionData.ShaderDescriptorSets.size())
			return poolDesc;

		if (set == BindlessTextureTable::DescriptorSet)
		{
			poolDesc.textureMaxNum = BindlessTextureTable::GetCapacity() * instanceCount;
			return poolDesc;
		}

		// Counted exactly as CreateNRIPipelineLayout declares the ranges.
		const ShaderResource::ShaderDescriptorSet& descriptorSet = m_ReflectionData.ShaderDescriptorSets[set];
		poolDesc.constantBufferMaxNum = static_cast<uint32_t>(descriptorSet.UniformBuffers.size()) * instanceCount;
		for (const auto& [binding, storageBuffer] : descriptorSet.StorageBuffers)
			(storageBuffer.ReadOnly ? poolDesc.structuredBufferMaxNum : poolDesc.storageStructuredBufferMaxNum) += instanceCount;
		for (const auto& [binding, texture] : descriptorSet.SeparateTextures)
			poolDesc.textureMaxNum += std::max(texture.ArraySize, 1u) * instanceCount;
		for (const auto& [binding, sampler] : descriptorSet.SeparateSamplers)
			poolDesc.samplerMaxNum += std::max(sampler.ArraySize, 1u) * instanceCount;
		for (const auto& [binding, image] : descriptorSet.StorageImages)
			poolDesc.storageTextureMaxNum += std::max(image.ArraySize, 1u) * instanceCount;
		return poolDesc;
	}

	const std::vector<uint32_t>& VulkanShader::GetSPIRV(ShaderStage stage) const
	{
		auto it = m_ShaderData.find(stage);
		return it != m_ShaderData.end() ? it->second : s_NoSPIRV;
	}

	void VulkanShader::CreateNRIPipelineLayout()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		ReleaseNRIPipelineLayout();

		const auto& shaderDescriptorSets = m_ReflectionData.ShaderDescriptorSets;
		if (shaderDescriptorSets.size() > m_NRISetIndices.size())
		{
			LUX_CORE_ERROR_TAG("Renderer", "Shader {} declares {} descriptor sets; NRI layouts support {}, so it has none", m_Name, shaderDescriptorSets.size(), m_NRISetIndices.size());
			return;
		}

		// Ranges must outlive CreatePipelineLayout; one vector per set.
		std::vector<std::vector<nri::DescriptorRangeDesc>> ranges(shaderDescriptorSets.size());
		std::vector<nri::DescriptorSetDesc> setDescs;
		m_NRISetIndices.fill(k_NoNRISet);

		for (uint32_t set = 0; set < shaderDescriptorSets.size(); set++)
		{
			const ShaderResource::ShaderDescriptorSet& descriptorSet = shaderDescriptorSets[set];
			std::vector<nri::DescriptorRangeDesc>& setRanges = ranges[set];

			if (set == BindlessTextureTable::DescriptorSet)
			{
				if (descriptorSet)
					setRanges.push_back(BindlessTextureTable::GetNRIRange());
			}
			else
			{
				const auto addRange = [&](uint32_t binding, nri::DescriptorType type, uint32_t count)
				{
					nri::DescriptorRangeDesc& range = setRanges.emplace_back();
					range.baseRegisterIndex = binding;
					range.descriptorNum = std::max(count, 1u);
					range.descriptorType = type;
					range.shaderStages = nri::StageBits::ALL;
					range.flags = count > 1 ? nri::DescriptorRangeBits::ARRAY : nri::DescriptorRangeBits::NONE;
				};

				for (const auto& [binding, uniformBuffer] : descriptorSet.UniformBuffers)
					addRange(binding, nri::DescriptorType::CONSTANT_BUFFER, 1);
				// Lux binds storage buffers as raw (byte-address) views.
				for (const auto& [binding, storageBuffer] : descriptorSet.StorageBuffers)
					addRange(binding, storageBuffer.ReadOnly ? nri::DescriptorType::STRUCTURED_BUFFER : nri::DescriptorType::STORAGE_STRUCTURED_BUFFER, 1);
				for (const auto& [binding, texture] : descriptorSet.SeparateTextures)
					addRange(binding, nri::DescriptorType::TEXTURE, texture.ArraySize);
				for (const auto& [binding, sampler] : descriptorSet.SeparateSamplers)
					addRange(binding, nri::DescriptorType::SAMPLER, sampler.ArraySize);
				for (const auto& [binding, image] : descriptorSet.StorageImages)
					addRange(binding, nri::DescriptorType::STORAGE_TEXTURE, image.ArraySize);
				for (const auto& [binding, imageSampler] : descriptorSet.ImageSamplers)
					LUX_CORE_ERROR_TAG("Renderer", "Shader {}: combined image sampler {} ({}.{}) has no NRI descriptor and is left out", m_Name, imageSampler.Name, set, binding);

				std::sort(setRanges.begin(), setRanges.end(), [](const nri::DescriptorRangeDesc& a, const nri::DescriptorRangeDesc& b) { return a.baseRegisterIndex < b.baseRegisterIndex; });
			}

			if (setRanges.empty())
				continue;

			for (const nri::DescriptorRangeDesc& range : setRanges)
				m_NRIRanges[set].push_back({ range.baseRegisterIndex, range.descriptorNum, static_cast<uint32_t>(range.descriptorType), static_cast<uint32_t>(range.flags) });

			m_NRISetIndices[set] = static_cast<uint32_t>(setDescs.size());
			nri::DescriptorSetDesc& setDesc = setDescs.emplace_back();
			setDesc.registerSpace = set;
			setDesc.ranges = setRanges.data();
			setDesc.rangeNum = static_cast<uint32_t>(setRanges.size());
		}

		// NVRHI puts all push-constant ranges in one block; so does NRI's single root constant.
		nri::RootConstantDesc rootConstant = {};
		for (const ShaderResource::PushConstantRange& range : m_ReflectionData.PushConstantRanges)
			rootConstant.size = std::max(rootConstant.size, range.Offset + range.Size);
		rootConstant.size = (rootConstant.size + 3u) & ~3u;
		rootConstant.shaderStages = nri::StageBits::ALL;
		m_NRIRootConstantSize = rootConstant.size;

		nri::PipelineLayoutDesc layoutDesc = {};
		// No root descriptors; keep the (unused) root space clear of every set.
		layoutDesc.rootRegisterSpace = static_cast<uint32_t>(shaderDescriptorSets.size());
		layoutDesc.rootConstants = rootConstant.size ? &rootConstant : nullptr;
		layoutDesc.rootConstantNum = rootConstant.size ? 1 : 0;
		layoutDesc.descriptorSets = setDescs.data();
		layoutDesc.descriptorSetNum = static_cast<uint32_t>(setDescs.size());
		// The shader's own stages. NRI's validation checks every graphics pipeline stage against
		// this mask and reads StageBits::ALL (0) there as no stages.
		uint32_t layoutStages = 0;
		for (const auto& [stage, spirv] : m_ShaderData)
			layoutStages |= static_cast<uint32_t>(ToNRIStageBits(stage));
		layoutDesc.shaderStages = layoutStages ? static_cast<nri::StageBits>(layoutStages) : nri::StageBits::ALL;
		// Reflected bindings are final SPIR-V binding numbers.
		layoutDesc.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;

		if (RHIDevice::API().CreatePipelineLayout(RHIDevice::Get(), layoutDesc, m_NRIPipelineLayout) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Failed to create the NRI pipeline layout of shader {}", m_Name);
			m_NRIPipelineLayout = nullptr;
			m_NRISetIndices.fill(k_NoNRISet);
			for (std::vector<NRIRange>& ranges : m_NRIRanges)
				ranges.clear();
			m_NRIRootConstantSize = 0;
			return;
		}

		RHIDevice::API().SetDebugName(m_NRIPipelineLayout, m_Name.c_str());
	}

	void VulkanShader::RT_Reload(const bool forceCompile)
	{
		LUX_PROFILE_FUNCTION_AUTO;
#if LUX_HAS_SHADER_COMPILER 
		if (!VulkanShaderCompiler::TryRecompile(this))
		{
			LUX_CORE_FATAL("Failed to recompile shader!");
		}
#endif
	}

	void VulkanShader::Reload(bool forceCompile)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Renderer::Submit([instance = Ref(this), forceCompile]() mutable
		{
			instance->RT_Reload(forceCompile);
		});
	}

	size_t VulkanShader::GetHash() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return Hash::GenerateFNVHash(m_AssetPath.string());
	}

	void VulkanShader::SetShaderData(const std::map<ShaderStage, std::vector<uint32_t>>& shaderData)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_ShaderData = shaderData;

		for (const auto& [stage, data] : shaderData)
		{
			LUX_CORE_ASSERT(data.size());
			// The binary the driver gets, so a GPU crash dump can map its shader addresses.
			Aftermath::AddShaderBinary(data.data(), data.size());
		}
	}


	
	void VulkanShader::CreateDescriptors()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// The input declarations DescriptorSetManager binds by name, then the NRI layout. The bindless
		// set's one unsized array is shared by every shader that declares it and is not an input.
		const auto dimensionInputType = [](uint32_t dimension, RenderInputType oneD, RenderInputType twoD, RenderInputType threeD, RenderInputType volume)
		{
			switch (dimension)
			{
				case 1:		return oneD;
				case 2:		return twoD;
				case 3:		return threeD;
				case 4:		return volume;
				default:	break;
			}
			LUX_CORE_ASSERT(false);
			return RenderInputType::None;
		};

		for (uint32_t set = 0; set < m_ReflectionData.ShaderDescriptorSets.size(); set++)
		{
			if (set == BindlessTextureTable::DescriptorSet)
				continue;

			auto& shaderDescriptorSet = m_ReflectionData.ShaderDescriptorSets[set];
			const auto declare = [&](const std::string& name, uint32_t binding, RenderInputType type, uint32_t count)
			{
				RenderInputDeclaration& inputDecl = shaderDescriptorSet.InputDeclarations[name];
				inputDecl.Type = type;
				inputDecl.Set = set;
				inputDecl.Binding = binding;
				inputDecl.Name = name;
				inputDecl.Count = count;
			};

			for (auto& [binding, uniformBuffer] : shaderDescriptorSet.UniformBuffers)
				declare(uniformBuffer.Name, binding, RenderInputType::UniformBuffer, 1);
			for (auto& [binding, storageBuffer] : shaderDescriptorSet.StorageBuffers)
				declare(storageBuffer.Name, binding, RenderInputType::StorageBuffer, 1);
			for (auto& [binding, imageSampler] : shaderDescriptorSet.ImageSamplers)
				declare(imageSampler.Name, binding, dimensionInputType(imageSampler.Dimension, RenderInputType::ImageSampler1D, RenderInputType::ImageSampler2D, RenderInputType::ImageSampler3D, RenderInputType::ImageSampler3DVolume), imageSampler.ArraySize);
			for (auto& [binding, texture] : shaderDescriptorSet.SeparateTextures)
				declare(texture.Name, binding, dimensionInputType(texture.Dimension, RenderInputType::ImageSampler1D, RenderInputType::ImageSampler2D, RenderInputType::ImageSampler3D, RenderInputType::ImageSampler3DVolume), texture.ArraySize);
			for (auto& [binding, sampler] : shaderDescriptorSet.SeparateSamplers)
			{
				// Separate samplers are reflected with dimension 0.
				const RenderInputType type = sampler.Dimension == 0 ? RenderInputType::ImageSampler
					: dimensionInputType(sampler.Dimension, RenderInputType::ImageSampler1D, RenderInputType::ImageSampler2D, RenderInputType::ImageSampler3D, RenderInputType::ImageSampler3DVolume);
				declare(sampler.Name, binding, type, sampler.ArraySize);
			}
			for (auto& [binding, image] : shaderDescriptorSet.StorageImages)
				declare(image.Name, binding, dimensionInputType(image.Dimension, RenderInputType::StorageImage1D, RenderInputType::StorageImage2D, RenderInputType::StorageImage3D, RenderInputType::StorageImage3DVolume), image.ArraySize);
		}

		CreateNRIPipelineLayout();
	}


	const std::unordered_map<std::string, ShaderResourceDeclaration>& VulkanShader::GetResources() const
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_ReflectionData.Resources;
	}

	void VulkanShader::AddShaderReloadedCallback(const ShaderReloadedCallback& callback)
	{
		LUX_PROFILE_FUNCTION_AUTO;
	}

	bool VulkanShader::TryReadReflectionData(StreamReader* serializer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		uint32_t shaderDescriptorSetCount;
		serializer->ReadRaw<uint32_t>(shaderDescriptorSetCount);

		for (uint32_t i = 0; i < shaderDescriptorSetCount; i++)
		{
			auto& descriptorSet = m_ReflectionData.ShaderDescriptorSets.emplace_back();
			serializer->ReadMap(descriptorSet.UniformBuffers);
			serializer->ReadMap(descriptorSet.StorageBuffers);
			serializer->ReadMap(descriptorSet.ImageSamplers);
			serializer->ReadMap(descriptorSet.StorageImages);
			serializer->ReadMap(descriptorSet.SeparateTextures);
			serializer->ReadMap(descriptorSet.SeparateSamplers);
			serializer->ReadMap(descriptorSet.InputDeclarations);
		}

		serializer->ReadMap(m_ReflectionData.Resources);
		serializer->ReadMap(m_ReflectionData.ConstantBuffers);
		serializer->ReadArray(m_ReflectionData.PushConstantRanges);

		return true;
	}

	void VulkanShader::SerializeReflectionData(StreamWriter* serializer)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		serializer->WriteRaw<uint32_t>((uint32_t)m_ReflectionData.ShaderDescriptorSets.size());
		for (const auto& descriptorSet : m_ReflectionData.ShaderDescriptorSets)
		{
			serializer->WriteMap(descriptorSet.UniformBuffers);
			serializer->WriteMap(descriptorSet.StorageBuffers);
			serializer->WriteMap(descriptorSet.ImageSamplers);
			serializer->WriteMap(descriptorSet.StorageImages);
			serializer->WriteMap(descriptorSet.SeparateTextures);
			serializer->WriteMap(descriptorSet.SeparateSamplers);
			serializer->WriteMap(descriptorSet.InputDeclarations);
		}

		serializer->WriteMap(m_ReflectionData.Resources);
		serializer->WriteMap(m_ReflectionData.ConstantBuffers);
		serializer->WriteArray(m_ReflectionData.PushConstantRanges);
	}

	void VulkanShader::SetReflectionData(const ReflectionData& reflectionData)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_ReflectionData = reflectionData;
	}

}
