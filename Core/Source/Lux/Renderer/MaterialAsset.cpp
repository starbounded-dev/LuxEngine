// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "MaterialAsset.h"

#include "Lux/Renderer/Renderer.h"

#include "Lux/Asset/AssetManager.h"

#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <unordered_map>
#include <utility>

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

	// Keyed by texture asset. Material assets fill it and SceneRenderer reads it for the bindless
	// colour slots, from whichever threads load assets and build frames.
	static std::unordered_map<AssetHandle, SRGBTextureCacheEntry> s_SRGBTextureCache;
	// Keyed by texture asset and cutoff step: what a Cutout material's albedo binds instead of its sRGB
	// copy. Ordered, so one texture's variants are adjacent. Shares the sRGB cache's mutex.
	static std::map<std::pair<AssetHandle, uint8_t>, SRGBTextureCacheEntry> s_AlphaCoverageTextureCache;
	static std::mutex s_SRGBTextureCacheMutex;

	enum MaterialDebugFlags : uint32_t
	{
		MaterialDebug_NormalMap = BIT(0),
		MaterialDebug_Transparent = BIT(1),
		MaterialDebug_TwoSided = BIT(2),
		MaterialDebug_AlbedoMap = BIT(3),
		MaterialDebug_MetalnessMap = BIT(4),
		MaterialDebug_RoughnessMap = BIT(5)
	};

	Ref<Texture2D> MaterialAsset::GetSRGBTexture(AssetHandle textureHandle, const Ref<Texture2D>& texture)
	{
		if (!texture)
			return nullptr;

		// Only 8-bit RGBA holds sRGB-encoded bytes that can be reinterpreted. sRGB textures need no copy,
		// float textures are already linear, and block-compressed ones keep their authored format.
		if (texture->GetFormat() != ImageFormat::RGBA)
			return texture;

#ifndef LUX_HEADLESS
		const uint64_t sourceHash = texture->GetHash();
		std::scoped_lock lock(s_SRGBTextureCacheMutex);
		auto& cacheEntry = s_SRGBTextureCache[textureHandle];
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

	Ref<Texture2D> MaterialAsset::GetAlphaCoverageTexture(AssetHandle textureHandle, const Ref<Texture2D>& texture, uint8_t alphaCoverageCutoff)
	{
		if (!texture)
			return nullptr;

		// The chain is rescaled from 8-bit RGBA bytes, the bytes the sRGB copy reinterprets.
		if (!alphaCoverageCutoff || texture->GetFormat() != ImageFormat::RGBA)
			return GetSRGBTexture(textureHandle, texture);

#ifndef LUX_HEADLESS
		const uint64_t sourceHash = texture->GetHash();
		std::scoped_lock lock(s_SRGBTextureCacheMutex);
		auto& cacheEntry = s_AlphaCoverageTextureCache[{ textureHandle, alphaCoverageCutoff }];
		if (!cacheEntry.Texture || cacheEntry.SourceHash != sourceHash || cacheEntry.Texture->GetSize() != texture->GetSize() || cacheEntry.Texture->GetMipLevelCount() != texture->GetMipLevelCount())
		{
			cacheEntry.SourceHash = sourceHash;
			cacheEntry.Texture = Texture2D::CreateFromSRGB(texture, static_cast<float>(alphaCoverageCutoff) / 255.0f);
		}

		// Editing a cutoff builds a variant for each step it passes through: drop this texture's other
		// variants once nothing but the cache holds them. Erasing other nodes keeps cacheEntry valid.
		for (auto it = s_AlphaCoverageTextureCache.lower_bound({ textureHandle, 0 });
			it != s_AlphaCoverageTextureCache.end() && it->first.first == textureHandle;)
		{
			const bool unused = !it->second.Texture || it->second.Texture->GetRefCount() == 1;
			if (it->first.second != alphaCoverageCutoff && unused)
				it = s_AlphaCoverageTextureCache.erase(it);
			else
				++it;
		}

		return cacheEntry.Texture ? cacheEntry.Texture : texture;
#else
		return texture;
#endif
	}

	void MaterialAsset::ReleaseSharedResources()
	{
		std::scoped_lock lock(s_SRGBTextureCacheMutex);
		s_SRGBTextureCache.clear();
		s_AlphaCoverageTextureCache.clear();
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
		UpdateMaterialComplexityMetadata();
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

	void MaterialAsset::WriteTexture(const std::string& name, const Ref<Texture2D>& texture)
	{
		// Only where the shader declares the slot. Bindless shaders (HazelPBR_Transparent) have none:
		// MaterialScene resolves their maps from the handles in m_Maps.
		if (m_Material && m_Material->FindResourceDeclaration(name))
			m_Material->Set(name, texture);
	}

	void MaterialAsset::SetAlbedoColor(const glm::vec3& color)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Values.AlbedoColor = color;
		WriteUniform(s_AlbedoColorUniform, color);
		m_Revision++;
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
			{
				WriteAlbedoTexture(handle, texture);
			}
			else
			{
				std::scoped_lock lock(s_SRGBTextureCacheMutex);
				s_SRGBTextureCache.erase(handle);
				for (auto it = s_AlphaCoverageTextureCache.lower_bound({ handle, 0 });
					it != s_AlphaCoverageTextureCache.end() && it->first.first == handle;)
				{
					it = s_AlphaCoverageTextureCache.erase(it);
				}
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
		WriteTexture(s_AlbedoMapUniform, Renderer::GetWhiteTexture());
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
				WriteTexture(s_NormalMapUniform, texture);
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
		WriteTexture(s_NormalMapUniform, Renderer::GetWhiteTexture());
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
				WriteTexture(s_MetalnessMapUniform, texture);
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
		WriteTexture(s_MetalnessMapUniform, Renderer::GetWhiteTexture());
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
				WriteTexture(s_RoughnessMapUniform, texture);
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
		WriteTexture(s_RoughnessMapUniform, Renderer::GetWhiteTexture());
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
		const bool albedoChainChanged = parameters.AlphaMode != m_Surface.AlphaMode || parameters.AlphaThreshold != m_Surface.AlphaThreshold;
		m_Surface = parameters;

		// The albedo's mip chain follows the alpha mode and cutoff.
		if (albedoChainChanged && m_Maps.AlbedoMap && m_Material)
		{
			if (Ref<Texture2D> texture = AssetManager::GetAsset<Texture2D>(m_Maps.AlbedoMap))
				WriteAlbedoTexture(m_Maps.AlbedoMap, texture);
		}

		UpdateMaterialComplexityMetadata();
	}

	void MaterialAsset::WriteAlbedoTexture(AssetHandle handle, const Ref<Texture2D>& texture)
	{
		// Skip building a copy for a shader that has no albedo slot to put it in.
		if (!m_Material->FindResourceDeclaration(s_AlbedoMapUniform))
			return;

		// The copy SceneRenderer binds for this material's bindless albedo slot, built here ahead of the
		// first frame that draws it.
		const bool cutout = !m_Transparent && m_Surface.AlphaMode == MaterialAlphaMode::Cutout;
		const uint8_t alphaCoverageCutoff = cutout ? GetAlphaCoverageCutoffKey(m_Surface.AlphaThreshold) : uint8_t(0);
		WriteTexture(s_AlbedoMapUniform, alphaCoverageCutoff
			? GetAlphaCoverageTexture(handle, texture, alphaCoverageCutoff)
			: GetSRGBTexture(handle, texture));

#ifndef LUX_HEADLESS
		// The copy this material stopped binding (the importer and the editor switch alpha modes after
		// the albedo is set) goes once only the cache holds it. The bound one has the material's reference.
		std::scoped_lock lock(s_SRGBTextureCacheMutex);
		auto isUnused = [](const SRGBTextureCacheEntry& entry) { return !entry.Texture || entry.Texture->GetRefCount() == 1; };
		if (alphaCoverageCutoff)
		{
			auto srgbIt = s_SRGBTextureCache.find(handle);
			if (srgbIt != s_SRGBTextureCache.end() && isUnused(srgbIt->second))
				s_SRGBTextureCache.erase(srgbIt);
		}
		else
		{
			for (auto it = s_AlphaCoverageTextureCache.lower_bound({ handle, 0 });
				it != s_AlphaCoverageTextureCache.end() && it->first.first == handle;)
			{
				it = isUnused(it->second) ? s_AlphaCoverageTextureCache.erase(it) : std::next(it);
			}
		}
#endif
	}

	void MaterialAsset::UpdateMaterialComplexityMetadata()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		// Every setter except SetAlbedoColor, SetShadowCasting and SetMaterial (which count their own)
		// funnels through here, so this is where a change is counted.
		m_Revision++;
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
		textureCount += (m_Surface.EmissiveMap ? 1 : 0) + (m_Surface.OcclusionMap ? 1 : 0) + (m_Surface.HeightMap ? 1 : 0)
			+ (m_Surface.DiffuseTransmissionMap ? 1 : 0);
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
		// A new table may reuse a freed one's address, which the render scene compares by pointer.
		MarkChanged();
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
		MarkChanged();
	}

	void MaterialTable::ClearMaterial(uint32_t index)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_ASSERT(HasMaterial(index));
		m_Materials.erase(index);
		if (index >= m_MaterialCount)
			m_MaterialCount = index + 1;
		MarkChanged();
	}

	void MaterialTable::Clear()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		m_Materials.clear();
		MarkChanged();
	}

}
