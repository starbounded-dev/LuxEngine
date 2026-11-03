// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "NativeInternal.h"
#include "Miniaudio.h"

#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/SoundBank.h"

#include <map>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

// Which clips are in memory, and Sound Banks. A decoded clip is held by a resource-manager data
// source (Holder), so every voice of it shares one decoded buffer; a streaming clip holds nothing of
// its own. Main thread only: the decoding itself runs on the engine's streaming thread.
namespace Lux
{
	namespace
	{
		struct ClipEntry
		{
			Scope<ma_resource_manager_data_source> Holder;   // the decoded clip; null when it streams
			uint32_t BankRefs = 0;   // loaded banks listing it (a bank listing it twice counts twice)
			uint32_t LiveRefs = 0;   // voices playing it
			uint32_t Streams = 0;    // of LiveRefs, the streaming ones
			bool WantsRelease = false;   // a bank let go while voices still play it
		};

		struct BankState
		{
			uint32_t Loads = 0;
			bool Resident = false;   // stays loaded until shutdown, whatever unloads it is asked for
			std::vector<std::filesystem::path> Clips;   // one per (Sound, clip), as BankRefs counts them
		};

		// MA_RESOURCE_MANAGER_PAGE_SIZE_IN_MILLISECONDS's default, which only miniaudio's implementation defines.
		constexpr uint64_t kStreamPageMilliseconds = 1000;

		std::map<std::filesystem::path, ClipEntry> s_Clips;
		std::unordered_map<uint64_t, BankState> s_Banks;
		std::unordered_set<uint64_t> s_UnbankedReported;

		template<typename Char>
		ma_result InitHolder(ma_resource_manager* resources, const Char* name, ma_uint32 flags, ma_resource_manager_data_source* holder)
		{
			// The same name form ma_sound_init_from_file(_w) hashes, so voices share this buffer.
			if constexpr (std::is_same_v<Char, wchar_t>)
				return ma_resource_manager_data_source_init_w(resources, name, flags, nullptr, holder);
			else
				return ma_resource_manager_data_source_init(resources, name, flags, nullptr, holder);
		}

		// Decodes the clip into a shared buffer, on the streaming thread when async.
		ma_result Decode(const std::filesystem::path& file, ClipEntry& entry, bool async)
		{
			if (entry.Holder)
				return MA_SUCCESS;
			ma_engine* engine = NativeInternal::GetEngine();
			if (!engine)
				return MA_INVALID_OPERATION;
			auto holder = CreateScope<ma_resource_manager_data_source>();
			const ma_uint32 flags = MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE | (async ? MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC : 0u);
			const ma_result result = InitHolder(ma_engine_get_resource_manager(engine), file.c_str(), flags, holder.get());
			if (result == MA_SUCCESS)
				entry.Holder = std::move(holder);
			return result;
		}

		void Free(std::map<std::filesystem::path, ClipEntry>::iterator it)
		{
			if (it->second.Holder)
				ma_resource_manager_data_source_uninit(it->second.Holder.get());
			s_Clips.erase(it);
		}

		// A bank stopped listing the clip: free it now, or with its last voice.
		void ReleaseBankRef(const std::filesystem::path& file)
		{
			const auto it = s_Clips.find(file);
			if (it == s_Clips.end() || it->second.BankRefs == 0)
				return;
			if (--it->second.BankRefs > 0)
				return;
			if (it->second.LiveRefs == 0)
				Free(it);
			else
				it->second.WantsRelease = true;
		}

		uint64_t HeldBytes(ma_resource_manager_data_source& holder)
		{
			if (ma_resource_manager_data_source_result(&holder) != MA_SUCCESS)
				return 0;
			ma_format format = ma_format_unknown;
			ma_uint32 channels = 0, sampleRate = 0;   // miniaudio writes every output, so none may be null
			ma_uint64 frames = 0;
			if (ma_resource_manager_data_source_get_data_format(&holder, &format, &channels, &sampleRate, nullptr, 0) != MA_SUCCESS
				|| ma_resource_manager_data_source_get_length_in_pcm_frames(&holder, &frames) != MA_SUCCESS)
				return 0;
			return frames * ma_get_bytes_per_frame(format, channels);
		}
	}

	int NativeInternal::UseClip(const std::filesystem::path& file, bool stream, uint64_t soundHandle, std::string& outError)
	{
		ClipEntry& entry = s_Clips[file];
#ifdef LUX_DEBUG
		// A clip no bank holds decodes (or opens) on the main thread when it first plays.
		if (entry.BankRefs == 0 && entry.LiveRefs == 0 && !entry.Holder && soundHandle && s_UnbankedReported.insert(soundHandle).second)
			LUX_CORE_WARN_TAG("Audio", "Sound {} played outside any loaded bank; add it to a Sound Bank the scene loads", soundHandle);
#else
		(void)soundHandle;
#endif
		if (!stream)
		{
			const ma_result result = Decode(file, entry, false);
			if (result != MA_SUCCESS)
			{
				outError = "cannot decode '" + file.string() + "': " + ma_result_description(result);
				if (entry.BankRefs == 0 && entry.LiveRefs == 0)
					s_Clips.erase(file);
				return result;
			}
		}
		++entry.LiveRefs;
		if (stream)
			++entry.Streams;
		return MA_SUCCESS;
	}

	void NativeInternal::DropClip(const std::filesystem::path& file)
	{
		const auto it = s_Clips.find(file);
		if (it == s_Clips.end() || it->second.LiveRefs == 0)
			return;
		ClipEntry& entry = it->second;
		--entry.LiveRefs;
		if (entry.Streams > entry.LiveRefs)
			entry.Streams = entry.LiveRefs;
		if (entry.LiveRefs == 0 && entry.BankRefs == 0 && (entry.WantsRelease || !entry.Holder))
			Free(it);
	}

	void NativeInternal::ReleaseClips()
	{
		s_Banks.clear();
		while (!s_Clips.empty())
			Free(s_Clips.begin());
		s_UnbankedReported.clear();
	}

	uint64_t NativeInternal::GetClipMemoryBytes()
	{
		ma_engine* engine = GetEngine();
		uint64_t bytes = 0;
		for (auto& [file, entry] : s_Clips)
		{
			if (entry.Holder)
				bytes += HeldBytes(*entry.Holder);
			// shortcut: a stream's two pages counted at the engine's rate in stereo, an upper bound; read
			// the stream's own format if the figure needs to be exact.
			if (engine)
				bytes += entry.Streams * 2ull * kStreamPageMilliseconds * ma_engine_get_sample_rate(engine) / 1000 * 2 * sizeof(float);
		}
		return bytes;
	}

	bool AudioEngine::LoadSoundBank(UUID handle)
	{
		if (!NativeInternal::GetEngine())
			return false;
		if (const auto it = s_Banks.find(static_cast<uint64_t>(handle)); it != s_Banks.end())
		{
			if (!it->second.Resident)
				++it->second.Loads;
			return true;
		}
		Ref<SoundBank> bank;
		std::string error;
		if (!NativeInternal::ResolveSoundBank(handle, bank, error))
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot load Sound Bank {}: {}", static_cast<uint64_t>(handle), error);
			return false;
		}
		BankState state;
		state.Loads = 1;
		state.Resident = bank->Resident;
		for (const AssetHandle soundHandle : bank->Sounds)
		{
			Ref<Sound> sound;
			if (!NativeInternal::ResolveSound(std::to_string(static_cast<uint64_t>(soundHandle)), sound, error))
			{
				LUX_CORE_WARN_TAG("Audio", "Sound Bank {} lists Sound {}, which cannot be loaded: {}", static_cast<uint64_t>(handle), static_cast<uint64_t>(soundHandle), error);
				continue;
			}
			for (const AssetHandle clip : sound->GetClips())
			{
				std::filesystem::path file;
				if (!NativeInternal::ResolveClip(clip, file, error))
				{
					LUX_CORE_WARN_TAG("Audio", "Sound Bank {}: a clip of Sound {} cannot be loaded: {}", static_cast<uint64_t>(handle), static_cast<uint64_t>(soundHandle), error);
					continue;
				}
				ClipEntry& entry = s_Clips[file];
				++entry.BankRefs;
				entry.WantsRelease = false;
				if (!sound->Stream)
				{
					const ma_result result = Decode(file, entry, true);
					if (result != MA_SUCCESS)
						LUX_CORE_WARN_TAG("Audio", "Sound Bank {}: cannot decode '{}': {}", static_cast<uint64_t>(handle), file.string(), ma_result_description(result));
				}
				state.Clips.push_back(std::move(file));
			}
		}
		s_Banks.emplace(static_cast<uint64_t>(handle), std::move(state));
		return true;
	}

	void AudioEngine::UnloadSoundBank(UUID handle)
	{
		const auto it = s_Banks.find(static_cast<uint64_t>(handle));
		if (it == s_Banks.end() || it->second.Resident || --it->second.Loads > 0)
			return;
		const std::vector<std::filesystem::path> clips = std::move(it->second.Clips);
		s_Banks.erase(it);
		for (const auto& file : clips)
			ReleaseBankRef(file);
	}

	bool AudioEngine::IsSoundBankLoaded(UUID handle)
	{
		const auto it = s_Banks.find(static_cast<uint64_t>(handle));
		if (it == s_Banks.end())
			return false;
		for (const auto& file : it->second.Clips)
		{
			const auto clip = s_Clips.find(file);
			if (clip != s_Clips.end() && clip->second.Holder && ma_resource_manager_data_source_result(clip->second.Holder.get()) == MA_BUSY)
				return false;
		}
		return true;
	}

	std::vector<AudioLoadedBank> AudioEngine::GetLoadedSoundBanks()
	{
		std::vector<AudioLoadedBank> banks;
		for (const auto& [handle, state] : s_Banks)
			banks.push_back({ handle, state.Loads, state.Resident, IsSoundBankLoaded(handle) });
		return banks;
	}

	std::vector<AudioLoadedClip> AudioEngine::GetLoadedClips()
	{
		std::vector<AudioLoadedClip> clips;
		for (auto& [file, entry] : s_Clips)
			clips.push_back({ file, entry.BankRefs, entry.LiveRefs, entry.Streams, entry.Holder ? HeldBytes(*entry.Holder) : 0 });
		return clips;
	}

	void AudioEngine::ReleaseUnusedClips()
	{
		for (auto it = s_Clips.begin(); it != s_Clips.end();)
		{
			const auto next = std::next(it);
			if (it->second.BankRefs == 0 && it->second.LiveRefs == 0)
				Free(it);
			it = next;
		}
	}
}
