// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Asset/AssetMetadata.h"

#include <optional>
#include <vector>

namespace Lux::AudioFileUtils
{
	struct AudioFileInfo
	{
		double Duration;
		uint32_t SamplingRate;
		uint16_t BitDepth;
		uint16_t NumChannels;
		uint64_t FileSize;
	};

	std::optional<AudioFileInfo> GetFileInfo(const AssetMetadata& metadata);
	std::optional<AudioFileInfo> GetFileInfo(const std::filesystem::path& filepath);

	bool IsValidAudioFile(const std::filesystem::path& filepath);

	// The peak level (0 to 1, over every channel) of each of `buckets` equal slices of the file, for
	// waveform views. Decodes the whole file on the calling thread. The native backend reads it with
	// miniaudio.
	std::optional<std::vector<float>> GetWaveform(const std::filesystem::path& filepath, uint32_t buckets);

	std::string ChannelsToLayoutString(uint16_t numChannels);
}

