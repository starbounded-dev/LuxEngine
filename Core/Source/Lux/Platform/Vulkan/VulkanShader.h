// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <filesystem>
#include <unordered_set>

#include "Lux/Renderer/Shader.h"
#include "VulkanShaderResource.h"

#include "nvrhi/nvrhi.h"

#include <array>

namespace nri {
	struct DescriptorPoolDesc;
	struct PipelineLayout;
}

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
		static constexpr uint32_t k_NoNRISet = UINT32_MAX;
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

		ShaderResource::UniformBuffer& GetUniformBuffer(const uint32_t binding = 0, const uint32_t set = 0) { LUX_CORE_ASSERT(m_ReflectionData.ShaderDescriptorSets.at(set).UniformBuffers.size() > binding); return m_ReflectionData.ShaderDescriptorSets.at(set).UniformBuffers.at(binding); }
		uint32_t GetUniformBufferCount(const uint32_t set = 0)
		{
			if (m_ReflectionData.ShaderDescriptorSets.size() < set)
				return 0;

			return (uint32_t)m_ReflectionData.ShaderDescriptorSets[set].UniformBuffers.size();
		}

		const std::vector<ShaderResource::ShaderDescriptorSet>& GetShaderDescriptorSets() const { return m_ReflectionData.ShaderDescriptorSets; }

		const std::vector<ShaderResource::PushConstantRange>& GetPushConstantRanges() const { return m_ReflectionData.PushConstantRanges; }

		// The SPIR-V the driver gets for `stage` (empty if the shader has no such stage).
		const std::vector<uint32_t>& GetSPIRV(ShaderStage stage) const;

		// NRI pipeline layout built from the reflection (NRI migration Phase 9): one descriptor set per
		// declared set number (register space = set number, StageBits::ALL so equal bindings mean
		// compatible set layouts), the shared bindless range for set 4, one root-constant block for
		// the push constants. Null if NRI rejected it.
		nri::PipelineLayout* GetNRIPipelineLayout() const { return m_NRIPipelineLayout; }
		// Position of descriptor set `set` in the NRI layout (SetDescriptorSetDesc::setIndex), or
		// k_NoNRISet when the shader declares nothing there.
		uint32_t GetNRISetIndex(uint32_t set) const { return set < m_NRISetIndices.size() ? m_NRISetIndices[set] : k_NoNRISet; }
		// Range index of `binding` within NRI set `set` (UpdateDescriptorRangeDesc::rangeIndex), or
		// k_NoNRISet when the set has no such binding.
		uint32_t GetNRIRangeIndex(uint32_t set, uint32_t binding) const;
		// A pool that holds `instanceCount` copies of set `set`.
		nri::DescriptorPoolDesc GetNRIPoolDesc(uint32_t set, uint32_t instanceCount) const;
		bool HasNRIRootConstants() const { return !m_ReflectionData.PushConstantRanges.empty(); }
		// True when a set built for this shader's set `set` can be bound in `other`'s layout: both
		// declare the same ranges there (and, for set 0, the same root constants, as the NVRHI binding
		// layouts compared their push constants).
		bool IsNRISetCompatible(uint32_t set, const VulkanShader& other) const;
	private:
		// The SPIR-V of every stage, which NRI pipelines are created from.
		void SetShaderData(const std::map<ShaderStage, std::vector<uint32_t>>& shaderData);
		void CreateDescriptors();
		void CreateNRIPipelineLayout();
		void ReleaseNRIPipelineLayout();
	private:
		std::filesystem::path m_AssetPath;
		std::string m_Name;
		bool m_DisableOptimization = false;

		std::map<ShaderStage, std::vector<uint32_t>> m_ShaderData;
		ReflectionData m_ReflectionData;

		// One NRI descriptor range, as the layout declares it (NRI types stay out of this header).
		struct NRIRange
		{
			uint32_t Binding = 0;
			uint32_t DescriptorNum = 0;
			uint32_t Type = 0;
			uint32_t Flags = 0;
			bool operator==(const NRIRange&) const = default;
		};

		nri::PipelineLayout* m_NRIPipelineLayout = nullptr;
		std::array<uint32_t, MaxDescriptorSets> m_NRISetIndices = MakeNoNRISets();
		// Per set number: the NRI ranges, in binding (= range) order.
		std::array<std::vector<NRIRange>, MaxDescriptorSets> m_NRIRanges;
		uint32_t m_NRIRootConstantSize = 0;

		static constexpr std::array<uint32_t, MaxDescriptorSets> MakeNoNRISets()
		{
			std::array<uint32_t, MaxDescriptorSets> sets = {};
			sets.fill(k_NoNRISet);
			return sets;
		}
	private:
		friend class ShaderCache;
		friend class ShaderPack;
		friend class VulkanShaderCompiler;
	};

}
