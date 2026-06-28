#include "lpch.h"

#include "Lux/Renderer/MaterialScene.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace Lux {

	namespace
	{
		static const std::string s_AlbedoColorUniform = "u_MaterialUniforms.AlbedoColor";
		static const std::string s_UseNormalMapUniform = "u_MaterialUniforms.UseNormalMap";
		static const std::string s_MetalnessUniform = "u_MaterialUniforms.Metalness";
		static const std::string s_RoughnessUniform = "u_MaterialUniforms.Roughness";
		static const std::string s_EmissionUniform = "u_MaterialUniforms.Emission";
		static const std::string s_TransparencyUniform = "u_MaterialUniforms.Transparency";
		static const std::string s_MaterialComplexityScoreUniform = "u_MaterialUniforms.MaterialComplexityScore";

		bool MaterialDataEquals(const GPUMaterialData& lhs, const GPUMaterialData& rhs)
		{
			return std::memcmp(&lhs, &rhs, sizeof(GPUMaterialData)) == 0;
		}

		glm::vec3 ToLinearColor(glm::vec3 color)
		{
			color = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
			return glm::pow(color, glm::vec3(2.2f));
		}

		float ReadMaterialFloat(Ref<Material> material, const std::string& name, float fallback)
		{
			if (!material || !material->FindUniformDeclaration(name))
				return fallback;

			return material->GetFloat(name);
		}

		bool ReadMaterialBool(Ref<Material> material, const std::string& name, bool fallback)
		{
			if (!material || !material->FindUniformDeclaration(name))
				return fallback;

			return material->GetBool(name);
		}

		glm::vec3 ReadMaterialVec3(Ref<Material> material, const std::string& name, const glm::vec3& fallback)
		{
			if (!material || !material->FindUniformDeclaration(name))
				return fallback;

			return material->GetVector3(name);
		}

		bool IsTextureHandleValid(AssetHandle textureHandle)
		{
			if (!textureHandle || !Project::GetAssetManager())
				return false;

			return AssetManager::IsAssetHandleValid(textureHandle)
				&& AssetManager::GetAssetType(textureHandle) == AssetType::Texture;
		}
	}

	MaterialScene::MaterialScene()
	{
		EnsureFallbackMaterial();
	}

	void MaterialScene::SetTextureResolver(std::function<GPUTextureIndex(AssetHandle)> textureResolver)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_TextureResolver = std::move(textureResolver);
	}

	GPUMaterialData MaterialScene::GetFallbackMaterialData()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		GPUMaterialData data;
		data.BaseColor = glm::vec4(1.0f);
		data.Scalars = glm::vec4(0.0f, 0.5f, 0.0f, 0.0f);
		data.TextureIndices = glm::uvec4(InvalidGPUTextureIndex);
		data.Metadata = glm::uvec4((uint32_t)(GPUMaterialFlags::Valid | GPUMaterialFlags::Missing), (uint32_t)GPUMaterialAlphaMode::Opaque, InvalidRenderMaterialID, 0);
		return data;
	}

	GPUMaterialData MaterialScene::BuildGPUMaterialData(
		const GPUMaterialBuildInput& input,
		const std::function<GPUTextureIndex(AssetHandle)>& resolveTextureIndex)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		GPUMaterialData data = GetFallbackMaterialData();

		Ref<MaterialAsset> materialAsset = input.MaterialAsset;
		if (!materialAsset && input.MaterialHandle && Project::GetAssetManager())
			materialAsset = AssetManager::GetAsset<MaterialAsset>(input.MaterialHandle);

		Ref<Material> material = input.OverrideMaterial;
		if (!material && materialAsset)
			material = materialAsset->GetMaterial();

		if (!material)
		{
			data.Metadata.x = (uint32_t)GPUMaterialFlags::Missing;
			return data;
		}

		const bool transparent = input.Transparent || (materialAsset && materialAsset->IsTransparent());
		const bool shadowCasting = materialAsset ? materialAsset->IsShadowCasting() : !material->GetFlag(MaterialFlag::DisableShadowCasting);
		const bool twoSided = material->GetFlag(MaterialFlag::TwoSided);

		const glm::vec3 albedoColor = ReadMaterialVec3(material, s_AlbedoColorUniform, glm::vec3(1.0f));
		const float metalness = transparent ? 0.0f : glm::clamp(ReadMaterialFloat(material, s_MetalnessUniform, 0.0f), 0.0f, 1.0f);
		const float roughness = glm::clamp(ReadMaterialFloat(material, s_RoughnessUniform, transparent ? 0.5f : 0.4f), 0.0f, 1.0f);
		const float emission = glm::max(ReadMaterialFloat(material, s_EmissionUniform, 0.0f), 0.0f);
		const float opacity = transparent ? glm::clamp(ReadMaterialFloat(material, s_TransparencyUniform, 1.0f), 0.0f, 1.0f) : 1.0f;
		const float complexity = glm::max(ReadMaterialFloat(material, s_MaterialComplexityScoreUniform, transparent ? 5.0f : 3.0f), 0.0f);

		data.BaseColor = glm::vec4(ToLinearColor(albedoColor), opacity);
		data.Scalars = glm::vec4(metalness, roughness, emission, complexity);
		data.TextureIndices = glm::uvec4(InvalidGPUTextureIndex);

		GPUMaterialFlags flags = GPUMaterialFlags::Valid;
		if (input.OverrideMaterial)
			flags |= GPUMaterialFlags::OverrideMaterial;
		if (transparent)
			flags |= GPUMaterialFlags::Transparent;
		if (shadowCasting)
			flags |= GPUMaterialFlags::ShadowCasting;
		if (twoSided)
			flags |= GPUMaterialFlags::TwoSided;

		auto assignTexture = [&](AssetHandle textureHandle, GPUMaterialFlags presentFlag, uint32_t textureSlot)
			{
				if (!textureHandle)
					return;

				if (!IsTextureHandleValid(textureHandle))
				{
					flags |= GPUMaterialFlags::MissingTexture;
					return;
				}

				flags |= presentFlag;
				data.TextureIndices[textureSlot] = resolveTextureIndex ? resolveTextureIndex(textureHandle) : InvalidGPUTextureIndex;
			};

		if (materialAsset)
		{
			assignTexture(materialAsset->GetAlbedoMapHandle(), GPUMaterialFlags::HasAlbedoTexture, 0);

			const bool useNormalMap = ReadMaterialBool(material, s_UseNormalMapUniform, false)
				&& materialAsset->GetNormalMapHandle();
			if (useNormalMap)
				flags |= GPUMaterialFlags::UseNormalMap;
			assignTexture(useNormalMap ? materialAsset->GetNormalMapHandle() : AssetHandle(0), GPUMaterialFlags::HasNormalTexture, 1);
			assignTexture(!transparent ? materialAsset->GetMetalnessMapHandle() : AssetHandle(0), GPUMaterialFlags::HasMetalnessTexture, 2);
			assignTexture(materialAsset->GetRoughnessMapHandle(), GPUMaterialFlags::HasRoughnessTexture, 3);
		}

		data.Metadata = glm::uvec4(
			(uint32_t)flags,
			(uint32_t)(transparent ? GPUMaterialAlphaMode::Blend : GPUMaterialAlphaMode::Opaque),
			InvalidRenderMaterialID,
			0);
		return data;
	}

	void MaterialScene::EnsureFallbackMaterial()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_Materials.empty())
			return;

		m_Materials.push_back(GetFallbackMaterialData());
		m_MaterialKeys.push_back({});
		m_LastTouchedFrames.push_back(m_FrameIndex);
	}

	void MaterialScene::BeginSync(uint32_t frameIndex)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_FrameIndex = frameIndex;
		m_DirtyMaterialCount = 0;
		m_DirtyMaterialIDs.clear();
		m_DirtyRanges.clear();
		EnsureFallbackMaterial();
		m_LastTouchedFrames[InvalidRenderMaterialID] = m_FrameIndex;
	}

	RenderMaterialID MaterialScene::UpsertMaterial(AssetHandle materialHandle, bool forceDirty)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!materialHandle)
			return InvalidRenderMaterialID;

		GPUMaterialBuildInput input;
		input.MaterialHandle = materialHandle;
		if (Project::GetAssetManager())
			input.MaterialAsset = AssetManager::GetAsset<MaterialAsset>(materialHandle);

		GPUMaterialData data = BuildGPUMaterialData(input, [this](AssetHandle textureHandle) { return ResolveTextureIndex(textureHandle); });
		return UpsertMaterial({ (uint64_t)materialHandle, MaterialKeyType::Asset }, data, forceDirty);
	}

	RenderMaterialID MaterialScene::UpsertOverrideMaterial(uint64_t overrideKey, const Ref<Material>& material, bool transparent, bool forceDirty)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!overrideKey || !material)
			return InvalidRenderMaterialID;

		GPUMaterialBuildInput input;
		input.OverrideMaterial = material;
		input.Transparent = transparent;
		GPUMaterialData data = BuildGPUMaterialData(input, [this](AssetHandle textureHandle) { return ResolveTextureIndex(textureHandle); });
		return UpsertMaterial({ overrideKey, MaterialKeyType::Override }, data, forceDirty);
	}

	RenderMaterialID MaterialScene::UpsertMaterial(MaterialKey key, GPUMaterialData data, bool forceDirty)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!key.SourceID)
			return InvalidRenderMaterialID;

		auto idIt = m_MaterialIDByKey.find(key);
		RenderMaterialID materialID = InvalidRenderMaterialID;
		bool created = false;
		if (idIt == m_MaterialIDByKey.end())
		{
			created = true;
			if (!m_FreeMaterialIDs.empty())
			{
				materialID = m_FreeMaterialIDs.back();
				m_FreeMaterialIDs.pop_back();
			}
			else
			{
				materialID = (RenderMaterialID)m_Materials.size();
				m_Materials.emplace_back();
				m_MaterialKeys.emplace_back();
				m_LastTouchedFrames.emplace_back(0);
			}

			m_MaterialIDByKey[key] = materialID;
			m_MaterialKeys[materialID] = key;
		}
		else
		{
			materialID = idIt->second;
		}

		data.Metadata.z = materialID;
		m_LastTouchedFrames[materialID] = m_FrameIndex;

		if (created || forceDirty || !MaterialDataEquals(m_Materials[materialID], data))
		{
			m_Materials[materialID] = data;
			MarkMaterialDirty(materialID);
		}

		return materialID;
	}

	GPUTextureIndex MaterialScene::ResolveTextureIndex(AssetHandle textureHandle)
	{
		if (!textureHandle)
			return InvalidGPUTextureIndex;

		if (m_TextureResolver)
			return m_TextureResolver(textureHandle);

		auto [it, inserted] = m_TextureIndexByHandle.try_emplace(textureHandle, InvalidGPUTextureIndex);
		if (inserted || it->second == InvalidGPUTextureIndex)
			it->second = m_NextTextureIndex++;

		return it->second;
	}

	void MaterialScene::EndSync()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		for (RenderMaterialID materialID = 1; materialID < m_Materials.size(); materialID++)
		{
			if (m_LastTouchedFrames[materialID] == m_FrameIndex)
				continue;

			const MaterialKey key = m_MaterialKeys[materialID];
			if (!key.SourceID)
				continue;

			m_MaterialIDByKey.erase(key);
			m_MaterialKeys[materialID] = {};
			m_LastTouchedFrames[materialID] = 0;
			m_Materials[materialID] = GetFallbackMaterialData();
			m_Materials[materialID].Metadata.z = materialID;
			m_FreeMaterialIDs.push_back(materialID);
			MarkMaterialDirty(materialID);
		}

		if (m_DirtyMaterialIDs.empty())
			return;

		std::sort(m_DirtyMaterialIDs.begin(), m_DirtyMaterialIDs.end());
		m_DirtyMaterialIDs.erase(std::unique(m_DirtyMaterialIDs.begin(), m_DirtyMaterialIDs.end()), m_DirtyMaterialIDs.end());
		m_DirtyMaterialCount = (uint32_t)m_DirtyMaterialIDs.size();

		MaterialSceneDirtyRange range;
		range.FirstMaterial = m_DirtyMaterialIDs.front();
		range.MaterialCount = 1;
		uint32_t previousMaterialID = range.FirstMaterial;

		for (size_t index = 1; index < m_DirtyMaterialIDs.size(); index++)
		{
			const uint32_t materialID = m_DirtyMaterialIDs[index];
			if (materialID == previousMaterialID + 1)
			{
				range.MaterialCount++;
			}
			else
			{
				m_DirtyRanges.push_back(range);
				range.FirstMaterial = materialID;
				range.MaterialCount = 1;
			}

			previousMaterialID = materialID;
		}

		m_DirtyRanges.push_back(range);
	}

	void MaterialScene::Clear()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_FrameIndex = 0;
		m_DirtyMaterialCount = 0;
		m_NextTextureIndex = 1;
		m_Materials.clear();
		m_MaterialKeys.clear();
		m_LastTouchedFrames.clear();
		m_FreeMaterialIDs.clear();
		m_MaterialIDByKey.clear();
		m_TextureIndexByHandle.clear();
		m_TextureResolver = {};
		m_DirtyMaterialIDs.clear();
		m_DirtyRanges.clear();
		EnsureFallbackMaterial();
	}

	void MaterialScene::MarkMaterialDirty(RenderMaterialID materialID)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (materialID >= m_Materials.size())
			return;

		m_DirtyMaterialIDs.push_back(materialID);
	}

}
