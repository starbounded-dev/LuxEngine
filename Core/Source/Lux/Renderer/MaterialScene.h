#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Core/Base.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Renderer/TextureScene.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Lux {

	using RenderMaterialID = uint32_t;
	inline constexpr RenderMaterialID InvalidRenderMaterialID = 0;

	enum class GPUMaterialAlphaMode : uint32_t
	{
		Opaque = 0,
		Masked = 1,
		Blend = 2
	};

	enum class GPUMaterialFlags : uint32_t
	{
		None = 0,
		Valid = BIT(0),
		Missing = BIT(1),
		Transparent = BIT(2),
		ShadowCasting = BIT(3),
		TwoSided = BIT(4),
		UseNormalMap = BIT(5),
		HasAlbedoTexture = BIT(6),
		HasNormalTexture = BIT(7),
		HasMetalnessTexture = BIT(8),
		HasRoughnessTexture = BIT(9),
		MissingTexture = BIT(10),
		OverrideMaterial = BIT(11)
	};

	inline constexpr GPUMaterialFlags operator|(GPUMaterialFlags lhs, GPUMaterialFlags rhs)
	{
		return (GPUMaterialFlags)((uint32_t)lhs | (uint32_t)rhs);
	}

	inline GPUMaterialFlags& operator|=(GPUMaterialFlags& lhs, GPUMaterialFlags rhs)
	{
		lhs = lhs | rhs;
		return lhs;
	}

	inline constexpr bool HasGPUMaterialFlag(uint32_t flags, GPUMaterialFlags mask)
	{
		return (flags & (uint32_t)mask) != 0;
	}

	struct GPUMaterialData
	{
		glm::vec4 BaseColor = glm::vec4(1.0f); // linear RGB, alpha = opacity/transparency
		glm::vec4 Scalars = glm::vec4(0.0f, 0.5f, 0.0f, 1.0f); // x metalness, y roughness, z emission, w material complexity
		glm::uvec4 TextureIndices = glm::uvec4(InvalidGPUTextureIndex);
		glm::uvec4 Metadata = glm::uvec4(0); // x flags, y alpha mode, z render material ID, w reserved
	};

	struct GPUMaterialBuildInput
	{
		AssetHandle MaterialHandle = 0;
		Ref<MaterialAsset> MaterialAsset;
		Ref<Material> OverrideMaterial;
		bool Transparent = false;
	};

	struct MaterialSceneDirtyRange
	{
		uint32_t FirstMaterial = 0;
		uint32_t MaterialCount = 0;
	};

	class MaterialScene
	{
	public:
		MaterialScene();

		void SetTextureResolver(std::function<GPUTextureIndex(AssetHandle)> textureResolver);
		void BeginSync(uint32_t frameIndex);
		RenderMaterialID UpsertMaterial(AssetHandle materialHandle, bool forceDirty = false);
		RenderMaterialID UpsertOverrideMaterial(uint64_t overrideKey, const Ref<Material>& material, bool transparent, bool forceDirty = false);
		void EndSync();
		void Clear();

		const std::vector<GPUMaterialData>& GetMaterials() const { return m_Materials; }
		const std::vector<MaterialSceneDirtyRange>& GetDirtyRanges() const { return m_DirtyRanges; }
		bool HasDirtyMaterials() const { return m_DirtyMaterialCount > 0; }
		uint32_t GetDirtyMaterialCount() const { return m_DirtyMaterialCount; }
		size_t GetActiveMaterialCount() const { return m_MaterialIDByKey.size(); }
		uint32_t GetTextureIndexCount() const { return m_TextureResolver ? 0 : (m_NextTextureIndex > 0 ? m_NextTextureIndex - 1 : 0); }

		static GPUMaterialData GetFallbackMaterialData();
		static GPUMaterialData BuildGPUMaterialData(
			const GPUMaterialBuildInput& input,
			const std::function<GPUTextureIndex(AssetHandle)>& resolveTextureIndex);

	private:
		enum class MaterialKeyType : uint32_t
		{
			Asset = 0,
			Override = 1
		};

		struct MaterialKey
		{
			uint64_t SourceID = 0;
			MaterialKeyType Type = MaterialKeyType::Asset;

			bool operator==(const MaterialKey& other) const
			{
				return SourceID == other.SourceID && Type == other.Type;
			}
		};

		struct MaterialKeyHasher
		{
			size_t operator()(const MaterialKey& key) const
			{
				size_t seed = std::hash<uint64_t>{}(key.SourceID);
				seed ^= std::hash<uint32_t>{}((uint32_t)key.Type) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
				return seed;
			}
		};

		RenderMaterialID UpsertMaterial(MaterialKey key, GPUMaterialData data, bool forceDirty);
		GPUTextureIndex ResolveTextureIndex(AssetHandle textureHandle);
		void MarkMaterialDirty(RenderMaterialID materialID);
		void EnsureFallbackMaterial();

	private:
		uint32_t m_FrameIndex = 0;
		uint32_t m_DirtyMaterialCount = 0;
		GPUTextureIndex m_NextTextureIndex = 1;
		std::function<GPUTextureIndex(AssetHandle)> m_TextureResolver;

		std::vector<GPUMaterialData> m_Materials;
		std::vector<MaterialKey> m_MaterialKeys;
		std::vector<uint32_t> m_LastTouchedFrames;
		std::vector<RenderMaterialID> m_FreeMaterialIDs;
		std::unordered_map<MaterialKey, RenderMaterialID, MaterialKeyHasher> m_MaterialIDByKey;
		std::unordered_map<AssetHandle, GPUTextureIndex> m_TextureIndexByHandle;

		std::vector<RenderMaterialID> m_DirtyMaterialIDs;
		std::vector<MaterialSceneDirtyRange> m_DirtyRanges;
	};

}
