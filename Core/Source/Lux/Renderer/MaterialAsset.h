#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Asset/Asset.h"
#include "Lux/Renderer/Material.h"

#include <map>

namespace Lux {

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

		bool IsShadowCasting() const { return !m_Material->GetFlag(MaterialFlag::DisableShadowCasting); }
		void SetShadowCasting(bool castsShadows) { return m_Material->SetFlag(MaterialFlag::DisableShadowCasting, !castsShadows); }
		void UpdateMaterialComplexityMetadata();

		static AssetType GetStaticType() { return AssetType::Material; }
		virtual AssetType GetAssetType() const override { return GetStaticType(); }

		Ref<Material> GetMaterial() const { return m_Material; }
		void SetMaterial(Ref<Material> material) { m_Material = material; }

		bool IsTransparent() const { return m_Transparent; }
	private:
		void SetDefaults();
		template<typename T>
		void WriteUniform(const std::string& name, const T& value);
	private:
		Ref<Material> m_Material;

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

		bool m_Transparent = false;

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
		std::map<uint32_t, AssetHandle>& GetMaterials() { return m_Materials; }
		const std::map<uint32_t, AssetHandle>& GetMaterials() const { return m_Materials; }

		uint32_t GetMaterialCount() const { return m_MaterialCount; }
		void SetMaterialCount(uint32_t materialCount) { m_MaterialCount = materialCount; }

		void Clear();
	private:
		std::map<uint32_t, AssetHandle> m_Materials;
		uint32_t m_MaterialCount;
	};

}
