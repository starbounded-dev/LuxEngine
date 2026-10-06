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
		m_DescriptorSetLayouts.resize(0);
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

		nri::PipelineLayoutDesc layoutDesc = {};
		// No root descriptors; keep the (unused) root space clear of every set.
		layoutDesc.rootRegisterSpace = static_cast<uint32_t>(shaderDescriptorSets.size());
		layoutDesc.rootConstants = rootConstant.size ? &rootConstant : nullptr;
		layoutDesc.rootConstantNum = rootConstant.size ? 1 : 0;
		layoutDesc.descriptorSets = setDescs.data();
		layoutDesc.descriptorSetNum = static_cast<uint32_t>(setDescs.size());
		layoutDesc.shaderStages = nri::StageBits::ALL;
		// Reflected bindings are final SPIR-V binding numbers.
		layoutDesc.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;

		if (RHIDevice::API().CreatePipelineLayout(RHIDevice::Get(), layoutDesc, m_NRIPipelineLayout) != nri::Result::SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Renderer", "Failed to create the NRI pipeline layout of shader {}", m_Name);
			m_NRIPipelineLayout = nullptr;
			m_NRISetIndices.fill(k_NoNRISet);
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

	void VulkanShader::LoadAndCreateShaders(const std::map<ShaderStage, std::vector<uint32_t>>& shaderData)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_ShaderData = shaderData;

		nvrhi::IDevice* device = Application::Get().GetWindow().GetDeviceManager()->GetDevice();
		m_ShaderHandles.clear();

		std::string moduleName;
		for (auto [stage, data] : shaderData)
		{
			LUX_CORE_ASSERT(data.size());


			nvrhi::ShaderDesc desc;
			desc.shaderType = ToNVRHI(stage);
			desc.debugName = m_Name;
			desc.entryName = "main";
			m_ShaderHandles[stage] = device->createShader(desc, data.data(), data.size() * sizeof(uint32_t));
			// The binary the driver gets, so a GPU crash dump can map its shader addresses.
			Aftermath::AddShaderBinary(data.data(), data.size());
		}
	}


	
	void VulkanShader::CreateDescriptors()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		//LUX_CORE_VERIFY(false);
		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();

		m_DescriptorSetLayouts.resize(m_ReflectionData.ShaderDescriptorSets.size());
		for (uint32_t set = 0; set < m_ReflectionData.ShaderDescriptorSets.size(); set++)
		{
			auto& shaderDescriptorSet = m_ReflectionData.ShaderDescriptorSets[set];

			// The bindless set is one unsized texture array shared by every shader that declares it,
			// so it uses the renderer's layout rather than one reflected per shader.
			if (set == BindlessTextureTable::DescriptorSet)
			{
				m_DescriptorSetLayouts[set] = BindlessTextureTable::GetLayout();
				continue;
			}
			
			std::vector<VkDescriptorSetLayoutBinding> layoutBindings;

			nvrhi::BindingLayoutDesc bindingLayoutDesc;
			bindingLayoutDesc.visibility = nvrhi::ShaderType::None;

			if (set == 0 && !m_ReflectionData.PushConstantRanges.empty())
			{
				uint32_t index = 0;
				for (const ShaderResource::PushConstantRange& pushConstantRange : m_ReflectionData.PushConstantRanges)
				{
					bindingLayoutDesc.visibility = static_cast<nvrhi::ShaderType>(
					static_cast<uint32_t>(bindingLayoutDesc.visibility) | 
					static_cast<uint32_t>(pushConstantRange.ShaderStage));
					bindingLayoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::PushConstants(index++, pushConstantRange.Size));
				}
			}

			for (auto& [binding, uniformBuffer] : shaderDescriptorSet.UniformBuffers)
			{
				RenderInputDeclaration& inputDecl = shaderDescriptorSet.InputDeclarations[uniformBuffer.Name];
				inputDecl.Type = RenderInputType::UniformBuffer;
				inputDecl.Set = set;
				inputDecl.Binding = binding;
				inputDecl.Name = uniformBuffer.Name;
				inputDecl.Count = 1;

				bindingLayoutDesc.visibility = static_cast<nvrhi::ShaderType>(
				static_cast<uint32_t>(bindingLayoutDesc.visibility) | 
				static_cast<uint32_t>(uniformBuffer.ShaderStage));
				bindingLayoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::ConstantBuffer(binding));
			}

			for (auto& [binding, storageBuffer] : shaderDescriptorSet.StorageBuffers)
			{
				RenderInputDeclaration& inputDecl = shaderDescriptorSet.InputDeclarations[storageBuffer.Name];
				inputDecl.Type = RenderInputType::StorageBuffer;
				inputDecl.Set = set;
				inputDecl.Binding = binding;
				inputDecl.Name = storageBuffer.Name;
				inputDecl.Count = 1;

				bindingLayoutDesc.visibility = static_cast<nvrhi::ShaderType>(
				static_cast<uint32_t>(bindingLayoutDesc.visibility) | 
				static_cast<uint32_t>(storageBuffer.ShaderStage));
				if (storageBuffer.ReadOnly)
					bindingLayoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::RawBuffer_SRV(binding));
				else
					bindingLayoutDesc.bindings.push_back(nvrhi::BindingLayoutItem::RawBuffer_UAV(binding));
			}

			for (auto& [binding, imageSampler] : shaderDescriptorSet.ImageSamplers)
			{
				RenderInputDeclaration& inputDecl = shaderDescriptorSet.InputDeclarations[imageSampler.Name];
				switch (imageSampler.Dimension)
				{
					case 1:
						inputDecl.Type = RenderInputType::ImageSampler1D;
						break;
					case 2:
						inputDecl.Type = RenderInputType::ImageSampler2D;
						break;
					case 3:
						inputDecl.Type = RenderInputType::ImageSampler3D;
						break;
					case 4:
						inputDecl.Type = RenderInputType::ImageSampler3DVolume;
						break;
					default:
						LUX_CORE_ASSERT(false);
				}

				inputDecl.Set = set;
				inputDecl.Binding = binding;
				inputDecl.Name = imageSampler.Name;
				inputDecl.Count = imageSampler.ArraySize;

				nvrhi::BindingLayoutItem bindingLayoutItem = nvrhi::BindingLayoutItem::Texture_SRV(binding);
				bindingLayoutItem.size = imageSampler.ArraySize;

			bindingLayoutDesc.visibility = static_cast<nvrhi::ShaderType>(
				static_cast<uint32_t>(bindingLayoutDesc.visibility) | 
				static_cast<uint32_t>(imageSampler.ShaderStage));
			bindingLayoutDesc.bindings.push_back(bindingLayoutItem);
		}

		for (auto& [binding, imageSampler] : shaderDescriptorSet.SeparateTextures)
			{
				RenderInputDeclaration& inputDecl = shaderDescriptorSet.InputDeclarations[imageSampler.Name];
				switch (imageSampler.Dimension)
				{
					case 1:
						inputDecl.Type = RenderInputType::ImageSampler1D;
						break;
					case 2:
						inputDecl.Type = RenderInputType::ImageSampler2D;
						break;
					case 3:
						inputDecl.Type = RenderInputType::ImageSampler3D;
						break;
					case 4:
						inputDecl.Type = RenderInputType::ImageSampler3DVolume;
						break;
					default:
						LUX_CORE_ASSERT(false);

				}
				inputDecl.Set = set;
				inputDecl.Binding = binding;
				inputDecl.Name = imageSampler.Name;

				inputDecl.Count = imageSampler.ArraySize;

				nvrhi::BindingLayoutItem bindingLayoutItem = nvrhi::BindingLayoutItem::Texture_SRV(binding);
				bindingLayoutItem.size = imageSampler.ArraySize;

			bindingLayoutDesc.visibility = static_cast<nvrhi::ShaderType>(
				static_cast<uint32_t>(bindingLayoutDesc.visibility) | 
				static_cast<uint32_t>(imageSampler.ShaderStage));
			bindingLayoutDesc.bindings.push_back(bindingLayoutItem);
		}

		for (auto& [binding, imageSampler] : shaderDescriptorSet.SeparateSamplers)
			{
				RenderInputDeclaration& inputDecl = shaderDescriptorSet.InputDeclarations[imageSampler.Name];
				switch (imageSampler.Dimension)
				{
					case 0:
						inputDecl.Type = RenderInputType::ImageSampler;
						break;
					case 1:
						inputDecl.Type = RenderInputType::ImageSampler1D;
						break;
					case 2:
						inputDecl.Type = RenderInputType::ImageSampler2D;
						break;
					case 3:
						inputDecl.Type = RenderInputType::ImageSampler3D;
						break;
					case 4:
						inputDecl.Type = RenderInputType::ImageSampler3DVolume;
						break;
					default:
						LUX_CORE_ASSERT(false);

				}

				inputDecl.Set = set;
				inputDecl.Binding = binding;
				inputDecl.Name = imageSampler.Name;
				inputDecl.Count = imageSampler.ArraySize;

				nvrhi::BindingLayoutItem bindingLayoutItem = nvrhi::BindingLayoutItem::Sampler(binding);
				bindingLayoutItem.size = imageSampler.ArraySize;

			bindingLayoutDesc.visibility = static_cast<nvrhi::ShaderType>(
				static_cast<uint32_t>(bindingLayoutDesc.visibility) | 
				static_cast<uint32_t>(imageSampler.ShaderStage));
			bindingLayoutDesc.bindings.push_back(bindingLayoutItem);
		}

		for (auto& [binding, imageSampler] : shaderDescriptorSet.StorageImages)
			{
				RenderInputDeclaration& inputDecl = shaderDescriptorSet.InputDeclarations[imageSampler.Name];
				switch (imageSampler.Dimension)
				{
					case 1:
						inputDecl.Type = RenderInputType::StorageImage1D;
						break;
					case 2:
						inputDecl.Type = RenderInputType::StorageImage2D;
						break;
					case 3:
						inputDecl.Type = RenderInputType::StorageImage3D;
						break;
					case 4:
						inputDecl.Type = RenderInputType::StorageImage3DVolume;
						break;
					default:
						LUX_CORE_ASSERT(false);

				}

				inputDecl.Set = set;
				inputDecl.Binding = binding;
				inputDecl.Name = imageSampler.Name;
				inputDecl.Count = imageSampler.ArraySize;

				nvrhi::BindingLayoutItem bindingLayoutItem = nvrhi::BindingLayoutItem::Texture_UAV(binding);
				bindingLayoutItem.size = imageSampler.ArraySize;

			bindingLayoutDesc.visibility = static_cast<nvrhi::ShaderType>(
				static_cast<uint32_t>(bindingLayoutDesc.visibility) | 
				static_cast<uint32_t>(imageSampler.ShaderStage));
			bindingLayoutDesc.bindings.push_back(bindingLayoutItem);
		}
		
		m_DescriptorSetLayouts[set] = device->createBindingLayout(bindingLayoutDesc);
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

	nvrhi::ShaderHandle VulkanShader::GetHandle(ShaderStage stage) const
	{
		LUX_CORE_VERIFY(m_ShaderHandles.contains(stage));
		return m_ShaderHandles.at(stage);
	}

}
