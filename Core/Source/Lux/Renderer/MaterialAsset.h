// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Asset/Asset.h"
#include "Lux/Renderer/Material.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>

namespace Lux {

	// Which channel of a packed (ORM-style) texture a scalar map reads.
	enum class MaterialTextureChannel : uint8_t
	{
		R = 0,
		G,
		B,
		A
	};

	// How a material's alpha is resolved. Opaque ignores alpha entirely; Cutout discards fragments
	// below AlphaThreshold (a hard edge, still rendered in the opaque passes so it lights and
	// shadows like solid geometry); Blend is the sorted transparent forward path.
	enum class MaterialAlphaMode : uint8_t
	{
		Opaque = 0,
		Cutout,
		Blend
	};

	// A Cutout cutoff as the 1/255 step that keys its coverage-preserving albedo
	// (GPUTextureSlot::AlphaCoverageCutoff, MaterialAsset::GetAlphaCoverageTexture). 0, nothing to
	// preserve, for a cutoff every texel passes.
	inline uint8_t GetAlphaCoverageCutoffKey(float alphaThreshold)
	{
		return static_cast<uint8_t>(std::clamp(std::round(alphaThreshold * 255.0f), 0.0f, 255.0f));
	}

	// The "Lux Standard" surface inputs beyond the basic PBR set. Defaults reproduce the shading of
	// materials authored before these existed, so a file without them renders unchanged.
	struct MaterialSurfaceParameters
	{
		// Emission: radiance = EmissiveColor (perceptual, like the base colour) x Emission x map.
		glm::vec3 EmissiveColor{ 1.0f };
		AssetHandle EmissiveMap = 0;

		// Ambient occlusion: occludes ambient/IBL light only.
		AssetHandle OcclusionMap = 0;
		float OcclusionStrength = 1.0f;
		MaterialTextureChannel OcclusionChannel = MaterialTextureChannel::R;

		// Packed-map channels for the existing metalness / roughness maps (glTF ORM layout).
		MaterialTextureChannel MetalnessChannel = MaterialTextureChannel::B;
		MaterialTextureChannel RoughnessChannel = MaterialTextureChannel::G;

		float Specular = 0.5f;       // dielectric reflectance; 0.5 = 4% F0
		float NormalStrength = 1.0f;

		// Height map used as a bump map; BumpHeight is its full range in world units (metres).
		AssetHandle HeightMap = 0;
		float BumpHeight = 0.02f;

		// Applied to every map: scale, then rotate (degrees), then offset.
		glm::vec2 UVTiling{ 1.0f };
		glm::vec2 UVOffset{ 0.0f };
		float UVRotation = 0.0f;

		// Alpha handling. Opaque + 0.5 reproduces pre-Cutout shading: the opaque passes ignored
		// alpha, so a material without these keys renders exactly as before.
		MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
		float AlphaThreshold = 0.5f;

		// Disables backface culling for this material in the depth and G-buffer passes; a transparent
		// material draws its back faces, then its front faces. Shadow cascades already render without
		// culling (to avoid peter-panning), so this does not change them.
		bool TwoSided = false;

		// Diffuse transmission (glTF KHR_materials_diffuse_transmission): the share of the diffuse
		// light that passes through to the far side instead of reflecting, tinted by
		// DiffuseTransmissionColor, with the map's alpha multiplying the factor. For thin surfaces lit
		// from behind, such as leaves. 0 is shading without it. Opaque and Cutout materials only.
		float DiffuseTransmission = 0.0f;
		glm::vec3 DiffuseTransmissionColor{ 1.0f };
		AssetHandle DiffuseTransmissionMap = 0;

		bool operator==(const MaterialSurfaceParameters& other) const = default;
	};

	class MaterialAsset : public Asset
	{
	public:
		explicit MaterialAsset(bool transparent = false);
		explicit MaterialAsset(Ref<Material> material);
		~MaterialAsset();

		virtual void OnDependencyUpdated(AssetHandle handle) override;

		glm::vec3 GetAlbedoColor() const { return m_Values.AlbedoColor; }
		void SetAlbedoColor(const glm::vec3& color);

		float GetMetalness() const { return m_Values.Metalness; }
		void SetMetalness(float value);

		float GetRoughness() const { return m_Values.Roughness; }
		void SetRoughness(float value);

		float GetEmission() const { return m_Values.Emission; }
		void SetEmission(float value);

		// Textures
		Ref<Texture2D> GetAlbedoMap();
		AssetHandle GetAlbedoMapHandle() const { return m_Maps.AlbedoMap; }
		void SetAlbedoMap(AssetHandle handle);
		void ClearAlbedoMap();

		Ref<Texture2D> GetNormalMap();
		AssetHandle GetNormalMapHandle() const { return m_Maps.NormalMap; }
		void SetNormalMap(AssetHandle handle);
		bool IsUsingNormalMap() const { return m_Values.UseNormalMap; }
		void SetUseNormalMap(bool value);
		void ClearNormalMap();

		Ref<Texture2D> GetMetalnessMap();
		AssetHandle GetMetalnessMapHandle() const { return m_Maps.MetalnessMap; }
		void SetMetalnessMap(AssetHandle handle);
		void ClearMetalnessMap();

		Ref<Texture2D> GetRoughnessMap();
		AssetHandle GetRoughnessMapHandle() const { return m_Maps.RoughnessMap; }
		void SetRoughnessMap(AssetHandle handle);
		void ClearRoughnessMap();

		float GetTransparency() const { return m_Values.Transparency; }
		void SetTransparency(float transparency);

		const MaterialSurfaceParameters& GetSurfaceParameters() const { return m_Surface; }
		void SetSurfaceParameters(const MaterialSurfaceParameters& parameters);

		bool IsShadowCasting() const { return !m_Material->GetFlag(MaterialFlag::DisableShadowCasting); }
		void SetShadowCasting(bool castsShadows) { m_Material->SetFlag(MaterialFlag::DisableShadowCasting, !castsShadows); m_Revision++; }
		void UpdateMaterialComplexityMetadata();

		// Bumped by every change to the asset's values, maps, surface parameters, shadow casting or
		// material. MaterialScene rebuilds a material's GPU row only when this (or the asset object)
		// changes, so a new setter must bump it too.
		uint64_t GetRevision() const { return m_Revision; }

		// The shared sRGB copy of a colour texture (albedo, emissive): the same bytes read as sRGB, with
		// a mip chain averaged in linear space. Built on first use and rebuilt when the source changes.
		// Returns the texture itself when it already is sRGB. Any thread.
		static Ref<Texture2D> GetSRGBTexture(AssetHandle textureHandle, const Ref<Texture2D>& texture);
		// The shared copy of a Cutout material's albedo whose mips keep mip 0's alpha-test coverage at the
		// cutoff `alphaCoverageCutoff` stands for (GetAlphaCoverageCutoffKey), so foliage does not thin
		// out with distance. Otherwise as GetSRGBTexture, which it returns for a texture the chain cannot
		// be built from (anything but 8-bit RGBA) or a cutoff of 0. Any thread.
		static Ref<Texture2D> GetAlphaCoverageTexture(AssetHandle textureHandle, const Ref<Texture2D>& texture, uint8_t alphaCoverageCutoff);

		// Drops the shared sRGB and alpha-coverage texture caches. Must run before the graphics device is
		// destroyed: the caches are file-scope statics, so without this their textures are freed by the
		// CRT at exit.
		static void ReleaseSharedResources();

		static AssetType GetStaticType() { return AssetType::Material; }
		virtual AssetType GetAssetType() const override { return GetStaticType(); }

		Ref<Material> GetMaterial() const { return m_Material; }
		void SetMaterial(Ref<Material> material) { m_Material = material; m_Revision++; }

		bool IsTransparent() const { return m_Transparent; }

		// The acoustic tag (an AcousticMaterial value) colliders inherit from this material, or -1
		// for none. Not a shading input: the renderer never reads it; it lives here so choosing a
		// surface once sets how it looks and how it sounds.
		int32_t GetAcousticTag() const { return m_AcousticTag; }
		void SetAcousticTag(int32_t tag) { m_AcousticTag = tag; }
	private:
		void SetDefaults();
		template<typename T>
		void WriteUniform(const std::string& name, const T& value);
		void WriteTexture(const std::string& name, const Ref<Texture2D>& texture);
		// Binds the albedo map's copy for the current alpha mode: coverage-preserving for Cutout, else sRGB.
		void WriteAlbedoTexture(AssetHandle handle, const Ref<Texture2D>& texture);
	private:
		Ref<Material> m_Material;
		uint64_t m_Revision = 1;

		// The asset's own copy of its scalar properties. The shader's push-constant block is not a
		// reliable store: the opaque shader has no Transparency member and the transparent shader
		// has no material members at all, so values written only there are lost (and reading a
		// missing member is an out-of-bounds read). MaterialScene reads these for the GPU table.
		struct Values
		{
			glm::vec3 AlbedoColor{ 1.0f };
			float Metalness = 0.0f;
			float Roughness = 0.4f;
			float Emission = 0.0f;
			float Transparency = 1.0f;
			bool UseNormalMap = false;
		} m_Values;

		struct MapAssets
		{
			AssetHandle AlbedoMap = 0;
			AssetHandle NormalMap = 0;
			AssetHandle MetalnessMap = 0;
			AssetHandle RoughnessMap = 0;
		} m_Maps;

		MaterialSurfaceParameters m_Surface;

		bool m_Transparent = false;
		int32_t m_AcousticTag = -1;

		friend class MaterialEditor;
	};

	class MaterialTable : public RefCounted
	{
	public:
		MaterialTable(uint32_t materialCount = 1);
		MaterialTable(Ref<MaterialTable> other);
		~MaterialTable() = default;

		bool HasMaterial(uint32_t materialIndex) const { return m_Materials.find(materialIndex) != m_Materials.end(); }
		void SetMaterial(uint32_t index, AssetHandle material);
		void ClearMaterial(uint32_t index);

		AssetHandle GetMaterial(uint32_t materialIndex) const
		{
			LUX_CORE_VERIFY(HasMaterial(materialIndex));
			return m_Materials.at(materialIndex);
		}
		// Read-only: change the table through SetMaterial / ClearMaterial / Clear, which count the change.
		const std::map<uint32_t, AssetHandle>& GetMaterials() const { return m_Materials; }

		uint32_t GetMaterialCount() const { return m_MaterialCount; }
		void SetMaterialCount(uint32_t materialCount) { m_MaterialCount = materialCount; MarkChanged(); }

		void Clear();

		// Bumped by every change, so the render scene re-reads a table only when it changed.
		uint64_t GetRevision() const { return m_Revision; }
		// Bumped by a change to any table: while it holds still, no table needs re-reading at all.
		static uint64_t GetGlobalRevision() { return s_GlobalRevision.load(std::memory_order_relaxed); }
	private:
		void MarkChanged() { m_Revision++; s_GlobalRevision.fetch_add(1, std::memory_order_relaxed); }

	private:
		std::map<uint32_t, AssetHandle> m_Materials;
		uint32_t m_MaterialCount;
		uint64_t m_Revision = 1;
		// Tables are also built by asset loads on worker threads.
		inline static std::atomic<uint64_t> s_GlobalRevision{ 1 };
	};

}
