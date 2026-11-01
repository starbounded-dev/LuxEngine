// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "NativeInternal.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Project/Project.h"

// Playback references against the asset manager. Kept out of the playback files so the native
// tests can supply Sounds without a project.
namespace Lux
{
	namespace
	{
		// A Sound by handle, or by its path relative to the project's Assets directory (editor only:
		// asset packs carry handles, not paths).
		AssetHandle FindSound(const std::string& reference, std::string& outError)
		{
			uint64_t handle = 0;
			if (NativeInternal::ParseHandle(reference, handle))
				return handle;
			if (reference.starts_with("event:/") || reference.starts_with("snapshot:/") || reference.starts_with("{"))
			{
				outError = "it is an FMOD event reference, and the native backend plays Sound assets";
				return 0;
			}
			auto* editor = dynamic_cast<EditorAssetManager*>(Project::GetAssetManager().Raw());
			if (!editor)
			{
				outError = "Sound paths resolve only in the editor; reference the Sound asset itself";
				return 0;
			}
			const AssetHandle found = editor->GetAssetHandleFromFilePath(reference);
			if (!found)
				outError = "no asset has that path";
			return found;
		}
	}

	bool NativeInternal::ResolveSound(const std::string& reference, ResolvedSound& outSound, std::string& outError)
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
		const auto sound = AssetManager::GetAsset<Sound>(handle);
		if (!sound)
		{
			outError = "the Sound could not be loaded";
			return false;
		}
		if (!sound->Clip || !AssetManager::IsAssetHandleValid(sound->Clip) || AssetManager::GetAssetType(sound->Clip) != AssetType::Audio)
		{
			outError = "the Sound has no audio clip assigned";
			return false;
		}
		std::filesystem::path clip;
		if (auto* editor = dynamic_cast<EditorAssetManager*>(Project::GetAssetManager().Raw()))
			clip = editor->GetFileSystemPath(sound->Clip);
		else if (const auto file = AssetManager::GetAsset<AudioFile>(sound->Clip))
			clip = Project::GetActiveAssetDirectory() / file->FilePath; // loose files until asset packs carry audio
		std::error_code error;
		if (clip.empty() || !std::filesystem::is_regular_file(clip, error))
		{
			outError = "its clip file is missing: " + clip.string();
			return false;
		}
		outSound.Asset = sound;
		outSound.Clip = std::move(clip);
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
