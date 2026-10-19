// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

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
		OverrideMaterial = BIT(11),
		HasEmissiveTexture = BIT(12),
		HasOcclusionTexture = BIT(13),
		HasHeightTexture = BIT(14)
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

	// ExtraTextureIndices.w: two bits per packed-map channel selection (MaterialTextureChannel).
	inline constexpr uint32_t GPUMaterialChannelShiftMetalness = 0;
	inline constexpr uint32_t GPUMaterialChannelShiftRoughness = 2;
	inline constexpr uint32_t GPUMaterialChannelShiftOcclusion = 4;
	// glTF ORM layout: occlusion R, roughness G, metalness B.
	inline constexpr uint32_t GPUMaterialDefaultChannelSelects =
		((uint32_t)MaterialTextureChannel::B << GPUMaterialChannelShiftMetalness)
		| ((uint32_t)MaterialTextureChannel::G << GPUMaterialChannelShiftRoughness)
		| ((uint32_t)MaterialTextureChannel::R << GPUMaterialChannelShiftOcclusion);

	// Mirrored by `GPUMaterial` in Include/GLSL/MaterialScene.glslh (std430, (set 2, binding 7)).
	// Edit both in the same change.
	struct GPUMaterialData
	{
		glm::vec4 BaseColor = glm::vec4(1.0f); // linear RGB, alpha = opacity/transparency
		glm::vec4 Scalars = glm::vec4(0.0f, 0.5f, 0.0f, 1.0f); // x metalness, y roughness, z emission, w material complexity
		glm::uvec4 TextureIndices = glm::uvec4(InvalidGPUTextureIndex); // x albedo, y normal, z metalness, w roughness
		glm::uvec4 Metadata = glm::uvec4(0); // x flags, y alpha mode, z render material ID, w floatBits(env map rotation)
		glm::vec4 Emissive = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // rgb linear emissive radiance, w occlusion strength
		glm::vec4 Surface = glm::vec4(0.5f, 1.0f, 0.5f, 0.0f); // x specular, y normal strength, z alpha cutoff, w bump height
		glm::vec4 UVTransform = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // 2x2 matrix columns (xy, zw): rotation x tiling
		glm::vec4 UVOffset = glm::vec4(0.0f); // xy offset, zw reserved
		glm::uvec4 ExtraTextureIndices = glm::uvec4(InvalidGPUTextureIndex, InvalidGPUTextureIndex, InvalidGPUTextureIndex, GPUMaterialDefaultChannelSelects); // x emissive, y occlusion, z height, w channel selects
	};
	static_assert(sizeof(GPUMaterialData) == 9 * 16, "GPUMaterialData must match the std430 GPUMaterial layout");

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

	// Maps a material's texture to its bindless slot. Colour maps ask for TextureColorSpace::SRGB.
	using GPUTextureResolver = std::function<GPUTextureIndex(AssetHandle, TextureColorSpace)>;

	// The GPU material table of one RenderScene. Asset materials are reference counted: a row lives
	// while something holds it, and is rebuilt only when its MaterialAsset object or that asset's
	// revision changes. Rows hold references on their textures' slots in the TextureScene.
	class MaterialScene
	{
	public:
		MaterialScene();

		// The texture slots material rows reference. Set once by the owning RenderScene.
		void SetTextureScene(TextureScene* textureScene) { m_TextureScene = textureScene; }
		void BeginSync(uint32_t frameIndex);
		// Takes a reference on the material's row, building it on first use. Each acquired ID is
		// released exactly once with ReleaseMaterial. Handle 0 returns InvalidRenderMaterialID.
		RenderMaterialID AcquireMaterial(AssetHandle materialHandle);
		void ReleaseMaterial(RenderMaterialID materialID);
		// Unreferenced: an override row lives while it is upserted every frame.
		RenderMaterialID UpsertOverrideMaterial(uint64_t overrideKey, const Ref<Material>& material, bool transparent, bool forceDirty = false);
		// Rebuilds asset rows whose asset changed, then frees rows nothing holds.
		void EndSync();
		void Clear();

		const std::vector<GPUMaterialData>& GetMaterials() const { return m_Materials; }
		// Monotonic change counter: bumped whenever the material table mutates
		// (MarkMaterialDirty / Clear). Lets consumers skip re-copying the table on
		// frames where nothing changed. Never reset.
		uint64_t GetVersion() const { return m_Version; }
		const std::vector<MaterialSceneDirtyRange>& GetDirtyRanges() const { return m_DirtyRanges; }
		bool HasDirtyMaterials() const { return m_DirtyMaterialCount > 0; }
		uint32_t GetDirtyMaterialCount() const { return m_DirtyMaterialCount; }
		size_t GetActiveMaterialCount() const { return m_MaterialIDByKey.size(); }

		static GPUMaterialData GetFallbackMaterialData();
		static GPUMaterialData BuildGPUMaterialData(
			const GPUMaterialBuildInput& input,
			const GPUTextureResolver& resolveTextureIndex);

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

		// CPU state of one row of m_Materials.
		struct MaterialRow
		{
			MaterialKey Key;
			uint32_t RefCount = 0;
			uint32_t LastTouchedFrame = 0;
			Ref<MaterialAsset> Asset;  // asset rows: the object the row was built from
			uint64_t BuiltRevision = 0; // asset rows: Asset->GetRevision() when built
			std::vector<GPUTextureIndex> Textures; // slots the row holds a reference on
		};

		RenderMaterialID AllocateMaterialRow(MaterialKey key);
		void BuildAssetMaterialRow(RenderMaterialID materialID);
		void BuildMaterialRow(RenderMaterialID materialID, const GPUMaterialBuildInput& input, bool forceDirty);
		void FreeMaterialRow(RenderMaterialID materialID);
		void MarkMaterialDirty(RenderMaterialID materialID);
		void EnsureFallbackMaterial();

	private:
		uint32_t m_FrameIndex = 0;
		uint32_t m_DirtyMaterialCount = 0;
		uint64_t m_Version = 0;
		TextureScene* m_TextureScene = nullptr;

		std::vector<GPUMaterialData> m_Materials;
		std::vector<MaterialRow> m_Rows;
		std::vector<RenderMaterialID> m_FreeMaterialIDs;
		std::unordered_map<MaterialKey, RenderMaterialID, MaterialKeyHasher> m_MaterialIDByKey;

		std::vector<RenderMaterialID> m_DirtyMaterialIDs;
		std::vector<MaterialSceneDirtyRange> m_DirtyRanges;
	};

}
