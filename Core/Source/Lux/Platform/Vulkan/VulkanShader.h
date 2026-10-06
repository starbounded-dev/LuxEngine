// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <filesystem>
#include <unordered_set>

#include "Lux/Renderer/Shader.h"
#include "VulkanShaderResource.h"

#include "nvrhi/nvrhi.h"

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
		virtual ~VulkanShader() = default;
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

		nvrhi::ShaderHandle GetHandle() const { return m_ShaderHandles.begin()->second; }
		nvrhi::ShaderHandle GetHandle(ShaderStage stage) const;
		const std::map<ShaderStage, nvrhi::ShaderHandle>& GetHandles() const { return m_ShaderHandles; }

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

		const std::vector<ShaderResource::PushConstantRange>& GetPushConstantRanges() const { return m_ReflectionData.PushConstantRanges; }
	private:
		void LoadAndCreateShaders(const std::map<ShaderStage, std::vector<uint32_t>>& shaderData);
		void CreateDescriptors();
	private:
		std::map<ShaderStage, nvrhi::ShaderHandle> m_ShaderHandles;

		std::filesystem::path m_AssetPath;
		std::string m_Name;
		bool m_DisableOptimization = false;

		std::map<ShaderStage, std::vector<uint32_t>> m_ShaderData;
		ReflectionData m_ReflectionData;

		nvrhi::BindingLayoutVector m_DescriptorSetLayouts;
	private:
		friend class ShaderCache;
		friend class ShaderPack;
		friend class VulkanShaderCompiler;
	};

}
