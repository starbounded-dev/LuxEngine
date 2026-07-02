#pragma once

#include "Lux/Asset/Asset.h"

#include <limits>
#include <unordered_map>
#include <vector>

namespace Lux {

	using GPUTextureIndex = uint32_t;
	inline constexpr GPUTextureIndex InvalidGPUTextureIndex = std::numeric_limits<GPUTextureIndex>::max();
	inline constexpr GPUTextureIndex FallbackGPUTextureIndex = 0;

	struct TextureSceneDirtyRange
	{
		uint32_t FirstTexture = 0;
		uint32_t TextureCount = 0;
	};

	class TextureScene
	{
	public:
		TextureScene();

		void BeginSync(uint32_t frameIndex);
		GPUTextureIndex UpsertTexture(AssetHandle textureHandle, bool forceDirty = false);
		void EndSync();
		void Clear();

		const std::vector<AssetHandle>& GetTextureHandles() const { return m_TextureHandles; }
		// Monotonic change counter: bumped whenever the texture table mutates
		// (MarkTextureDirty / Clear). Lets consumers skip re-copying the table on
		// frames where nothing changed. Never reset.
		uint64_t GetVersion() const { return m_Version; }
		const std::vector<TextureSceneDirtyRange>& GetDirtyRanges() const { return m_DirtyRanges; }
		bool HasDirtyTextures() const { return m_DirtyTextureCount > 0; }
		uint32_t GetDirtyTextureCount() const { return m_DirtyTextureCount; }
		size_t GetActiveTextureCount() const { return m_TextureIndexByHandle.size(); }

	private:
		void EnsureFallbackTexture();
		void MarkTextureDirty(GPUTextureIndex textureIndex);

	private:
		uint32_t m_FrameIndex = 0;
		uint32_t m_DirtyTextureCount = 0;
		uint64_t m_Version = 0;

		std::vector<AssetHandle> m_TextureHandles;
		std::vector<uint32_t> m_LastTouchedFrames;
		std::vector<GPUTextureIndex> m_FreeTextureIndices;
		std::unordered_map<AssetHandle, GPUTextureIndex> m_TextureIndexByHandle;

		std::vector<GPUTextureIndex> m_DirtyTextureIndices;
		std::vector<TextureSceneDirtyRange> m_DirtyRanges;
	};

}
