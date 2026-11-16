// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "NativeInternal.h"
#include "Miniaudio.h"

#include <cstdio>
#include <mutex>
#include <unordered_map>

// The resource manager's file system: "luxpack/<handle><ext>" names a clip inside the asset pack, read
// in place at its offset; every other name is a loose file. miniaudio's job threads (async decoding,
// stream pages) open and read through it concurrently, so each open file has its own handle.
namespace Lux
{
	namespace
	{
		struct PackEntry
		{
			std::filesystem::path Pack;
			uint64_t Offset = 0, Size = 0;
			double Seconds = 0.0;
		};

		struct OpenFile
		{
			FILE* Pack = nullptr;        // a clip inside a pack
			ma_vfs_file Loose = nullptr; // or a loose file, opened by the default VFS
			uint64_t Offset = 0, Size = 0, Position = 0;
		};

		struct PackVFS
		{
			ma_vfs_callbacks Callbacks;
			ma_default_vfs Loose;
		};

		PackVFS s_VFS;
		bool s_VFSReady = false;
		std::mutex s_EntriesMutex;
		std::unordered_map<std::string, PackEntry> s_Entries;   // by generic name, "luxpack/123.wav"

		ma_vfs* LooseVFS() { return reinterpret_cast<ma_vfs*>(&s_VFS.Loose); }

		bool Seek(OpenFile& file)
		{
#ifdef LUX_PLATFORM_WINDOWS
			return _fseeki64(file.Pack, static_cast<int64_t>(file.Offset + file.Position), SEEK_SET) == 0;
#else
			return fseeko(file.Pack, static_cast<off_t>(file.Offset + file.Position), SEEK_SET) == 0;
#endif
		}

		ma_result Open(const std::filesystem::path& name, ma_uint32 openMode, ma_vfs_file* outFile)
		{
			PackEntry entry;
			{
				std::scoped_lock lock(s_EntriesMutex);
				const auto it = s_Entries.find(name.generic_string());
				if (it == s_Entries.end())
					return MA_DOES_NOT_EXIST;
				entry = it->second;
			}
			if (openMode & MA_OPEN_MODE_WRITE)
				return MA_ACCESS_DENIED;
#ifdef LUX_PLATFORM_WINDOWS
			FILE* pack = _wfopen(entry.Pack.c_str(), L"rb");
#else
			FILE* pack = fopen(entry.Pack.c_str(), "rb");
#endif
			if (!pack)
				return MA_DOES_NOT_EXIST;
			auto* file = new OpenFile{ pack, nullptr, entry.Offset, entry.Size, 0 };
			if (!Seek(*file))
			{
				fclose(pack);
				delete file;
				return MA_IO_ERROR;
			}
			*outFile = file;
			return MA_SUCCESS;
		}

		bool IsPackName(const std::filesystem::path& name)
		{
			const auto first = name.begin();
			return first != name.end() && *first == "luxpack";
		}

		ma_result OnOpen(ma_vfs*, const char* path, ma_uint32 openMode, ma_vfs_file* outFile)
		{
			if (IsPackName(path))
				return Open(path, openMode, outFile);
			ma_vfs_file loose = nullptr;
			const ma_result result = ma_vfs_open(LooseVFS(), path, openMode, &loose);
			if (result == MA_SUCCESS)
				*outFile = new OpenFile{ nullptr, loose };
			return result;
		}

		ma_result OnOpenW(ma_vfs*, const wchar_t* path, ma_uint32 openMode, ma_vfs_file* outFile)
		{
			if (IsPackName(path))
				return Open(path, openMode, outFile);
			ma_vfs_file loose = nullptr;
			const ma_result result = ma_vfs_open_w(LooseVFS(), path, openMode, &loose);
			if (result == MA_SUCCESS)
				*outFile = new OpenFile{ nullptr, loose };
			return result;
		}

		ma_result OnClose(ma_vfs*, ma_vfs_file handle)
		{
			auto* file = static_cast<OpenFile*>(handle);
			const ma_result result = file->Loose ? ma_vfs_close(LooseVFS(), file->Loose) : (fclose(file->Pack) == 0 ? MA_SUCCESS : MA_IO_ERROR);
			delete file;
			return result;
		}

		ma_result OnRead(ma_vfs*, ma_vfs_file handle, void* destination, size_t bytes, size_t* outRead)
		{
			auto* file = static_cast<OpenFile*>(handle);
			if (file->Loose)
				return ma_vfs_read(LooseVFS(), file->Loose, destination, bytes, outRead);
			// Never past the clip's end: what follows in the pack is another asset.
			const size_t wanted = static_cast<size_t>(std::min<uint64_t>(bytes, file->Size - file->Position));
			const size_t read = wanted ? fread(destination, 1, wanted, file->Pack) : 0;
			file->Position += read;
			if (outRead)
				*outRead = read;
			if (read == 0 && bytes > 0)
				return wanted == 0 ? MA_AT_END : MA_IO_ERROR;
			return MA_SUCCESS;
		}

		ma_result OnWrite(ma_vfs*, ma_vfs_file, const void*, size_t, size_t*)
		{
			return MA_ACCESS_DENIED;
		}

		ma_result OnSeek(ma_vfs*, ma_vfs_file handle, ma_int64 offset, ma_seek_origin origin)
		{
			auto* file = static_cast<OpenFile*>(handle);
			if (file->Loose)
				return ma_vfs_seek(LooseVFS(), file->Loose, offset, origin);
			const int64_t base = origin == ma_seek_origin_start ? 0
				: origin == ma_seek_origin_current ? static_cast<int64_t>(file->Position) : static_cast<int64_t>(file->Size);
			const int64_t target = base + offset;
			if (target < 0 || target > static_cast<int64_t>(file->Size))
				return MA_BAD_SEEK;
			file->Position = static_cast<uint64_t>(target);
			return Seek(*file) ? MA_SUCCESS : MA_BAD_SEEK;
		}

		ma_result OnTell(ma_vfs*, ma_vfs_file handle, ma_int64* outCursor)
		{
			auto* file = static_cast<OpenFile*>(handle);
			if (file->Loose)
				return ma_vfs_tell(LooseVFS(), file->Loose, outCursor);
			*outCursor = static_cast<ma_int64>(file->Position);
			return MA_SUCCESS;
		}

		ma_result OnInfo(ma_vfs*, ma_vfs_file handle, ma_file_info* outInfo)
		{
			auto* file = static_cast<OpenFile*>(handle);
			if (file->Loose)
				return ma_vfs_info(LooseVFS(), file->Loose, outInfo);
			outInfo->sizeInBytes = file->Size;
			return MA_SUCCESS;
		}
	}

	ma_vfs* NativeInternal::GetPackVFS()
	{
		if (!s_VFSReady)
		{
			s_VFS.Callbacks = { OnOpen, OnOpenW, OnClose, OnRead, OnWrite, OnSeek, OnTell, OnInfo };
			ma_default_vfs_init(&s_VFS.Loose, nullptr);
			s_VFSReady = true;
		}
		return reinterpret_cast<ma_vfs*>(&s_VFS);
	}

	bool NativeInternal::RegisterPackedClip(const std::filesystem::path& name, const std::filesystem::path& pack, uint64_t offset, uint64_t size)
	{
		const std::string key = name.generic_string();
		{
			std::scoped_lock lock(s_EntriesMutex);
			if (s_Entries.contains(key))
				return true;
			s_Entries[key] = { pack, offset, size };
		}
		// Measured once, through the VFS: a packed clip has no file for AudioFileUtils to read. Bad bytes
		// fail here, so the clip is refused before anything plays it.
		ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
		ma_decoder decoder;
		double seconds = -1.0;
		if (ma_decoder_init_vfs(GetPackVFS(), key.c_str(), &config, &decoder) == MA_SUCCESS)
		{
			ma_uint64 frames = 0;
			ma_uint32 sampleRate = 0;
			if (ma_decoder_get_data_format(&decoder, nullptr, nullptr, &sampleRate, nullptr, 0) == MA_SUCCESS
				&& ma_decoder_get_length_in_pcm_frames(&decoder, &frames) == MA_SUCCESS && sampleRate)
				seconds = static_cast<double>(frames) / sampleRate;
			ma_decoder_uninit(&decoder);
		}
		std::scoped_lock lock(s_EntriesMutex);
		if (seconds < 0.0)
		{
			s_Entries.erase(key);
			return false;
		}
		s_Entries[key].Seconds = seconds;
		return true;
	}

	bool NativeInternal::IsPackedClip(const std::filesystem::path& path)
	{
		std::scoped_lock lock(s_EntriesMutex);
		return s_Entries.contains(path.generic_string());
	}

	double NativeInternal::GetPackedClipSeconds(const std::filesystem::path& path)
	{
		std::scoped_lock lock(s_EntriesMutex);
		const auto it = s_Entries.find(path.generic_string());
		return it != s_Entries.end() ? it->second.Seconds : 0.0;
	}

	void NativeInternal::ClearPackedClips()
	{
		std::scoped_lock lock(s_EntriesMutex);
		s_Entries.clear();
	}
}
