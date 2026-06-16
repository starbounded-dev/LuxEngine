#include "lpch.h"
#include "VulkanShader.h"

#include "VulkanShaderUtils.h"

#if LUX_HAS_SHADER_COMPILER
#include "ShaderCompiler/VulkanShaderCompiler.h"
#endif

#include "Lux/Core/Application.h"
#include "Lux/Core/Hash.h"
#include "Lux/ImGui/ImGuiCore.h"
#include "Lux/Platform/Vulkan/VulkanContext.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Utilities/StringUtils.h"

#include <filesystem>
#include <format>

namespace Lux {

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

	void VulkanShader::Release()
	{
		auto& pipelineCIs = m_PipelineShaderStageCreateInfos;
		Renderer::SubmitResourceFree([pipelineCIs]()
		{
			auto* deviceManager = Application::Get().GetWindow().GetDeviceManager();
			if (!deviceManager || !deviceManager->GetDevice())
				return;

			VkDevice vulkanDevice = (VkDevice)deviceManager->GetDevice()->getNativeObject(nvrhi::ObjectTypes::VK_Device);

			for (const auto& ci : pipelineCIs)
				if (ci.module)
					vkDestroyShaderModule(vulkanDevice, ci.module, nullptr);
		});

		for (auto& ci : pipelineCIs)
			ci.module = nullptr;

		m_PipelineShaderStageCreateInfos.clear();
		m_DescriptorSetLayouts.resize(0);
		m_TypeCounts.clear();
	}

	VulkanShader::~VulkanShader()
	{
		Renderer::SubmitResourceFree([instance = Ref(this)]()
			{
				auto* deviceManager = Application::Get().GetWindow().GetDeviceManager();
				if (!deviceManager || !deviceManager->GetDevice())
					return;

				VkDevice device = (VkDevice)deviceManager->GetDevice()->getNativeObject(nvrhi::ObjectTypes::VK_Device);
				for (const auto& ci : instance->m_PipelineShaderStageCreateInfos)
					if (ci.module)
						vkDestroyShaderModule(device, ci.module, nullptr);
			});
	}

	void VulkanShader::RT_Reload(const bool forceCompile)
	{
#if LUX_HAS_SHADER_COMPILER 
		if (!VulkanShaderCompiler::TryRecompile(this))
		{
			LUX_CORE_FATAL("Failed to recompile shader!");
		}
#endif
	}

	void VulkanShader::Reload(bool forceCompile)
	{
		Renderer::Submit([instance = Ref(this), forceCompile]() mutable
		{
			instance->RT_Reload(forceCompile);
		});
	}

	size_t VulkanShader::GetHash() const
	{
		return Hash::GenerateFNVHash(m_AssetPath.string());
	}

	void VulkanShader::LoadAndCreateShaders(const std::map<nvrhi::ShaderType, std::vector<uint32_t>>& shaderData)
	{
		m_ShaderData = shaderData;

		nvrhi::IDevice* device = Application::Get().GetWindow().GetDeviceManager()->GetDevice();
#if OLD
		VkDevice device = VulkanContext::GetCurrentDevice()->GetVulkanDevice();
		m_PipelineShaderStageCreateInfos.clear();
#endif
		m_ShaderHandles.clear();

		std::string moduleName;
		for (auto [stage, data] : shaderData)
		{
			LUX_CORE_ASSERT(data.size());

#if OLD
			VkShaderModuleCreateInfo moduleCreateInfo{};

			moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleCreateInfo.codeSize = data.size() * sizeof(uint32_t);
			moduleCreateInfo.pCode = data.data();

			VkShaderModule shaderModule;
			VK_CHECK_RESULT(vkCreateShaderModule(device, &moduleCreateInfo, NULL, &shaderModule));
			VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_SHADER_MODULE, std::format("{}:{}", m_Name, ShaderUtils::ShaderStageToString(stage)), shaderModule);

			VkPipelineShaderStageCreateInfo& shaderStage = m_PipelineShaderStageCreateInfos.emplace_back();
			shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			shaderStage.stage = stage;
			shaderStage.module = shaderModule;
			shaderStage.pName = "main";
#endif

			nvrhi::ShaderDesc desc;
			desc.shaderType = stage;
			desc.debugName = m_Name;
			desc.entryName = "main";
			m_ShaderHandles[stage] = device->createShader(desc, data.data(), data.size() * sizeof(uint32_t));
		}
	}


	
	void VulkanShader::CreateDescriptors()
	{
		//LUX_CORE_VERIFY(false);
#if OLD
		VkDevice device = VulkanContext::GetCurrentDevice()->GetVulkanDevice();

		//////////////////////////////////////////////////////////////////////
		// Descriptor Pool
		//////////////////////////////////////////////////////////////////////

		m_TypeCounts.clear();
		for (uint32_t set = 0; set < m_ReflectionData.ShaderDescriptorSets.size(); set++)
		{
			auto& shaderDescriptorSet = m_ReflectionData.ShaderDescriptorSets[set];

			if (shaderDescriptorSet.UniformBuffers.size())
			{
				VkDescriptorPoolSize& typeCount = m_TypeCounts[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				typeCount.descriptorCount = (uint32_t)(shaderDescriptorSet.UniformBuffers.size());
			}
			if (shaderDescriptorSet.StorageBuffers.size())
			{
				VkDescriptorPoolSize& typeCount = m_TypeCounts[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				typeCount.descriptorCount = (uint32_t)(shaderDescriptorSet.StorageBuffers.size());
			}
			if (shaderDescriptorSet.ImageSamplers.size())
			{
				VkDescriptorPoolSize& typeCount = m_TypeCounts[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				typeCount.descriptorCount = (uint32_t)(shaderDescriptorSet.ImageSamplers.size());
			}
			if (shaderDescriptorSet.SeparateTextures.size())
			{
				VkDescriptorPoolSize& typeCount = m_TypeCounts[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				typeCount.descriptorCount = (uint32_t)(shaderDescriptorSet.SeparateTextures.size());
			}
			if (shaderDescriptorSet.SeparateSamplers.size())
			{
				VkDescriptorPoolSize& typeCount = m_TypeCounts[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_SAMPLER;
				typeCount.descriptorCount = (uint32_t)(shaderDescriptorSet.SeparateSamplers.size());
			}
			if (shaderDescriptorSet.StorageImages.size())
			{
				VkDescriptorPoolSize& typeCount = m_TypeCounts[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				typeCount.descriptorCount = (uint32_t)(shaderDescriptorSet.StorageImages.size());
			}

#if 0
			// TODO: Move this to the centralized renderer
			VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
			descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			descriptorPoolInfo.pNext = nullptr;
			descriptorPoolInfo.poolSizeCount = m_TypeCounts.size();
			descriptorPoolInfo.pPoolSizes = m_TypeCounts.data();
			descriptorPoolInfo.maxSets = 1;

			VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &m_DescriptorPool));
#endif

			//////////////////////////////////////////////////////////////////////
			// Descriptor Set Layout
			//////////////////////////////////////////////////////////////////////


			std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
			for (auto& [binding, uniformBuffer] : shaderDescriptorSet.UniformBuffers)
			{
				VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings.emplace_back();
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				layoutBinding.descriptorCount = 1;
				layoutBinding.stageFlags = uniformBuffer.ShaderStage;
				layoutBinding.pImmutableSamplers = nullptr;
				layoutBinding.binding = binding;

				VkWriteDescriptorSet& writeDescriptorSet = shaderDescriptorSet.WriteDescriptorSets[uniformBuffer.Name];
				writeDescriptorSet = {};
				writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSet.descriptorType = layoutBinding.descriptorType;
				writeDescriptorSet.descriptorCount = 1;
				writeDescriptorSet.dstBinding = layoutBinding.binding;
			}

			for (auto& [binding, storageBuffer] : shaderDescriptorSet.StorageBuffers)
			{
				VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings.emplace_back();
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				layoutBinding.descriptorCount = 1;
				layoutBinding.stageFlags = storageBuffer.ShaderStage;
				layoutBinding.pImmutableSamplers = nullptr;
				layoutBinding.binding = binding;
				LUX_CORE_ASSERT(shaderDescriptorSet.UniformBuffers.find(binding) == shaderDescriptorSet.UniformBuffers.end(), "Binding is already present!");

				VkWriteDescriptorSet& writeDescriptorSet = shaderDescriptorSet.WriteDescriptorSets[storageBuffer.Name];
				writeDescriptorSet = {};
				writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSet.descriptorType = layoutBinding.descriptorType;
				writeDescriptorSet.descriptorCount = 1;
				writeDescriptorSet.dstBinding = layoutBinding.binding;
			}

			for (auto& [binding, imageSampler] : shaderDescriptorSet.ImageSamplers)
			{
				auto& layoutBinding = layoutBindings.emplace_back();
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				layoutBinding.descriptorCount = imageSampler.ArraySize;
				layoutBinding.stageFlags = imageSampler.ShaderStage;
				layoutBinding.pImmutableSamplers = nullptr;
				layoutBinding.binding = binding;

				LUX_CORE_ASSERT(shaderDescriptorSet.UniformBuffers.find(binding) == shaderDescriptorSet.UniformBuffers.end(), "Binding is already present!");
				LUX_CORE_ASSERT(shaderDescriptorSet.StorageBuffers.find(binding) == shaderDescriptorSet.StorageBuffers.end(), "Binding is already present!");

				VkWriteDescriptorSet& writeDescriptorSet = shaderDescriptorSet.WriteDescriptorSets[imageSampler.Name];
				writeDescriptorSet = {};
				writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSet.descriptorType = layoutBinding.descriptorType;
				writeDescriptorSet.descriptorCount = layoutBinding.descriptorCount;
				writeDescriptorSet.dstBinding = layoutBinding.binding;
			}

			for (auto& [binding, imageSampler] : shaderDescriptorSet.SeparateTextures)
			{
				auto& layoutBinding = layoutBindings.emplace_back();
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				layoutBinding.descriptorCount = imageSampler.ArraySize;
				layoutBinding.stageFlags = imageSampler.ShaderStage;
				layoutBinding.pImmutableSamplers = nullptr;
				layoutBinding.binding = binding;

				LUX_CORE_ASSERT(shaderDescriptorSet.UniformBuffers.find(binding) == shaderDescriptorSet.UniformBuffers.end(), "Binding is already present!");
				LUX_CORE_ASSERT(shaderDescriptorSet.ImageSamplers.find(binding) == shaderDescriptorSet.ImageSamplers.end(), "Binding is already present!");
				LUX_CORE_ASSERT(shaderDescriptorSet.StorageBuffers.find(binding) == shaderDescriptorSet.StorageBuffers.end(), "Binding is already present!");

				VkWriteDescriptorSet& writeDescriptorSet = shaderDescriptorSet.WriteDescriptorSets[imageSampler.Name];
				writeDescriptorSet = {};
				writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSet.descriptorType = layoutBinding.descriptorType;
				writeDescriptorSet.descriptorCount = imageSampler.ArraySize;
				writeDescriptorSet.dstBinding = layoutBinding.binding;
			}

			for (auto& [binding, imageSampler] : shaderDescriptorSet.SeparateSamplers)
			{
				auto& layoutBinding = layoutBindings.emplace_back();
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				layoutBinding.descriptorCount = imageSampler.ArraySize;
				layoutBinding.stageFlags = imageSampler.ShaderStage;
				layoutBinding.pImmutableSamplers = nullptr;
				layoutBinding.binding = binding;

				LUX_CORE_ASSERT(shaderDescriptorSet.UniformBuffers.find(binding) == shaderDescriptorSet.UniformBuffers.end(), "Binding is already present!");
				LUX_CORE_ASSERT(shaderDescriptorSet.ImageSamplers.find(binding) == shaderDescriptorSet.ImageSamplers.end(), "Binding is already present!");
				LUX_CORE_ASSERT(shaderDescriptorSet.StorageBuffers.find(binding) == shaderDescriptorSet.StorageBuffers.end(), "Binding is already present!");
				LUX_CORE_ASSERT(shaderDescriptorSet.SeparateTextures.find(binding) == shaderDescriptorSet.SeparateTextures.end(), "Binding is already present!");

				VkWriteDescriptorSet& writeDescriptorSet = shaderDescriptorSet.WriteDescriptorSets[imageSampler.Name];
				writeDescriptorSet = {};
				writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSet.descriptorType = layoutBinding.descriptorType;
				writeDescriptorSet.descriptorCount = imageSampler.ArraySize;
				writeDescriptorSet.dstBinding = layoutBinding.binding;
			}

			for (auto& [bindingAndSet, imageSampler] : shaderDescriptorSet.StorageImages)
			{
				auto& layoutBinding = layoutBindings.emplace_back();
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				layoutBinding.descriptorCount = imageSampler.ArraySize;
				layoutBinding.stageFlags = imageSampler.ShaderStage;
				layoutBinding.pImmutableSamplers = nullptr;

				uint32_t binding = bindingAndSet & 0xffffffff;
				//uint32_t descriptorSet = (bindingAndSet >> 32);
				layoutBinding.binding = binding;

				LUX_CORE_ASSERT(shaderDescriptorSet.UniformBuffers.find(binding) == shaderDescriptorSet.UniformBuffers.end(), "Binding is already present!");
				LUX_CORE_ASSERT(shaderDescriptorSet.StorageBuffers.find(binding) == shaderDescriptorSet.StorageBuffers.end(), "Binding is already present!");
				LUX_CORE_ASSERT(shaderDescriptorSet.ImageSamplers.find(binding) == shaderDescriptorSet.ImageSamplers.end(), "Binding is already present!");
				LUX_CORE_ASSERT(shaderDescriptorSet.SeparateTextures.find(binding) == shaderDescriptorSet.SeparateTextures.end(), "Binding is already present!");
				LUX_CORE_ASSERT(shaderDescriptorSet.SeparateSamplers.find(binding) == shaderDescriptorSet.SeparateSamplers.end(), "Binding is already present!");

				VkWriteDescriptorSet& writeDescriptorSet = shaderDescriptorSet.WriteDescriptorSets[imageSampler.Name];
				writeDescriptorSet = {};
				writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSet.descriptorType = layoutBinding.descriptorType;
				writeDescriptorSet.descriptorCount = layoutBinding.descriptorCount;
				writeDescriptorSet.dstBinding = layoutBinding.binding;
			}

			VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
			descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorLayout.pNext = nullptr;
			descriptorLayout.bindingCount = (uint32_t)(layoutBindings.size());
			descriptorLayout.pBindings = layoutBindings.data();

			if (set >= m_DescriptorSetLayouts.size())
				m_DescriptorSetLayouts.resize((size_t)(set + 1));
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &m_DescriptorSetLayouts[set]));
		}
#endif
		nvrhi::DeviceHandle device = Application::GetGraphicsDevice();

		m_DescriptorSetLayouts.resize(m_ReflectionData.ShaderDescriptorSets.size());
		for (uint32_t set = 0; set < m_ReflectionData.ShaderDescriptorSets.size(); set++)
		{
			auto& shaderDescriptorSet = m_ReflectionData.ShaderDescriptorSets[set];
			
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

	}

#if OLD

	VulkanShader::ShaderMaterialDescriptorSet VulkanShader::AllocateDescriptorSet(uint32_t set)
	{
		LUX_CORE_ASSERT(set < m_DescriptorSetLayouts.size());
		ShaderMaterialDescriptorSet result;

		if (m_ReflectionData.ShaderDescriptorSets.empty())
			return result;

		// TODO: remove
		result.Pool = nullptr;

		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_DescriptorSetLayouts[set];
		VkDescriptorSet descriptorSet = VulkanRenderer::RT_AllocateDescriptorSet(allocInfo);
		LUX_CORE_ASSERT(descriptorSet);
		result.DescriptorSets.push_back(descriptorSet);
		return result;
	}

	VulkanShader::ShaderMaterialDescriptorSet VulkanShader::CreateDescriptorSets(uint32_t set)
	{
		ShaderMaterialDescriptorSet result;

		VkDevice device = VulkanContext::GetCurrentDevice()->GetVulkanDevice();

		LUX_CORE_ASSERT(m_TypeCounts.find(set) != m_TypeCounts.end());

		// TODO: Move this to the centralized renderer
		VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
		descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolInfo.pNext = nullptr;
		descriptorPoolInfo.poolSizeCount = (uint32_t)m_TypeCounts.at(set).size();
		descriptorPoolInfo.pPoolSizes = m_TypeCounts.at(set).data();
		descriptorPoolInfo.maxSets = 1;

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &result.Pool));

		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = result.Pool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_DescriptorSetLayouts[set];

		result.DescriptorSets.emplace_back();
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, result.DescriptorSets.data()));
		return result;
	}

	VulkanShader::ShaderMaterialDescriptorSet VulkanShader::CreateDescriptorSets(uint32_t set, uint32_t numberOfSets)
	{
		ShaderMaterialDescriptorSet result;

		VkDevice device = VulkanContext::GetCurrentDevice()->GetVulkanDevice();

		std::unordered_map<uint32_t, std::vector<VkDescriptorPoolSize>> poolSizes;
		for (uint32_t set = 0; set < m_ReflectionData.ShaderDescriptorSets.size(); set++)
		{
			auto& shaderDescriptorSet = m_ReflectionData.ShaderDescriptorSets[set];
			if (!shaderDescriptorSet) // Empty descriptor set
				continue;

			if (shaderDescriptorSet.UniformBuffers.size())
			{
				VkDescriptorPoolSize& typeCount = poolSizes[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				typeCount.descriptorCount = (uint32_t)shaderDescriptorSet.UniformBuffers.size() * numberOfSets;
			}
			if (shaderDescriptorSet.StorageBuffers.size())
			{
				VkDescriptorPoolSize& typeCount = poolSizes[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				typeCount.descriptorCount = (uint32_t)shaderDescriptorSet.StorageBuffers.size() * numberOfSets;
			}
			if (shaderDescriptorSet.ImageSamplers.size())
			{
				VkDescriptorPoolSize& typeCount = poolSizes[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				uint32_t descriptorSetCount = 0;
				for (auto&& [binding, imageSampler] : shaderDescriptorSet.ImageSamplers)
					descriptorSetCount += imageSampler.ArraySize;

				typeCount.descriptorCount = descriptorSetCount * numberOfSets;
			}
			if (shaderDescriptorSet.SeparateTextures.size())
			{
				VkDescriptorPoolSize& typeCount = poolSizes[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				uint32_t descriptorSetCount = 0;
				for (auto&& [binding, imageSampler] : shaderDescriptorSet.SeparateTextures)
					descriptorSetCount += imageSampler.ArraySize;

				typeCount.descriptorCount = descriptorSetCount * numberOfSets;
			}
			if (shaderDescriptorSet.SeparateTextures.size())
			{
				VkDescriptorPoolSize& typeCount = poolSizes[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_SAMPLER;
				uint32_t descriptorSetCount = 0;
				for (auto&& [binding, imageSampler] : shaderDescriptorSet.SeparateSamplers)
					descriptorSetCount += imageSampler.ArraySize;

				typeCount.descriptorCount = descriptorSetCount * numberOfSets;
			}
			if (shaderDescriptorSet.StorageImages.size())
			{
				VkDescriptorPoolSize& typeCount = poolSizes[set].emplace_back();
				typeCount.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				typeCount.descriptorCount = (uint32_t)shaderDescriptorSet.StorageImages.size() * numberOfSets;
			}

		}

		LUX_CORE_ASSERT(poolSizes.find(set) != poolSizes.end());

		// TODO: Move this to the centralized renderer
		VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
		descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolInfo.pNext = nullptr;
		descriptorPoolInfo.poolSizeCount = (uint32_t)poolSizes.at(set).size();
		descriptorPoolInfo.pPoolSizes = poolSizes.at(set).data();
		descriptorPoolInfo.maxSets = numberOfSets;

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &result.Pool));

		result.DescriptorSets.resize(numberOfSets);

		for (uint32_t i = 0; i < numberOfSets; i++)
		{
			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = result.Pool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &m_DescriptorSetLayouts[set];

			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &result.DescriptorSets[i]));
		}
		return result;
	}

	const VkWriteDescriptorSet* VulkanShader::GetDescriptorSet(const std::string& name, uint32_t set) const
	{
		LUX_CORE_ASSERT(set < m_ReflectionData.ShaderDescriptorSets.size());
		LUX_CORE_ASSERT(m_ReflectionData.ShaderDescriptorSets[set]);
		if (m_ReflectionData.ShaderDescriptorSets.at(set).WriteDescriptorSets.find(name) == m_ReflectionData.ShaderDescriptorSets.at(set).WriteDescriptorSets.end())
		{
			LUX_CORE_WARN_TAG("Renderer", "Shader {0} does not contain requested descriptor set {1}", m_Name, name);
			return nullptr;
		}
		return &m_ReflectionData.ShaderDescriptorSets.at(set).WriteDescriptorSets.at(name);
	}

#endif

	const std::unordered_map<std::string, ShaderResourceDeclaration>& VulkanShader::GetResources() const
	{
		return m_ReflectionData.Resources;
	}

	void VulkanShader::AddShaderReloadedCallback(const ShaderReloadedCallback& callback)
	{
	}

	bool VulkanShader::TryReadReflectionData(StreamReader* serializer)
	{
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
		m_ReflectionData = reflectionData;
	}

	nvrhi::ShaderHandle VulkanShader::GetHandle(nvrhi::ShaderType type) const
	{
		LUX_CORE_VERIFY(m_ShaderHandles.contains(type));
		return m_ShaderHandles.at(type);
	}

}
