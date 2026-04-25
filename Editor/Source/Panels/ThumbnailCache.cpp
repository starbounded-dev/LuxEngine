#include "lpch.h"
#include "ThumbnailCache.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"

#include <chrono>
#include <fstream>

namespace Lux {
	namespace
	{
		Ref<Texture2D> LoadTextureFromFile(const std::filesystem::path& path)
		{
			TextureSpecification spec;
			spec.Format = ImageFormat::SRGBA;
			spec.GenerateMips = false;
			spec.DebugName = path.string();
			return Texture2D::Create(spec, path);
		}
	}

	ThumbnailCache::ThumbnailCache(Ref<Project> project)
		: m_Project(project)
	{
		EnsureCacheDirectoryExists();
	}

	// -----------------------------------------------------------------
	// Path helpers
	// -----------------------------------------------------------------

	std::filesystem::path ThumbnailCache::GetCacheDirectory() const
	{
		// Project methods are not const so go through the static accessor.
		return Project::GetActive()->GetAssetDirectory() / "Cache" / "Thumbnails";
	}

	std::filesystem::path ThumbnailCache::GetCacheFilePath(AssetHandle assetHandle) const
	{
		// One file per asset, keyed by handle - same as Hazel's naming scheme.
		return GetCacheDirectory() / std::format("{}.thumbnail", (uint64_t)assetHandle);
	}

	void ThumbnailCache::EnsureCacheDirectoryExists() const
	{
		auto dir = GetCacheDirectory();
		if (!std::filesystem::exists(dir))
			std::filesystem::create_directories(dir);
	}

	// -----------------------------------------------------------------
	// Public query API  (mirrors Hazel's ThumbnailCache interface)
	// -----------------------------------------------------------------

	bool ThumbnailCache::HasThumbnail(AssetHandle assetHandle) const
	{
		if (m_CachedImages.contains(assetHandle))
			return true;
		return std::filesystem::exists(GetCacheFilePath(assetHandle));
	}

	uint64_t ThumbnailCache::GetThumbnailTimestamp(AssetHandle assetHandle) const
	{
		if (m_CachedImages.contains(assetHandle))
			return m_CachedImages.at(assetHandle).LastWriteTime;
		return ReadTimestampFromDisk(assetHandle);
	}

	uint64_t ThumbnailCache::GetAssetTimestamp(AssetHandle assetHandle) const
	{
		// Resolve handle -> filesystem path via the asset manager, then
		// read the file's last-write-time exactly as Hazel does.
		const auto& registry = Project::GetActive()->GetEditorAssetManager()->GetAssetRegistry();
		auto it = registry.find(assetHandle);
		if (it == registry.end())
			return 0;

        // Project methods are not const, use the static active project accessor here
        auto absPath = Project::GetActive()->GetAssetAbsolutePath(it->second.FilePath);
		if (!std::filesystem::exists(absPath))
			return 0;

		auto lwt = std::filesystem::last_write_time(absPath);
		return std::chrono::duration_cast<std::chrono::seconds>(
			lwt.time_since_epoch()).count();
	}

	bool ThumbnailCache::IsThumbnailCurrent(AssetHandle assetHandle) const
	{
		return GetThumbnailTimestamp(assetHandle) >= GetAssetTimestamp(assetHandle);
	}

	// -----------------------------------------------------------------
	// GetOrCreateThumbnail
	// Main entry point called by ContentBrowserPanel each frame.
	// Returns null on first call (queues generation); valid on subsequent calls.
	// -----------------------------------------------------------------

	Ref<Texture2D> ThumbnailCache::GetOrCreateThumbnail(const std::filesystem::path& assetRelativePath)
	{
		auto absPath = Project::GetActive()->GetAssetAbsolutePath(assetRelativePath);
		if (!std::filesystem::exists(absPath))
			return nullptr;

		// Resolve relative path -> AssetHandle via registry
		AssetHandle handle = 0;
		uint64_t timestamp = 0;
		{
			auto lwt = std::filesystem::last_write_time(absPath);
			timestamp = std::chrono::duration_cast<std::chrono::seconds>(
				lwt.time_since_epoch()).count();

			const auto& registry = Project::GetActive()->GetEditorAssetManager()->GetAssetRegistry();
			for (const auto& [h, meta] : registry)
			{
				if (meta.FilePath == assetRelativePath)
				{
					handle = h;
					break;
				}
			}
		}

		// 1. Check memory cache first
		if (handle && m_CachedImages.contains(handle))
		{
			auto& cached = m_CachedImages.at(handle);
			if (cached.LastWriteTime == timestamp)
				return cached.Image;
			// stale - fall through to re-queue
		}

		// 2. Check disk cache
		if (handle && HasThumbnail(handle) && IsThumbnailCurrent(handle))
		{
			if (LoadFromDisk(handle))
				return m_CachedImages.at(handle).Image;
		}

		// 3. Only PNG thumbnails for now (matches old Lux behaviour)
		if (assetRelativePath.extension() != ".png")
			return nullptr;

		// Queue for generation (skip if already queued)
		m_Queue.push({ absPath, assetRelativePath, handle, timestamp });
		return nullptr;
	}

	void ThumbnailCache::SetThumbnailImage(AssetHandle assetHandle, Ref<Texture2D> image, uint64_t timestamp)
	{
		m_CachedImages[assetHandle] = { image, timestamp };
		WriteToDisk(assetHandle, image, timestamp);
	}

	void ThumbnailCache::Clear()
	{
		m_CachedImages.clear();
	}

	// -----------------------------------------------------------------
	// OnUpdate - process one pending thumbnail per frame
	// -----------------------------------------------------------------

	void ThumbnailCache::OnUpdate()
	{
		while (!m_Queue.empty())
		{
			const auto& info = m_Queue.front();

			// Already cached and current?
			if (info.Handle && m_CachedImages.contains(info.Handle))
			{
				if (m_CachedImages.at(info.Handle).LastWriteTime == info.Timestamp)
				{
					m_Queue.pop();
					continue;
				}
			}

			// FIX: load texture BEFORE calling Resize, then null-check.
			// The old code called texture->Resize() before checking if
			// texture was valid, causing a crash on failed loads.
			Ref<Texture2D> texture = LoadTextureFromFile(info.AbsolutePath);
			if (!texture)
			{
				m_Queue.pop();
				continue;
			}

			// Scale to thumbnail size, preserving aspect ratio
			uint32_t thumbHeight = m_ThumbnailSize;
			if (texture->GetWidth() > 0)
			{
				thumbHeight = static_cast<uint32_t>(
					m_ThumbnailSize * (float(texture->GetHeight()) / float(texture->GetWidth())));
			}
			texture->Resize(m_ThumbnailSize, thumbHeight);

			AssetHandle handle = info.Handle;
			if (!handle)
			{
				// Asset not in registry yet - store by path hash as fallback
				handle = std::hash<std::string>{}(info.RelativePath.generic_string());
			}

			m_CachedImages[handle] = { texture, info.Timestamp };
			WriteToDisk(handle, texture, info.Timestamp);

			m_Queue.pop();
			break; // one per frame
		}
	}

	// -----------------------------------------------------------------
	// Disk I/O
	// -----------------------------------------------------------------

	bool ThumbnailCache::LoadFromDisk(AssetHandle assetHandle)
	{
		auto filepath = GetCacheFilePath(assetHandle);
		if (!std::filesystem::exists(filepath))
			return false;

		std::ifstream file(filepath, std::ios::binary);
		if (!file)
			return false;

		ThumbnailFileHeader header;
		file.read(reinterpret_cast<char*>(&header), sizeof(header));

		// Validate magic
		if (header.Magic[0] != 'L' || header.Magic[1] != 'X' ||
			header.Magic[2] != 'T' || header.Magic[3] != 'N')
			return false;

		if (header.Width == 0 || header.Height == 0)
			return false;

		uint32_t dataSize = header.Width * header.Height * 4; // RGBA8
		std::vector<uint8_t> pixelData(dataSize);
		file.read(reinterpret_cast<char*>(pixelData.data()), dataSize);

		if (!file)
			return false;

		// Build spec field-by-field to avoid designated initializer ordering issues.
		TextureSpecification texSpec;
		texSpec.Width = header.Width;
		texSpec.Height = header.Height;
		texSpec.Format = ImageFormat::RGBA;   // RGBA8_UNORM in NVRHI terms
		texSpec.DebugName = "Thumbnail";

		Ref<Texture2D> texture = Texture2D::Create(texSpec,
			Buffer{ pixelData.data(), (uint64_t)dataSize });

		if (!texture)
			return false;

		m_CachedImages[assetHandle] = { texture, header.LastWriteTime };
		return true;
	}

	uint64_t ThumbnailCache::ReadTimestampFromDisk(AssetHandle assetHandle) const
	{
		auto filepath = GetCacheFilePath(assetHandle);
		if (!std::filesystem::exists(filepath))
			return 0;

		std::ifstream file(filepath, std::ios::binary);
		if (!file)
			return 0;

		ThumbnailFileHeader header;
		file.read(reinterpret_cast<char*>(&header), sizeof(header));
		return header.LastWriteTime;
	}

	void ThumbnailCache::WriteToDisk(AssetHandle assetHandle, Ref<Texture2D> image, uint64_t timestamp)
	{
		if (!image)
			return;

		EnsureCacheDirectoryExists();
		auto filepath = GetCacheFilePath(assetHandle);

		// Texture2D::CopyToHostBuffer reads pixels back from GPU into a Buffer.
		// This is the correct API - GetRawData does not exist on Texture2D.
		Buffer pixelData;
		image->CopyToHostBuffer(pixelData);
		if (!pixelData)
			return;

		ThumbnailFileHeader header;
		header.Width = static_cast<uint16_t>(image->GetWidth());
		header.Height = static_cast<uint16_t>(image->GetHeight());
		header.LastWriteTime = timestamp;

		std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
		if (!file)
			return;

		file.write(reinterpret_cast<const char*>(&header), sizeof(header));
		file.write(reinterpret_cast<const char*>(pixelData.Data), pixelData.Size);
	}

}
