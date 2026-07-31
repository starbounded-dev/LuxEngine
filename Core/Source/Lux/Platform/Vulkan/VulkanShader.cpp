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
		LUX_PROFILE_FUNCTION_AUTO;
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
		// Capture the shader-module handles by value — never Ref(this). The object is already being
		// destroyed, so resurrecting it with a Ref bumps the refcount 0->1; the deferred free then
		// runs (during Renderer::Shutdown) on freed memory and deletes the shader a SECOND time when
		// that Ref dies -> the heap corruption / double-free of the member maps seen at shutdown.
		// Match Release()'s by-value capture.
		auto& pipelineCIs = m_PipelineShaderStageCreateInfos;
		Renderer::SubmitResourceFree([pipelineCIs]()
			{
				auto* deviceManager = Application::Get().GetWindow().GetDeviceManager();
				if (!deviceManager || !deviceManager->GetDevice())
					return;

				VkDevice device = (VkDevice)deviceManager->GetDevice()->getNativeObject(nvrhi::ObjectTypes::VK_Device);
				for (const auto& ci : pipelineCIs)
					if (ci.module)
						vkDestroyShaderModule(device, ci.module, nullptr);
			});
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

	void VulkanShader::LoadAndCreateShaders(const std::map<nvrhi::ShaderType, std::vector<uint32_t>>& shaderData)
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
			desc.shaderType = stage;
			desc.debugName = m_Name;
			desc.entryName = "main";
			m_ShaderHandles[stage] = device->createShader(desc, data.data(), data.size() * sizeof(uint32_t));
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

	nvrhi::ShaderHandle VulkanShader::GetHandle(nvrhi::ShaderType type) const
	{
		LUX_CORE_VERIFY(m_ShaderHandles.contains(type));
		return m_ShaderHandles.at(type);
	}

}
