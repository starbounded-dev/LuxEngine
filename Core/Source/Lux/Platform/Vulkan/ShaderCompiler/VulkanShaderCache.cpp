// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "VulkanShaderCache.h"
#include "Lux/Core/Hash.h"
#include "Lux/Platform/Vulkan/VulkanShaderUtils.h"

#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/SerializationMacros.h"

#include "ShaderPreprocessing/ShaderPreprocessor.h"

namespace Lux {

	static const char* s_ShaderRegistryPath = "Resources/Cache/Shader/ShaderRegistry.cache";

	ShaderStage VulkanShaderCache::HasChanged(Ref<VulkanShaderCompiler> shader)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::map<std::string, std::map<ShaderStage, StageData>> shaderCache;

		Deserialize(shaderCache);

		ShaderStage changedStages = ShaderStage::None;
		const bool shaderNotCached = shaderCache.find(shader->m_ShaderSourcePath.string()) == shaderCache.end();

		for (const auto& [stage, stageSource] : shader->m_ShaderSource)
		{
			// Keep in mind that we're using the [] operator.
			// Which means that we add the stage if it's not already there.
			if (shaderNotCached || shader->m_StagesMetadata.at(stage) != shaderCache[shader->m_ShaderSourcePath.string()][stage])
			{
				shaderCache[shader->m_ShaderSourcePath.string()][stage] = shader->m_StagesMetadata.at(stage);
				*(uint16_t*)&changedStages |= (uint16_t)stage;
			}
		}

		// Update cache in case we added a stage but didn't remove the deleted(in file) stages
		shaderCache.at(shader->m_ShaderSourcePath.string()) = shader->m_StagesMetadata;

		if (changedStages != ShaderStage::None)
			Serialize(shaderCache);

		return changedStages;
	}


	void VulkanShaderCache::Serialize(const std::map<std::string, std::map<ShaderStage, StageData>>& shaderCache)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		Yaml::Writer out;

		out << Yaml::BeginMap << Yaml::Key << "ShaderRegistry" << Yaml::BeginSeq;// ShaderRegistry_

		for (auto& [filepath, shader] : shaderCache)
		{
			out << Yaml::BeginMap; // Shader_

			out << Yaml::Key << "ShaderPath" << Yaml::Value << filepath;

			out << Yaml::Key << "Stages" << Yaml::BeginSeq; // Stages_

			for (auto& [stage, stageData] : shader)
			{
				out << Yaml::BeginMap; // Stage_

				out << Yaml::Key << "Stage" << Yaml::Value << ShaderStageToString(stage);
				out << Yaml::Key << "StageHash" << Yaml::Value << stageData.HashValue;

				out << Yaml::Key << "Headers" << Yaml::BeginSeq; // Headers_
				for (auto& header : stageData.Headers)
				{

					out << Yaml::BeginMap;

					LUX_SERIALIZE_PROPERTY(HeaderPath, header.IncludedFilePath.string(), out);
					LUX_SERIALIZE_PROPERTY(IncludeDepth, header.IncludeDepth, out);
					LUX_SERIALIZE_PROPERTY(IsRelative, header.IsRelative, out);
					LUX_SERIALIZE_PROPERTY(IsGaurded, header.IsGuarded, out);
					LUX_SERIALIZE_PROPERTY(HashValue, header.HashValue, out);

					out << Yaml::EndMap;
				}
				out << Yaml::EndSeq; // Headers_

				out << Yaml::EndMap; // Stage_
			}
			out << Yaml::EndSeq; // Stages_
			out << Yaml::EndMap; // Shader_

		}
		out << Yaml::EndSeq; // ShaderRegistry_
		out << Yaml::EndMap; // File_

		std::ofstream fout(s_ShaderRegistryPath);
		fout << out.c_str();
	}

	void VulkanShaderCache::Deserialize(std::map<std::string, std::map<ShaderStage, StageData>>& shaderCache)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// Read registry
		std::ifstream stream(s_ShaderRegistryPath);
		if (!stream.good())
			return;

		std::stringstream strStream;
		strStream << stream.rdbuf();

		Yaml::Node data = Yaml::Load(strStream.str());
		auto handles = data["ShaderRegistry"];
		if (!handles || handles.IsNull())
		{
		LUX_CORE_ERROR("[ShaderCache] Shader Registry is invalid.");
		return;
	}

	// Old format
	if (handles.IsMap())
	{
		LUX_CORE_ERROR("[ShaderCache] Old Shader Registry format.");
			return;
		}

		for (auto shader : handles)
		{
			std::string path;
			LUX_DESERIALIZE_PROPERTY(ShaderPath, path, shader, std::string());
			for (auto stage : shader["Stages"]) //Stages
			{
				std::string stageType;
				uint32_t stageHash;
				LUX_DESERIALIZE_PROPERTY(Stage, stageType, stage, std::string());
				LUX_DESERIALIZE_PROPERTY(StageHash, stageHash, stage, 0u);

				auto& stageCache = shaderCache[path][ShaderStageFromString(stageType)];
				stageCache.HashValue = stageHash;

				for (auto header : stage["Headers"])
				{
					std::string headerPath;
					uint32_t includeDepth;
					bool isRelative;
					bool isGuarded;
					uint32_t hashValue;
					LUX_DESERIALIZE_PROPERTY(HeaderPath, headerPath, header, std::string());
					LUX_DESERIALIZE_PROPERTY(IncludeDepth, includeDepth, header, 0u);
					LUX_DESERIALIZE_PROPERTY(IsRelative, isRelative, header, false);
					LUX_DESERIALIZE_PROPERTY(IsGaurded, isGuarded, header, false);
					LUX_DESERIALIZE_PROPERTY(HashValue, hashValue, header, 0u);

					stageCache.Headers.emplace(IncludeData{ headerPath, includeDepth, isRelative, isGuarded, hashValue });

				}

			}

		}

	}

}
