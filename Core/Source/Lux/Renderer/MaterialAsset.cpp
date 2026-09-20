// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "MaterialAsset.h"

#include "Lux/Renderer/Renderer.h"

#include "Lux/Asset/AssetManager.h"

#include <algorithm>
#include <array>
#include <unordered_map>

namespace Lux {

	static const std::string s_AlbedoColorUniform = "u_MaterialUniforms.AlbedoColor";
	static const std::string s_UseNormalMapUniform = "u_MaterialUniforms.UseNormalMap";
	static const std::string s_MetalnessUniform = "u_MaterialUniforms.Metalness";
	static const std::string s_RoughnessUniform = "u_MaterialUniforms.Roughness";
	static const std::string s_EmissionUniform = "u_MaterialUniforms.Emission";
	static const std::string s_TransparencyUniform = "u_MaterialUniforms.Transparency";
	static const std::string s_MaterialComplexityScoreUniform = "u_MaterialUniforms.MaterialComplexityScore";
	static const std::string s_MaterialDebugFlagsUniform = "u_MaterialUniforms.MaterialDebugFlags";

	static const std::string s_AlbedoMapUniform = "u_AlbedoTexture";
	static const std::string s_NormalMapUniform = "u_NormalTexture";
	static const std::string s_MetalnessMapUniform = "u_MetalnessTexture";
	static const std::string s_RoughnessMapUniform = "u_RoughnessTexture";

	struct SRGBTextureCacheEntry
	{
		uint64_t SourceHash = 0;
		Ref<Texture2D> Texture;
	};

	static std::unordered_map<AssetHandle, SRGBTextureCacheEntry> s_SRGBAlbedoTextureCache;

	enum MaterialDebugFlags : uint32_t
	{
		MaterialDebug_NormalMap = BIT(0),
		MaterialDebug_Transparent = BIT(1),
		MaterialDebug_TwoSided = BIT(2),
		MaterialDebug_AlbedoMap = BIT(3),
		MaterialDebug_MetalnessMap = BIT(4),
		MaterialDebug_RoughnessMap = BIT(5)
	};

	static Ref<Texture2D> GetAlbedoTextureForMaterial(AssetHandle handle, const Ref<Texture2D>& texture)
	{
		if (!texture)
			return nullptr;

		const ImageFormat format = texture->GetFormat();
		if (format == ImageFormat::SRGBA || format == ImageFormat::SRGB)
			return texture;

#ifndef LUX_HEADLESS
		const uint64_t sourceHash = texture->GetHash();
		auto& cacheEntry = s_SRGBAlbedoTextureCache[handle];
		if (!cacheEntry.Texture || cacheEntry.SourceHash != sourceHash || cacheEntry.Texture->GetSize() != texture->GetSize() || cacheEntry.Texture->GetMipLevelCount() != texture->GetMipLevelCount())
		{
			cacheEntry.SourceHash = sourceHash;
			cacheEntry.Texture = Texture2D::CreateFromSRGB(texture);
		}

		return cacheEntry.Texture ? cacheEntry.Texture : texture;
#else
		return texture;
#endif
	}

	MaterialAsset::MaterialAsset(bool transparent)
		: m_Transparent(transparent)
	{
		Handle = {};

#ifndef LUX_HEADLESS
		if (transparent)
			m_Material = Material::Create(Renderer::GetShaderLibrary()->Get("HazelPBR_Transparent"));
		else
			m_Material = Material::Create(Renderer::GetShaderLibrary()->Get("HazelPBR_Static"));
#endif

		SetDefaults();
	}

	MaterialAsset::MaterialAsset(Ref<Material> material)
	{
		Handle = {};
		m_Material = Material::Copy(material);

		// Seed the asset's values from whatever the source material's shader block holds.
		auto read = [this](const std::string& name, auto& target)
			{
				using T = std::remove_reference_t<decltype(target)>;
				if (m_Material && m_Material->FindUniformDeclaration(name))
					target = m_Material->Get<T>(name);
			};
		read(s_AlbedoColorUniform, m_Values.AlbedoColor);
		read(s_MetalnessUniform, m_Values.Metalness);
		read(s_RoughnessUniform, m_Values.Roughness);
		read(s_EmissionUniform, m_Values.Emission);
		read(s_TransparencyUniform, m_Values.Transparency);
		read(s_UseNormalMapUniform, m_Values.UseNormalMap);
	}

	MaterialAsset::~MaterialAsset()
	{
	}

	void MaterialAsset::OnDependencyUpdated(AssetHandle handle)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (handle == m_Maps.AlbedoMap)
		{
			SetAlbedoMap(handle);
		}
		else if (handle == m_Maps.NormalMap)
		{
			SetNormalMap(handle);
		}
		else if (handle == m_Maps.MetalnessMap)
		{
			SetMetalnessMap(handle);

		}
		else if (handle == m_Maps.RoughnessMap)
		{
			SetRoughnessMap(handle);
		}
	}

	template<typename T>
	void MaterialAsset::WriteUniform(const std::string& name, const T& value)
	{
		// Mirror into the shader block only where it has the member (see m_Values).
		if (m_Material && m_Material->FindUniformDeclaration(name))
			m_Material->Set(name, value);
	}

	void MaterialAsset::SetAlbedoColor(const glm::vec3& color)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Values.AlbedoColor = color;
		WriteUniform(s_AlbedoColorUniform, color);
	}

	void MaterialAsset::SetMetalness(float value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Values.Metalness = value;
		WriteUniform(s_MetalnessUniform, value);
		UpdateMaterialComplexityMetadata();
	}

	void MaterialAsset::SetRoughness(float value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Values.Roughness = value;
		WriteUniform(s_RoughnessUniform, value);
		UpdateMaterialComplexityMetadata();
	}

	void MaterialAsset::SetEmission(float value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Values.Emission = value;
		WriteUniform(s_EmissionUniform, value);
		UpdateMaterialComplexityMetadata();
	}

	Ref<Texture2D> MaterialAsset::GetAlbedoMap()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// QUESTION: Is there a reason we need to go to the material here?
		//           Don't we already have the texture handle in m_Maps.AlbedoMap?
		auto texture = m_Material->TryGetTexture2D(s_AlbedoMapUniform);
		return texture;
	}

	void MaterialAsset::SetAlbedoMap(AssetHandle handle)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Maps.AlbedoMap = handle;
		if (!m_Material)
			return;

		if (handle)
		{
			Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(handle);
			if (texture)
				m_Material->Set(s_AlbedoMapUniform, GetAlbedoTextureForMaterial(handle, texture));
			else
			{
				s_SRGBAlbedoTextureCache.erase(handle);
				ClearAlbedoMap();
			}
		}
		else
		{
			ClearAlbedoMap();
		}

		UpdateMaterialComplexityMetadata();
	}

	void MaterialAsset::ClearAlbedoMap()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Maps.AlbedoMap = 0;
#ifndef LUX_HEADLESS
		m_Material->Set(s_AlbedoMapUniform, Renderer::GetWhiteTexture());
#endif
		UpdateMaterialComplexityMetadata();
	}

	Ref<Texture2D> MaterialAsset::GetNormalMap()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_Material->TryGetTexture2D(s_NormalMapUniform);
	}

	void MaterialAsset::SetNormalMap(AssetHandle handle)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Maps.NormalMap = handle;

		if (handle)
		{
			Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(handle);
			if (texture)
				m_Material->Set(s_NormalMapUniform, texture);
			else
				ClearNormalMap();
		}
		else
		{
			ClearNormalMap();
		}

		UpdateMaterialComplexityMetadata();
	}

	void MaterialAsset::SetUseNormalMap(bool value)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Values.UseNormalMap = value;
		WriteUniform(s_UseNormalMapUniform, value);
		UpdateMaterialComplexityMetadata();
	}

	void MaterialAsset::ClearNormalMap()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Maps.NormalMap = 0;
#ifndef LUX_HEADLESS
		m_Material->Set(s_NormalMapUniform, Renderer::GetWhiteTexture());
#endif
		UpdateMaterialComplexityMetadata();
	}

	Ref<Texture2D> MaterialAsset::GetMetalnessMap()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_Material->TryGetTexture2D(s_MetalnessMapUniform);
	}

	void MaterialAsset::SetMetalnessMap(AssetHandle handle)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Maps.MetalnessMap = handle;

		if (handle)
		{
			Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(handle);
			if (texture)
				m_Material->Set(s_MetalnessMapUniform, texture);
			else
				ClearMetalnessMap();
		}
		else
		{
			ClearMetalnessMap();
		}

		UpdateMaterialComplexityMetadata();
	}

	void MaterialAsset::ClearMetalnessMap()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Maps.MetalnessMap = 0;
#ifndef LUX_HEADLESS
		m_Material->Set(s_MetalnessMapUniform, Renderer::GetWhiteTexture());
#endif
		UpdateMaterialComplexityMetadata();
	}

	Ref<Texture2D> MaterialAsset::GetRoughnessMap()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		return m_Material->TryGetTexture2D(s_RoughnessMapUniform);
	}

	void MaterialAsset::SetRoughnessMap(AssetHandle handle)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Maps.RoughnessMap = handle;

		if (handle)
		{
			Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(handle);
			if (texture)
				m_Material->Set(s_RoughnessMapUniform, texture);
			else
				ClearRoughnessMap();
		}
		else
		{
			ClearRoughnessMap();
		}

		UpdateMaterialComplexityMetadata();
	}

	void MaterialAsset::ClearRoughnessMap()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Maps.RoughnessMap = 0;
#ifndef LUX_HEADLESS
		m_Material->Set(s_RoughnessMapUniform, Renderer::GetWhiteTexture());
#endif
		UpdateMaterialComplexityMetadata();
	}

	void MaterialAsset::SetTransparency(float transparency)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Values.Transparency = transparency;
		WriteUniform(s_TransparencyUniform, transparency);
		UpdateMaterialComplexityMetadata();
	}

	void MaterialAsset::SetSurfaceParameters(const MaterialSurfaceParameters& parameters)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Surface = parameters;
		UpdateMaterialComplexityMetadata();
	}

	void MaterialAsset::UpdateMaterialComplexityMetadata()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_Material || !m_Material->FindUniformDeclaration(s_MaterialComplexityScoreUniform))
			return;

		const bool usingNormalMap = m_Values.UseNormalMap && m_Maps.NormalMap;
		const bool twoSided = m_Material->GetFlag(MaterialFlag::TwoSided);

		uint32_t flags = 0;
		uint32_t textureCount = 0;
		if (m_Maps.AlbedoMap)
		{
			flags |= MaterialDebug_AlbedoMap;
			textureCount++;
		}
		textureCount += (m_Surface.EmissiveMap ? 1 : 0) + (m_Surface.OcclusionMap ? 1 : 0) + (m_Surface.HeightMap ? 1 : 0);
		if (usingNormalMap)
		{
			flags |= MaterialDebug_NormalMap;
			textureCount++;
		}
		if (!m_Transparent && m_Maps.MetalnessMap)
		{
			flags |= MaterialDebug_MetalnessMap;
			textureCount++;
		}
		if (!m_Transparent && m_Maps.RoughnessMap)
		{
			flags |= MaterialDebug_RoughnessMap;
			textureCount++;
		}
		if (m_Transparent)
			flags |= MaterialDebug_Transparent;
		if (twoSided)
			flags |= MaterialDebug_TwoSided;

		float score = m_Transparent ? 5.0f : 3.0f;
		score += static_cast<float>(textureCount) * 0.75f;
		score += usingNormalMap ? 1.5f : 0.0f;
		score += twoSided ? 1.25f : 0.0f;
		score += m_Values.Emission > 0.0f ? 1.0f : 0.0f;
		if (!m_Transparent)
		{
			score += std::clamp(m_Values.Metalness, 0.0f, 1.0f) * 0.5f;
			score += (1.0f - std::clamp(m_Values.Roughness, 0.0f, 1.0f)) * 0.5f;
		}
		else
		{
			score += (1.0f - std::clamp(m_Values.Transparency, 0.0f, 1.0f)) * 2.0f;
		}

		m_Material->Set(s_MaterialComplexityScoreUniform, score);
		if (m_Material->FindUniformDeclaration(s_MaterialDebugFlagsUniform))
			m_Material->Set(s_MaterialDebugFlagsUniform, flags);
	}

	void MaterialAsset::SetDefaults()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!m_Material)
			return;

		if (m_Transparent)
		{
			// Set defaults
			SetAlbedoColor(glm::vec3(1.0f));

			// Maps
			ClearAlbedoMap();
		}
		else
		{
			// Set defaults
			SetAlbedoColor(glm::vec3(1.0f));
			SetEmission(0.0f);
			SetUseNormalMap(false);
			SetMetalness(0.0f);
			SetRoughness(0.4f);

			// Maps
			ClearAlbedoMap();
			ClearNormalMap();
			ClearMetalnessMap();
			ClearRoughnessMap();
		}

		UpdateMaterialComplexityMetadata();
	}

	MaterialTable::MaterialTable(uint32_t materialCount)
		: m_MaterialCount(materialCount)
	{
	}

	MaterialTable::MaterialTable(Ref<MaterialTable> other)
		: m_MaterialCount(other->m_MaterialCount)
	{
		const auto& meshMaterials = other->GetMaterials();
		for (auto [index, materialAsset] : meshMaterials)
			SetMaterial(index, materialAsset);
	}

	void MaterialTable::SetMaterial(uint32_t index, AssetHandle material)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Materials[index] = material;
		if (index >= m_MaterialCount)
			m_MaterialCount = index + 1;
	}

	void MaterialTable::ClearMaterial(uint32_t index)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_ASSERT(HasMaterial(index));
		m_Materials.erase(index);
		if (index >= m_MaterialCount)
			m_MaterialCount = index + 1;
	}

	void MaterialTable::Clear()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Materials.clear();
	}

}
