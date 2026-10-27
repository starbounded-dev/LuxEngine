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
#include <unordered_set>
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
	// Cutout albedo textures already reported as keeping their box-filtered chain. Same mutex.
	static std::unordered_set<AssetHandle> s_ReportedAlphaCoverageFallbacks;
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

		if (!alphaCoverageCutoff)
			return GetSRGBTexture(textureHandle, texture);

		// The chain is rescaled from 8-bit RGBA bytes, the bytes the sRGB copy reinterprets.
		if (texture->GetFormat() != ImageFormat::RGBA)
		{
			{
				std::scoped_lock lock(s_SRGBTextureCacheMutex);
				if (s_ReportedAlphaCoverageFallbacks.insert(textureHandle).second)
				{
					LUX_CORE_WARN_TAG("Renderer", "Cutout albedo '{}' ({}) keeps its box-filtered mips and may thin out with distance: coverage-preserving mips are built from 8-bit RGBA only",
						texture->GetPath().string(), Utils::ImageFormatToString(texture->GetFormat()));
				}
			}
			return GetSRGBTexture(textureHandle, texture);
		}

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
		if (handle && handle == m_Parent)
		{
			if (Ref<MaterialAsset> parent = AssetManager::GetAsset<MaterialAsset>(m_Parent))
				InheritFrom(*parent);
			return;
		}

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

	// -- Material instances --------------------------------------------------------------------

	namespace {

		// One inheritable value: how to compare it between two materials and copy it across. The
		// keys are the .lmat names. Maps come before UseNormalMap: assigning a normal map sets it.
		struct InstanceField
		{
			std::string_view Key;
			bool (*Equal)(const MaterialAsset&, const MaterialAsset&);
			void (*Copy)(MaterialAsset&, const MaterialAsset&);
		};

		template<auto Member>
		constexpr InstanceField SurfaceField(std::string_view key)
		{
			return { key,
				[](const MaterialAsset& a, const MaterialAsset& b) { return a.GetSurfaceParameters().*Member == b.GetSurfaceParameters().*Member; },
				[](MaterialAsset& to, const MaterialAsset& from)
				{
					MaterialSurfaceParameters surface = to.GetSurfaceParameters();
					surface.*Member = from.GetSurfaceParameters().*Member;
					to.SetSurfaceParameters(surface);
				} };
		}

		#define LUX_MATERIAL_FIELD(key, get, set) InstanceField{ key, \
			[](const MaterialAsset& a, const MaterialAsset& b) { return a.get() == b.get(); }, \
			[](MaterialAsset& to, const MaterialAsset& from) { to.set(from.get()); } }
		#define LUX_MATERIAL_MAP(key, get, set, clear) InstanceField{ key, \
			[](const MaterialAsset& a, const MaterialAsset& b) { return a.get() == b.get(); }, \
			[](MaterialAsset& to, const MaterialAsset& from) { if (from.get()) to.set(from.get()); else to.clear(); } }

		const InstanceField s_InstanceFields[] = {
			LUX_MATERIAL_FIELD("AlbedoColor", GetAlbedoColor, SetAlbedoColor),
			LUX_MATERIAL_FIELD("Metalness", GetMetalness, SetMetalness),
			LUX_MATERIAL_FIELD("Roughness", GetRoughness, SetRoughness),
			LUX_MATERIAL_FIELD("Emission", GetEmission, SetEmission),
			LUX_MATERIAL_FIELD("Transparency", GetTransparency, SetTransparency),
			LUX_MATERIAL_MAP("AlbedoMap", GetAlbedoMapHandle, SetAlbedoMap, ClearAlbedoMap),
			LUX_MATERIAL_MAP("NormalMap", GetNormalMapHandle, SetNormalMap, ClearNormalMap),
			LUX_MATERIAL_MAP("MetalnessMap", GetMetalnessMapHandle, SetMetalnessMap, ClearMetalnessMap),
			LUX_MATERIAL_MAP("RoughnessMap", GetRoughnessMapHandle, SetRoughnessMap, ClearRoughnessMap),
			LUX_MATERIAL_FIELD("UseNormalMap", IsUsingNormalMap, SetUseNormalMap),
			LUX_MATERIAL_FIELD("CastShadows", IsShadowCasting, SetShadowCasting),
			LUX_MATERIAL_FIELD("AcousticMaterial", GetAcousticTag, SetAcousticTag),
			SurfaceField<&MaterialSurfaceParameters::EmissiveColor>("EmissiveColor"),
			SurfaceField<&MaterialSurfaceParameters::EmissiveMap>("EmissiveMap"),
			SurfaceField<&MaterialSurfaceParameters::OcclusionMap>("OcclusionMap"),
			SurfaceField<&MaterialSurfaceParameters::OcclusionStrength>("OcclusionStrength"),
			SurfaceField<&MaterialSurfaceParameters::OcclusionChannel>("OcclusionChannel"),
			SurfaceField<&MaterialSurfaceParameters::MetalnessChannel>("MetalnessChannel"),
			SurfaceField<&MaterialSurfaceParameters::RoughnessChannel>("RoughnessChannel"),
			SurfaceField<&MaterialSurfaceParameters::Specular>("Specular"),
			SurfaceField<&MaterialSurfaceParameters::NormalStrength>("NormalStrength"),
			SurfaceField<&MaterialSurfaceParameters::HeightMap>("HeightMap"),
			SurfaceField<&MaterialSurfaceParameters::BumpHeight>("BumpHeight"),
			SurfaceField<&MaterialSurfaceParameters::UVTiling>("UVTiling"),
			SurfaceField<&MaterialSurfaceParameters::UVOffset>("UVOffset"),
			SurfaceField<&MaterialSurfaceParameters::UVRotation>("UVRotation"),
			SurfaceField<&MaterialSurfaceParameters::AlphaMode>("AlphaMode"),
			SurfaceField<&MaterialSurfaceParameters::AlphaThreshold>("AlphaThreshold"),
			SurfaceField<&MaterialSurfaceParameters::TwoSided>("TwoSided"),
			SurfaceField<&MaterialSurfaceParameters::Transmission>("Transmission"),
			SurfaceField<&MaterialSurfaceParameters::IOR>("IOR"),
			SurfaceField<&MaterialSurfaceParameters::Thickness>("Thickness"),
			SurfaceField<&MaterialSurfaceParameters::AttenuationColor>("AttenuationColor"),
			SurfaceField<&MaterialSurfaceParameters::AttenuationDistance>("AttenuationDistance"),
			SurfaceField<&MaterialSurfaceParameters::DiffuseTransmission>("DiffuseTransmission"),
			SurfaceField<&MaterialSurfaceParameters::DiffuseTransmissionColor>("DiffuseTransmissionColor"),
			SurfaceField<&MaterialSurfaceParameters::DiffuseTransmissionMap>("DiffuseTransmissionMap"),
			SurfaceField<&MaterialSurfaceParameters::Clearcoat>("Clearcoat"),
			SurfaceField<&MaterialSurfaceParameters::ClearcoatRoughness>("ClearcoatRoughness"),
			SurfaceField<&MaterialSurfaceParameters::SheenColor>("SheenColor"),
			SurfaceField<&MaterialSurfaceParameters::SheenRoughness>("SheenRoughness"),
			SurfaceField<&MaterialSurfaceParameters::SubsurfaceColor>("SubsurfaceColor"),
			SurfaceField<&MaterialSurfaceParameters::SubsurfaceThickness>("SubsurfaceThickness"),
			SurfaceField<&MaterialSurfaceParameters::Anisotropy>("Anisotropy"),
			SurfaceField<&MaterialSurfaceParameters::AnisotropyRotation>("AnisotropyRotation"),
			SurfaceField<&MaterialSurfaceParameters::Mapping>("Mapping"),
			SurfaceField<&MaterialSurfaceParameters::TriplanarSharpness>("TriplanarSharpness"),
			SurfaceField<&MaterialSurfaceParameters::DetailAlbedoMap>("DetailAlbedoMap"),
			SurfaceField<&MaterialSurfaceParameters::DetailNormalMap>("DetailNormalMap"),
			SurfaceField<&MaterialSurfaceParameters::DetailMaskMap>("DetailMaskMap"),
			SurfaceField<&MaterialSurfaceParameters::DetailTiling>("DetailTiling"),
			SurfaceField<&MaterialSurfaceParameters::DetailNormalStrength>("DetailNormalStrength"),
		};

		#undef LUX_MATERIAL_FIELD
		#undef LUX_MATERIAL_MAP

		// A deeper chain than this is a cycle written by hand into the files.
		constexpr int k_MaxInstanceDepth = 32;

		const std::array<std::string_view, std::size(s_InstanceFields)> s_InstanceKeys = [] {
			std::array<std::string_view, std::size(s_InstanceFields)> keys{};
			for (size_t i = 0; i < keys.size(); i++)
				keys[i] = s_InstanceFields[i].Key;
			return keys;
		}();
	}

	std::span<const std::string_view> MaterialAsset::GetInstanceKeys()
	{
		return s_InstanceKeys;
	}

	bool MaterialAsset::SetParent(AssetHandle parent)
	{
		if (!parent)
		{
			// A former instance keeps its parent's graph as its own copy.
			if (m_Parent && m_Graph)
				m_Graph = m_Graph->Clone();
			m_Parent = 0;
			m_Overrides.clear();
			return true;
		}

		// An unknown handle is kept (a parent not imported yet, or deleted): the instance keeps its values.
		const AssetType type = AssetManager::GetAssetType(parent);
		if (parent == Handle || (type != AssetType::None && type != AssetType::Material))
			return false;

		// Walk up from the new parent: reaching this material would close a loop.
		AssetHandle ancestor = parent;
		for (int depth = 0; ancestor && depth < k_MaxInstanceDepth; depth++)
		{
			Ref<MaterialAsset> material = AssetManager::GetAsset<MaterialAsset>(ancestor);
			if (!material)
				break;
			ancestor = material->GetParent();
			if (ancestor == Handle)
				return false;
		}

		m_Parent = parent;
		return true;
	}

	void MaterialAsset::InheritFrom(const MaterialAsset& parent)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		for (const InstanceField& field : s_InstanceFields)
		{
			if (!m_Overrides.contains(field.Key) && !field.Equal(*this, parent))
				field.Copy(*this, parent);
		}

		// The graph is the parent's, and so is every parameter value not set here.
		m_Graph = parent.m_Graph;
		std::erase_if(m_GraphParameters, [&](const auto& entry) { return !m_Overrides.contains(GetGraphParameterKey(entry.first)); });
		for (const auto& [name, value] : parent.m_GraphParameters)
		{
			if (!m_Overrides.contains(GetGraphParameterKey(name)))
				m_GraphParameters[name] = value;
		}
		m_Revision++;
	}

	void MaterialAsset::UpdateOverrides(const MaterialAsset& parent)
	{
		m_Overrides.clear();
		for (const InstanceField& field : s_InstanceFields)
		{
			if (!field.Equal(*this, parent))
				m_Overrides.emplace(field.Key);
		}

		auto differs = [](const auto& map, const auto& other, const std::string& name)
		{
			auto a = map.find(name);
			auto b = other.find(name);
			if (a == map.end() || b == other.end())
				return (a == map.end()) != (b == other.end());
			return !(a->second == b->second);
		};
		for (const auto& [name, value] : m_GraphParameters)
		{
			if (differs(m_GraphParameters, parent.m_GraphParameters, name))
				m_Overrides.emplace(GetGraphParameterKey(name));
		}
	}

	MaterialGraphParameterValue MaterialAsset::GetGraphParameter(const MaterialGraphParameter& parameter) const
	{
		if (auto it = m_GraphParameters.find(parameter.Name); it != m_GraphParameters.end())
			return it->second;
		return { parameter.Default, parameter.DefaultTexture };
	}

	void MaterialAsset::SetGraphParameter(std::string_view name, const MaterialGraphParameterValue& value)
	{
		m_GraphParameters[std::string(name)] = value;
		m_Revision++;
	}

	void MaterialAsset::ResetToParent(std::string_view key, const MaterialAsset& parent)
	{
		if (auto it = m_Overrides.find(key); it != m_Overrides.end())
			m_Overrides.erase(it);
		InheritFrom(parent);
	}

	void MaterialAsset::PropagateToInstances(AssetHandle parent)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		std::vector<AssetHandle> pending = { parent };
		for (int depth = 0; !pending.empty() && depth < k_MaxInstanceDepth; depth++)
		{
			std::vector<AssetHandle> next;
			for (AssetHandle handle : pending)
			{
				Ref<MaterialAsset> source = AssetManager::GetAsset<MaterialAsset>(handle);
				if (!source)
					continue;
				// Collected first: inheriting can load textures, which adds to the loaded-asset map.
				std::vector<Ref<MaterialAsset>> children;
				for (const auto& [childHandle, asset] : AssetManager::GetLoadedAssets())
				{
					if (asset && asset->GetAssetType() == AssetType::Material && asset.As<MaterialAsset>()->GetParent() == handle)
						children.push_back(asset.As<MaterialAsset>());
				}
				for (Ref<MaterialAsset>& child : children)
				{
					child->InheritFrom(*source);
					next.push_back(child->Handle);
				}
			}
			pending = std::move(next);
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
