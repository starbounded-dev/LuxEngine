#pragma once

#include <filesystem>
#include <unordered_set>

#include "Lux/Renderer/Shader.h"
#include "VulkanShaderResource.h"

#include "nvrhi/nvrhi.h"

#include "VulkanMemoryAllocator/vk_mem_alloc.h"

namespace Lux {

	class VulkanShader : public Shader
	{
	public:
		struct ReflectionData
		{
			std::vector<ShaderResource::ShaderDescriptorSet> ShaderDescriptorSets;
			std::unordered_map<std::string, ShaderResourceDeclaration> Resources;
			std::unordered_map<std::string, ShaderBuffer> ConstantBuffers;
			std::vector<ShaderResource::PushConstantRange> PushConstantRanges;
		};
	public:
		VulkanShader() = default;
		VulkanShader(const std::string& path, bool forceCompile, bool disableOptimization);
		virtual ~VulkanShader();
		void Release();

		void Reload(bool forceCompile = false) override;
		void RT_Reload(bool forceCompile) override;

		virtual size_t GetHash() const override;
		void SetMacro(const std::string& name, const std::string& value) override {}

		virtual const std::string& GetName() const override { return m_Name; }
		virtual const std::unordered_map<std::string, ShaderBuffer>& GetShaderBuffers() const override { return m_ReflectionData.ConstantBuffers; }
		virtual const std::unordered_map<std::string, ShaderResourceDeclaration>& GetResources() const override;
		virtual void AddShaderReloadedCallback(const ShaderReloadedCallback& callback) override;

		bool TryReadReflectionData(StreamReader* serializer);

		void SerializeReflectionData(StreamWriter* serializer);

		void SetReflectionData(const ReflectionData& reflectionData);

		virtual nvrhi::ShaderHandle GetHandle() const override { return m_ShaderHandles.begin()->second; }
		virtual nvrhi::ShaderHandle GetHandle(nvrhi::ShaderType type) const override;
		virtual const std::map<nvrhi::ShaderType, nvrhi::ShaderHandle>& GetHandles() const override { return m_ShaderHandles; }

		// Vulkan-specific
		const std::vector<VkPipelineShaderStageCreateInfo>& GetPipelineShaderStageCreateInfos() const { return m_PipelineShaderStageCreateInfos; }

		VkDescriptorSet GetDescriptorSet() { return m_DescriptorSet; }

		nvrhi::BindingLayoutHandle GetDescriptorSetLayout(uint32_t set = 0) { return m_DescriptorSetLayouts[set]; }
		const nvrhi::BindingLayoutVector& GetAllDescriptorSetLayouts() const { return m_DescriptorSetLayouts; }

		ShaderResource::UniformBuffer& GetUniformBuffer(const uint32_t binding = 0, const uint32_t set = 0) { LUX_CORE_ASSERT(m_ReflectionData.ShaderDescriptorSets.at(set).UniformBuffers.size() > binding); return m_ReflectionData.ShaderDescriptorSets.at(set).UniformBuffers.at(binding); }
		uint32_t GetUniformBufferCount(const uint32_t set = 0)
		{
			if (m_ReflectionData.ShaderDescriptorSets.size() < set)
				return 0;

			return (uint32_t)m_ReflectionData.ShaderDescriptorSets[set].UniformBuffers.size();
		}

		const std::vector<ShaderResource::ShaderDescriptorSet>& GetShaderDescriptorSets() const { return m_ReflectionData.ShaderDescriptorSets; }
		bool HasDescriptorSet(uint32_t set) const { return m_TypeCounts.find(set) != m_TypeCounts.end(); }

		const std::vector<ShaderResource::PushConstantRange>& GetPushConstantRanges() const { return m_ReflectionData.PushConstantRanges; }

		struct ShaderMaterialDescriptorSet
		{
			VkDescriptorPool Pool = nullptr;
			std::vector<VkDescriptorSet> DescriptorSets;
		};

#if OLD
		ShaderMaterialDescriptorSet AllocateDescriptorSet(uint32_t set = 0);
		ShaderMaterialDescriptorSet CreateDescriptorSets(uint32_t set = 0);
		ShaderMaterialDescriptorSet CreateDescriptorSets(uint32_t set, uint32_t numberOfSets);
#endif

		const VkWriteDescriptorSet* GetDescriptorSet(const std::string& name, uint32_t set = 0) const;
	private:
		void LoadAndCreateShaders(const std::map<nvrhi::ShaderType, std::vector<uint32_t>>& shaderData);
		void CreateDescriptors();
	private:
		std::map<nvrhi::ShaderType, nvrhi::ShaderHandle> m_ShaderHandles;

		std::vector<VkPipelineShaderStageCreateInfo> m_PipelineShaderStageCreateInfos;

		std::filesystem::path m_AssetPath;
		std::string m_Name;
		bool m_DisableOptimization = false;

		std::map<nvrhi::ShaderType, std::vector<uint32_t>> m_ShaderData;
		ReflectionData m_ReflectionData;

		nvrhi::BindingLayoutVector m_DescriptorSetLayouts;
		VkDescriptorSet m_DescriptorSet;

		std::unordered_map<uint32_t, std::vector<VkDescriptorPoolSize>> m_TypeCounts;
	private:
		friend class ShaderCache;
		friend class ShaderPack;
		friend class VulkanShaderCompiler;
	};

}
