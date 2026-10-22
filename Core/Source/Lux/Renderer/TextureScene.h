// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"

#include <limits>
#include <unordered_map>
#include <vector>

namespace Lux {

	using GPUTextureIndex = uint32_t;
	inline constexpr GPUTextureIndex InvalidGPUTextureIndex = std::numeric_limits<GPUTextureIndex>::max();
	inline constexpr GPUTextureIndex FallbackGPUTextureIndex = 0;

	// Colour textures (albedo, emissive) are bound through their sRGB copy, so the sampler decodes
	// them to linear and their mips are averaged in linear space. Data textures are bound as stored.
	enum class TextureColorSpace : uint8_t
	{
		Linear = 0,
		SRGB = 1
	};

	// One bindless texture slot: the same texture used as colour and as data takes two slots, and so
	// does an albedo a Cutout material alpha-tests (its mips keep their coverage at that cutoff).
	struct GPUTextureSlot
	{
		AssetHandle Handle = 0;
		TextureColorSpace ColorSpace = TextureColorSpace::Linear;
		// A Cutout material's cutoff (GetAlphaCoverageCutoffKey) for sRGB albedo; 0 for every other slot.
		uint8_t AlphaCoverageCutoff = 0;

		bool operator==(const GPUTextureSlot& other) const
		{
			return Handle == other.Handle && ColorSpace == other.ColorSpace && AlphaCoverageCutoff == other.AlphaCoverageCutoff;
		}
	};

	struct GPUTextureSlotHasher
	{
		size_t operator()(const GPUTextureSlot& slot) const
		{
			size_t seed = std::hash<AssetHandle>{}(slot.Handle);
			const uint32_t slotKind = static_cast<uint32_t>(slot.ColorSpace) | (static_cast<uint32_t>(slot.AlphaCoverageCutoff) << 8);
			seed ^= std::hash<uint32_t>{}(slotKind) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
			return seed;
		}
	};

	struct TextureSceneDirtyRange
	{
		uint32_t FirstTexture = 0;
		uint32_t TextureCount = 0;
	};

	class TextureScene
	{
	public:
		TextureScene();

		void BeginSync();
		// Takes a reference on the texture's bindless slot, allocating the slot on first use. Returns
		// InvalidGPUTextureIndex (and takes no reference) for a handle that is not a texture asset.
		// Each acquired index is released exactly once with ReleaseTexture.
		GPUTextureIndex AcquireTexture(const GPUTextureSlot& slot);
		void ReleaseTexture(GPUTextureIndex textureIndex);
		// Frees the slots whose last reference was released this frame.
		void EndSync();
		void Clear();

		const std::vector<GPUTextureSlot>& GetTextureSlots() const { return m_TextureSlots; }
		// Monotonic change counter: bumped whenever the texture table mutates
		// (MarkTextureDirty / Clear). Lets consumers skip re-copying the table on
		// frames where nothing changed. Never reset.
		uint64_t GetVersion() const { return m_Version; }
		const std::vector<TextureSceneDirtyRange>& GetDirtyRanges() const { return m_DirtyRanges; }
		bool HasDirtyTextures() const { return m_DirtyTextureCount > 0; }
		uint32_t GetDirtyTextureCount() const { return m_DirtyTextureCount; }
		size_t GetActiveTextureCount() const { return m_TextureIndexBySlot.size(); }

	private:
		void EnsureFallbackTexture();
		void MarkTextureDirty(GPUTextureIndex textureIndex);

	private:
		uint32_t m_DirtyTextureCount = 0;
		uint64_t m_Version = 0;

		std::vector<GPUTextureSlot> m_TextureSlots;
		std::vector<uint32_t> m_RefCounts;
		std::vector<GPUTextureIndex> m_FreeTextureIndices;
		// Slots whose count reached zero this frame; EndSync frees those still at zero.
		std::vector<GPUTextureIndex> m_ReleasedTextureIndices;
		std::unordered_map<GPUTextureSlot, GPUTextureIndex, GPUTextureSlotHasher> m_TextureIndexBySlot;

		std::vector<GPUTextureIndex> m_DirtyTextureIndices;
		std::vector<TextureSceneDirtyRange> m_DirtyRanges;
	};

}
