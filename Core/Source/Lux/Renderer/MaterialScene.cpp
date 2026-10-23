// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

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
		static const std::string s_EnvMapRotationUniform = "u_MaterialUniforms.EnvMapRotation";
		static const std::string s_MaterialComplexityScoreUniform = "u_MaterialUniforms.MaterialComplexityScore";

		bool MaterialDataEquals(const GPUMaterialData& lhs, const GPUMaterialData& rhs)
		{
			return std::memcmp(&lhs, &rhs, sizeof(GPUMaterialData)) == 0;
		}

		bool IsRefractive(const GPUMaterialData& data)
		{
			return data.Transmission.x > 0.0f;
		}

		uint32_t PackFloatBits(float value)
		{
			uint32_t bits = 0;
			std::memcpy(&bits, &value, sizeof(float));
			return bits;
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

		// A transparent asset is Blend whatever it authored — that is what routes it to the sorted
		// forward pass, and a Cutout there would be discarded twice. Everything else takes the
		// authored mode, so an older file (no AlphaMode key, hence Opaque) is unchanged.
		GPUMaterialAlphaMode ResolveAlphaMode(const Ref<MaterialAsset>& materialAsset, bool fromAsset, bool transparent)
		{
			if (transparent)
				return GPUMaterialAlphaMode::Blend;

			if (!fromAsset || !materialAsset)
				return GPUMaterialAlphaMode::Opaque;

			switch (materialAsset->GetSurfaceParameters().AlphaMode)
			{
				case MaterialAlphaMode::Cutout: return GPUMaterialAlphaMode::Masked;
				// Authored Blend without a transparent asset cannot blend: the material is not in
				// the sorted forward pass, so the opaque pass draws it solid. Report what actually
				// happens rather than a mode the frame never uses.
				case MaterialAlphaMode::Blend:  return GPUMaterialAlphaMode::Opaque;
				case MaterialAlphaMode::Opaque: break;
			}
			return GPUMaterialAlphaMode::Opaque;
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
		const GPUTextureResolver& resolveTextureIndex)
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
		const bool twoSided = ResolveRasterState(materialAsset, input.OverrideMaterial, input.Transparent).TwoSided;

		// A material asset owns its values; only a bare override material is read from its shader
		// block, which may not declare every member.
		const bool fromAsset = materialAsset && !input.OverrideMaterial;
		const glm::vec3 albedoColor = fromAsset ? materialAsset->GetAlbedoColor() : ReadMaterialVec3(material, s_AlbedoColorUniform, glm::vec3(1.0f));
		const float metalness = transparent ? 0.0f : glm::clamp(fromAsset ? materialAsset->GetMetalness() : ReadMaterialFloat(material, s_MetalnessUniform, 0.0f), 0.0f, 1.0f);
		const float roughness = glm::clamp(fromAsset ? materialAsset->GetRoughness() : ReadMaterialFloat(material, s_RoughnessUniform, transparent ? 0.5f : 0.4f), 0.0f, 1.0f);
		const float emission = glm::max(fromAsset ? materialAsset->GetEmission() : ReadMaterialFloat(material, s_EmissionUniform, 0.0f), 0.0f);
		const float opacity = transparent ? glm::clamp(fromAsset ? materialAsset->GetTransparency() : ReadMaterialFloat(material, s_TransparencyUniform, 1.0f), 0.0f, 1.0f) : 1.0f;
		const float envMapRotation = ReadMaterialFloat(material, s_EnvMapRotationUniform, 0.0f);
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

		auto assignTexture = [&](AssetHandle textureHandle, GPUMaterialFlags presentFlag, uint32_t& textureSlot,
			TextureColorSpace colorSpace = TextureColorSpace::Linear)
			{
				if (!textureHandle)
					return;

				if (!IsTextureHandleValid(textureHandle))
				{
					flags |= GPUMaterialFlags::MissingTexture;
					return;
				}

				flags |= presentFlag;
				textureSlot = resolveTextureIndex ? resolveTextureIndex(textureHandle, colorSpace) : InvalidGPUTextureIndex;
			};

		// Without an asset (a bare override material) emission keeps tinting by the base colour.
		data.Emissive = glm::vec4(ToLinearColor(albedoColor) * emission, 1.0f);

		if (materialAsset)
		{
			assignTexture(materialAsset->GetAlbedoMapHandle(), GPUMaterialFlags::HasAlbedoTexture, data.TextureIndices.x, TextureColorSpace::SRGB);

			const bool useNormalMap = (fromAsset ? materialAsset->IsUsingNormalMap() : ReadMaterialBool(material, s_UseNormalMapUniform, false))
				&& materialAsset->GetNormalMapHandle();
			if (useNormalMap)
				flags |= GPUMaterialFlags::UseNormalMap;
			assignTexture(useNormalMap ? materialAsset->GetNormalMapHandle() : AssetHandle(0), GPUMaterialFlags::HasNormalTexture, data.TextureIndices.y);
			assignTexture(!transparent ? materialAsset->GetMetalnessMapHandle() : AssetHandle(0), GPUMaterialFlags::HasMetalnessTexture, data.TextureIndices.z);
			assignTexture(materialAsset->GetRoughnessMapHandle(), GPUMaterialFlags::HasRoughnessTexture, data.TextureIndices.w);

			const MaterialSurfaceParameters& surface = materialAsset->GetSurfaceParameters();
			if (fromAsset)
				data.Emissive = glm::vec4(ToLinearColor(surface.EmissiveColor) * emission, glm::clamp(surface.OcclusionStrength, 0.0f, 1.0f));

			data.Surface = glm::vec4(
				glm::clamp(surface.Specular, 0.0f, 1.0f),
				glm::max(surface.NormalStrength, 0.0f),
				glm::clamp(surface.AlphaThreshold, 0.0f, 1.0f),
				glm::max(surface.BumpHeight, 0.0f));

			// uv' = R(rotation) * (uv * tiling) + offset
			const float rotation = glm::radians(surface.UVRotation);
			const float cosRotation = glm::cos(rotation);
			const float sinRotation = glm::sin(rotation);
			data.UVTransform = glm::vec4(
				cosRotation * surface.UVTiling.x, sinRotation * surface.UVTiling.x,
				-sinRotation * surface.UVTiling.y, cosRotation * surface.UVTiling.y);
			data.UVOffset = glm::vec4(surface.UVOffset, 0.0f, 0.0f);

			assignTexture(surface.EmissiveMap, GPUMaterialFlags::HasEmissiveTexture, data.ExtraTextureIndices.x, TextureColorSpace::SRGB);
			assignTexture(surface.OcclusionMap, GPUMaterialFlags::HasOcclusionTexture, data.ExtraTextureIndices.y);
			assignTexture(surface.HeightMap, GPUMaterialFlags::HasHeightTexture, data.ExtraTextureIndices.z);
			data.ExtraTextureIndices.w =
				((uint32_t)surface.MetalnessChannel << GPUMaterialChannelShiftMetalness)
				| ((uint32_t)surface.RoughnessChannel << GPUMaterialChannelShiftRoughness)
				| ((uint32_t)surface.OcclusionChannel << GPUMaterialChannelShiftOcclusion);

			data.Transmission = glm::vec4(
				transparent ? glm::clamp(surface.Transmission, 0.0f, 1.0f) : 0.0f,
				glm::max(surface.IOR, 1.0f),
				glm::max(surface.Thickness, 0.0f),
				glm::max(surface.AttenuationDistance, 0.0f));
			data.AttenuationColor = glm::vec4(ToLinearColor(glm::clamp(surface.AttenuationColor, glm::vec3(0.0f), glm::vec3(1.0f))), 0.0f);
		}

		data.Metadata = glm::uvec4(
			(uint32_t)flags,
			(uint32_t)ResolveAlphaMode(materialAsset, fromAsset, transparent),
			InvalidRenderMaterialID,
			PackFloatBits(envMapRotation));
		return data;
	}

	GPUMaterialRasterState MaterialScene::ResolveRasterState(const Ref<MaterialAsset>& materialAsset, const Ref<Material>& overrideMaterial, bool transparent)
	{
		const Ref<Material> material = overrideMaterial ? overrideMaterial : (materialAsset ? materialAsset->GetMaterial() : nullptr);
		if (!material)
			return {};

		GPUMaterialRasterState state;
		// The authored asset property is the source of truth; MaterialFlag::TwoSided remains
		// honoured so a bare override Material (which has no asset) can still opt in.
		state.TwoSided = material->GetFlag(MaterialFlag::TwoSided)
			|| (materialAsset && materialAsset->GetSurfaceParameters().TwoSided);

		const bool fromAsset = materialAsset && !overrideMaterial;
		transparent = transparent || (materialAsset && materialAsset->IsTransparent());
		state.AlphaMasked = ResolveAlphaMode(materialAsset, fromAsset, transparent) == GPUMaterialAlphaMode::Masked;
		return state;
	}

	void MaterialScene::EnsureFallbackMaterial()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_Materials.empty())
			return;

		m_Materials.push_back(GetFallbackMaterialData());
		m_Rows.emplace_back();
	}

	void MaterialScene::BeginSync(uint32_t frameIndex)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_FrameIndex = frameIndex;
		m_DirtyMaterialCount = 0;
		m_DirtyMaterialIDs.clear();
		m_DirtyRanges.clear();
		EnsureFallbackMaterial();
	}

	RenderMaterialID MaterialScene::AllocateMaterialRow(MaterialKey key)
	{
		RenderMaterialID materialID = InvalidRenderMaterialID;
		if (!m_FreeMaterialIDs.empty())
		{
			materialID = m_FreeMaterialIDs.back();
			m_FreeMaterialIDs.pop_back();
		}
		else
		{
			materialID = (RenderMaterialID)m_Materials.size();
			m_Materials.emplace_back();
			m_Rows.emplace_back();
		}

		m_MaterialIDByKey[key] = materialID;
		m_Rows[materialID] = {};
		m_Rows[materialID].Key = key;
		m_Rows[materialID].LastTouchedFrame = m_FrameIndex;
		// A new row is always uploaded, even when its data matches what the slot held before.
		MarkMaterialDirty(materialID);
		return materialID;
	}

	RenderMaterialID MaterialScene::AcquireMaterial(AssetHandle materialHandle)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!materialHandle)
			return InvalidRenderMaterialID;

		const MaterialKey key{ (uint64_t)materialHandle, MaterialKeyType::Asset };
		if (auto idIt = m_MaterialIDByKey.find(key); idIt != m_MaterialIDByKey.end())
		{
			MaterialRow& row = m_Rows[idIt->second];
			row.RefCount++;
			row.LastTouchedFrame = m_FrameIndex;
			return idIt->second;
		}

		const RenderMaterialID materialID = AllocateMaterialRow(key);
		MaterialRow& row = m_Rows[materialID];
		row.RefCount = 1;
		if (Project::GetAssetManager())
			row.Asset = AssetManager::GetAsset<MaterialAsset>(materialHandle);

		BuildAssetMaterialRow(materialID);
		return materialID;
	}

	void MaterialScene::ReleaseMaterial(RenderMaterialID materialID)
	{
		if (materialID == InvalidRenderMaterialID || materialID >= m_Rows.size())
			return;

		MaterialRow& row = m_Rows[materialID];
		LUX_CORE_ASSERT(row.RefCount > 0, "MaterialScene: material released more often than acquired");
		if (row.RefCount > 0)
			row.RefCount--;
	}

	RenderMaterialID MaterialScene::UpsertOverrideMaterial(uint64_t overrideKey, const Ref<Material>& material, bool transparent, bool forceDirty)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!overrideKey || !material)
			return InvalidRenderMaterialID;

		const MaterialKey key{ overrideKey, MaterialKeyType::Override };
		auto idIt = m_MaterialIDByKey.find(key);
		const RenderMaterialID materialID = idIt != m_MaterialIDByKey.end() ? idIt->second : AllocateMaterialRow(key);
		m_Rows[materialID].LastTouchedFrame = m_FrameIndex;

		GPUMaterialBuildInput input;
		input.OverrideMaterial = material;
		input.Transparent = transparent;
		BuildMaterialRow(materialID, input, forceDirty);
		return materialID;
	}

	void MaterialScene::BuildAssetMaterialRow(RenderMaterialID materialID)
	{
		MaterialRow& row = m_Rows[materialID];
		row.BuiltRevision = row.Asset ? row.Asset->GetRevision() : 0;

		GPUMaterialBuildInput input;
		input.MaterialHandle = (AssetHandle)row.Key.SourceID;
		input.MaterialAsset = row.Asset;
		BuildMaterialRow(materialID, input, false);
	}

	void MaterialScene::BuildMaterialRow(RenderMaterialID materialID, const GPUMaterialBuildInput& input, bool forceDirty)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		MaterialRow& row = m_Rows[materialID];

		// Acquire the new texture set before releasing the old one, so a texture both use keeps its slot.
		std::vector<GPUTextureIndex> previousTextures = std::move(row.Textures);
		row.Textures.clear();
		GPUMaterialData data = BuildGPUMaterialData(input, [this, &row](AssetHandle textureHandle, TextureColorSpace colorSpace)
			{
				if (!m_TextureScene)
					return InvalidGPUTextureIndex;

				const GPUTextureIndex textureIndex = m_TextureScene->AcquireTexture(textureHandle, colorSpace);
				if (textureIndex != InvalidGPUTextureIndex)
					row.Textures.push_back(textureIndex);
				return textureIndex;
			});

		if (m_TextureScene)
		{
			for (GPUTextureIndex textureIndex : previousTextures)
				m_TextureScene->ReleaseTexture(textureIndex);
		}

		data.Metadata.z = materialID;
		if (forceDirty || !MaterialDataEquals(m_Materials[materialID], data))
		{
			m_RefractiveMaterialCount -= IsRefractive(m_Materials[materialID]) ? 1u : 0u;
			m_RefractiveMaterialCount += IsRefractive(data) ? 1u : 0u;
			m_Materials[materialID] = data;
			MarkMaterialDirty(materialID);
		}
	}

	void MaterialScene::FreeMaterialRow(RenderMaterialID materialID)
	{
		MaterialRow& row = m_Rows[materialID];
		if (m_TextureScene)
		{
			for (GPUTextureIndex textureIndex : row.Textures)
				m_TextureScene->ReleaseTexture(textureIndex);
		}

		m_MaterialIDByKey.erase(row.Key);
		row = {};
		m_RefractiveMaterialCount -= IsRefractive(m_Materials[materialID]) ? 1u : 0u;
		m_Materials[materialID] = GetFallbackMaterialData();
		m_Materials[materialID].Metadata.z = materialID;
		m_FreeMaterialIDs.push_back(materialID);
		MarkMaterialDirty(materialID);
	}

	void MaterialScene::EndSync()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const bool hasAssetManager = Project::GetAssetManager() != nullptr;
		for (RenderMaterialID materialID = 1; materialID < m_Rows.size(); materialID++)
		{
			MaterialRow& row = m_Rows[materialID];
			if (!row.Key.SourceID)
				continue;

			// Released this frame or earlier, or an override nobody upserted this frame.
			if (row.RefCount == 0 && row.LastTouchedFrame != m_FrameIndex)
			{
				FreeMaterialRow(materialID);
				continue;
			}

			if (row.Key.Type != MaterialKeyType::Asset)
				continue;

			// A reload replaces the asset object; an edit bumps its revision. Both rebuild the row.
			Ref<MaterialAsset> asset = hasAssetManager ? AssetManager::GetAsset<MaterialAsset>((AssetHandle)row.Key.SourceID) : nullptr;
			if (asset.Raw() != row.Asset.Raw() || (asset && asset->GetRevision() != row.BuiltRevision))
			{
				row.Asset = std::move(asset);
				BuildAssetMaterialRow(materialID);
				m_AssetRevision++;
			}
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
		// Return the rows' texture references, so the TextureScene can be kept or cleared after this.
		if (m_TextureScene)
		{
			for (const MaterialRow& row : m_Rows)
			{
				for (GPUTextureIndex textureIndex : row.Textures)
					m_TextureScene->ReleaseTexture(textureIndex);
			}
		}

		m_FrameIndex = 0;
		m_DirtyMaterialCount = 0;
		m_RefractiveMaterialCount = 0;
		m_Materials.clear();
		m_Rows.clear();
		m_FreeMaterialIDs.clear();
		m_AssetRevision++;
		m_MaterialIDByKey.clear();
		m_DirtyMaterialIDs.clear();
		m_DirtyRanges.clear();
		m_Version++;
		EnsureFallbackMaterial();
	}

	void MaterialScene::MarkMaterialDirty(RenderMaterialID materialID)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (materialID >= m_Materials.size())
			return;

		m_DirtyMaterialIDs.push_back(materialID);
		m_Version++;
	}

}
