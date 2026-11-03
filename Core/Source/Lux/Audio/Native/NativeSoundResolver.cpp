// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "NativeInternal.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/SoundBank.h"
#include "Lux/Project/Project.h"

// Playback references against the asset manager. Kept out of the playback files so the native
// tests can supply Sounds without a project.
namespace Lux
{
	namespace
	{
		// An asset by handle (in decimal), or by its path relative to Assets with or without the
		// extension: the registry in the editor, the runtime project's path table in a game.
		AssetHandle FindByReference(const std::string& reference, const char* extension, std::string& outError)
		{
			uint64_t handle = 0;
			if (NativeInternal::ParseHandle(reference, handle))
				return handle;
			if (reference.starts_with("event:/") || reference.starts_with("snapshot:/") || reference.starts_with("{"))
			{
				outError = "it is an FMOD event reference; FMOD is gone, assign a Sound asset";
				return 0;
			}
			if (std::filesystem::path(reference).extension() == ".bank")
			{
				outError = "it is an FMOD bank; FMOD is gone, make a Sound Bank (New > Sound Bank) and load it by its path";
				return 0;
			}
			std::filesystem::path path(reference);
			if (path.extension() == extension)
				path.replace_extension();
			AssetHandle found = 0;
			if (auto* editor = dynamic_cast<EditorAssetManager*>(Project::GetAssetManager().Raw()))
				found = editor->GetAssetHandleFromFilePath(std::filesystem::path(path).concat(extension));
			else if (const auto project = Project::GetActive())
			{
				const auto& paths = project->GetConfig().Audio.AudioAssetPaths;
				if (const auto it = paths.find(path.generic_string()); it != paths.end())
					found = it->second;
			}
			if (!found)
				outError = "no asset has that path";
			return found;
		}

		AssetHandle FindSound(const std::string& reference, std::string& outError)
		{
			return FindByReference(reference, ".lsound", outError);
		}
	}

	bool NativeInternal::ResolveSound(const std::string& reference, Ref<Sound>& outSound, std::string& outError)
	{
		if (!Project::GetAssetManager())
		{
			outError = "no project is open";
			return false;
		}
		const AssetHandle handle = FindSound(reference, outError);
		if (!handle)
			return false;
		if (!AssetManager::IsAssetHandleValid(handle) || AssetManager::GetAssetType(handle) != AssetType::Sound)
		{
			outError = "it is not a Sound asset";
			return false;
		}
		outSound = AssetManager::GetAsset<Sound>(handle);
		if (!outSound)
		{
			outError = "the Sound could not be loaded";
			return false;
		}
		return true;
	}

	UUID AudioEngine::FindSoundBank(const std::string& reference)
	{
		std::string error;
		const AssetHandle bank = Project::GetAssetManager() ? FindByReference(reference, ".lbank", error) : AssetHandle(0);
		if (bank && AssetManager::IsAssetHandleValid(bank) && AssetManager::GetAssetType(bank) == AssetType::SoundBank)
			return bank;
		LUX_CORE_ERROR_TAG("Audio", "Cannot find Sound Bank '{}': {}", reference, error.empty() ? "it is not a Sound Bank asset" : error);
		return 0;
	}

	bool NativeInternal::ResolveSoundBank(AssetHandle bank, Ref<SoundBank>& outBank, std::string& outError)
	{
		if (!Project::GetAssetManager())
		{
			outError = "no project is open";
			return false;
		}
		if (!bank || !AssetManager::IsAssetHandleValid(bank) || AssetManager::GetAssetType(bank) != AssetType::SoundBank)
		{
			outError = "it is not a Sound Bank asset";
			return false;
		}
		outBank = AssetManager::GetAsset<SoundBank>(bank);
		if (!outBank)
		{
			outError = "the Sound Bank could not be loaded";
			return false;
		}
		return true;
	}

	bool NativeInternal::ResolveClip(AssetHandle clip, std::filesystem::path& outPath, std::string& outError)
	{
		if (!Project::GetAssetManager())
		{
			outError = "no project is open";
			return false;
		}
		if (!clip || !AssetManager::IsAssetHandleValid(clip) || AssetManager::GetAssetType(clip) != AssetType::Audio)
		{
			outError = "clip " + std::to_string(static_cast<uint64_t>(clip)) + " is not an audio asset";
			return false;
		}
		outPath.clear();
		if (auto* editor = dynamic_cast<EditorAssetManager*>(Project::GetAssetManager().Raw()))
			outPath = editor->GetFileSystemPath(clip);
		else if (const auto file = AssetManager::GetAsset<AudioFile>(clip))
		{
			if (!file->PackFile.empty())
			{
				// Read in place inside the pack, under a name that keeps the extension miniaudio decodes by.
				outPath = std::filesystem::path("luxpack") / (std::to_string(static_cast<uint64_t>(clip)) + std::filesystem::path(file->FilePath).extension().string());
				if (!NativeInternal::RegisterPackedClip(outPath, file->PackFile, file->PackOffset, file->FileSize))
				{
					outError = "the packed clip cannot be decoded: " + file->FilePath;
					return false;
				}
				return true;
			}
			outPath = Project::GetActiveAssetDirectory() / file->FilePath; // an older pack: loose files beside it
		}
		std::error_code error;
		if (outPath.empty() || !std::filesystem::is_regular_file(outPath, error))
		{
			outError = "the clip file is missing: " + outPath.string();
			return false;
		}
		return true;
	}

	std::string AudioEngine::ResolveEventReference(const std::string& reference)
	{
		std::string error;
		const AssetHandle handle = FindSound(reference, error);
		if (!handle)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot resolve sound '{}': {}", reference, error);
			return {};
		}
		return std::to_string(static_cast<uint64_t>(handle));
	}
}
