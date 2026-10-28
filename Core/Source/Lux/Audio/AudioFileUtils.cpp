// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioFileUtils.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Utilities/StringUtils.h"

namespace Lux::AudioFileUtils
{
	std::optional<AudioFileInfo> GetFileInfo(const AssetMetadata& metadata)
	{
		return GetFileInfo(std::filesystem::path(Project::GetEditorAssetManager()->GetFileSystemPathString(metadata)));
	}

	bool IsValidAudioFile(const std::filesystem::path& filepath)
	{
		// TODO: 1. get list of supported formats; 2. optionally open the file and read metadata

		if (Utils::String::EqualsIgnoreCase(filepath.extension().string(), ".wav"))
			return true;
		else if (Utils::String::EqualsIgnoreCase(filepath.extension().string(), ".ogg"))
			return true;

		return false;
	}

	std::string ChannelsToLayoutString(uint16_t numChannels)
	{
		// TODO: should be able to get actual channel layout from some file formats
		//		and should add a function to get the output speaker layout of the audio device

		std::string str;
		switch (numChannels)
		{
		case 1: str = "Mono"; break;
		case 2: str = "Stereo"; break;
		case 3: str = "3.0"; break;
		case 4: str = "Quad"; break;
		case 5: str = "5.0"; break;
		case 6: str = "5.1"; break;
		//case 7: str = "Unknown"; break;
		case 8: str = "7.1"; break;
		default: str = "Unknown layout"; break;
		}

		return str;
	}
}

