#pragma once

#include "Lux/Project/Project.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Asset/Asset.h"

#include <queue>
#include <map>

namespace Lux {

	// Matches the ThumbnailImage layout used in Hazel's ThumbnailGenerator.h
	// so the two systems can share the same data representation.
	struct ThumbnailImage
	{
		Ref<Texture2D> Image;
		uint64_t LastWriteTime = 0;

		operator bool() const { return (bool)Image; }
	};

	// -----------------------------------------------------------------
	// ThumbnailCache
	//
	// Two-level cache: in-memory map (fast) + binary disk files (persistent).
	// Architecture mirrors Hazel's ThumbnailCache closely:
	//   - Each asset gets its own .thumbnail file keyed by AssetHandle.
	//   - A timestamp comparison decides whether the cached image is current.
	//   - Disk I/O happens asynchronously (one asset per OnUpdate call).
	//
	// Differences from the old Lux ThumbnailCache:
	//   - Old: path-keyed, single cache file, no disk persistence.
	//   - New: handle-keyed, per-asset cache files, Hazel-compatible format.
	// -----------------------------------------------------------------

	class ThumbnailCache : public RefCounted
	{
	public:
		explicit ThumbnailCache(Ref<Project> project);

		// Returns true if a thumbnail exists in memory or on disk for this asset.
		bool HasThumbnail(AssetHandle assetHandle) const;

		// Timestamp of the cached thumbnail (0 if none).
		uint64_t GetThumbnailTimestamp(AssetHandle assetHandle) const;

		// Timestamp of the source asset on disk.
		uint64_t GetAssetTimestamp(AssetHandle assetHandle) const;

		// True when thumbnail timestamp >= asset timestamp.
		bool IsThumbnailCurrent(AssetHandle assetHandle) const;

		// Returns the cached thumbnail image, loading from disk if necessary.
		// May return null on the first call for an asset; check again next frame.
		Ref<Texture2D> GetOrCreateThumbnail(const std::filesystem::path& assetRelativePath);

		// Store a freshly generated thumbnail (writes to disk on next OnUpdate).
		void SetThumbnailImage(AssetHandle assetHandle, Ref<Texture2D> image, uint64_t timestamp);

		// Call once per frame - processes one pending disk write per call.
		void OnUpdate();

		void Clear();

	private:
		bool  LoadFromDisk(AssetHandle assetHandle);
		uint64_t ReadTimestampFromDisk(AssetHandle assetHandle) const;
		void  WriteToDisk(AssetHandle assetHandle, Ref<Texture2D> image, uint64_t timestamp);

		std::filesystem::path GetCacheDirectory() const;
		std::filesystem::path GetCacheFilePath(AssetHandle assetHandle) const;
		void EnsureCacheDirectoryExists() const;

	private:
		Ref<Project> m_Project;

		// In-memory cache: AssetHandle -> thumbnail + timestamp
		std::map<AssetHandle, ThumbnailImage> m_CachedImages;

		// Queue of assets awaiting thumbnail generation (one processed per frame)
		struct PendingThumbnail
		{
			std::filesystem::path AbsolutePath;    // full path to source asset
			std::filesystem::path RelativePath;    // relative to asset directory
			AssetHandle Handle = 0;
			uint64_t    Timestamp = 0;
		};
		std::queue<PendingThumbnail> m_Queue;

		uint32_t m_ThumbnailSize = 128;

		// Binary file header - matches Hazel's ThumbnailFileHeader layout
		// so cache files are forward-compatible with a full Hazel port.
		struct ThumbnailFileHeader
		{
			char     Magic[4] = { 'L', 'X', 'T', 'N' };  // LuX ThumbnailN
			uint16_t Format = 0;   // reserved
			uint16_t Version = 1;
			uint16_t Width = 0;
			uint16_t Height = 0;
			uint32_t Padding = 0;
			uint64_t LastWriteTime = 0;
			// Followed by raw RGBA8 pixel data: Width * Height * 4 bytes
		};
	};

}
