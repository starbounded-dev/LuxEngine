// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioFileUtils.h"
#include "Miniaudio.h"

#include <type_traits>

namespace Lux::AudioFileUtils
{
	namespace
	{
		// Picks miniaudio's narrow or wide file API by the platform's path character type.
		template<typename Char>
		ma_result OpenDecoder(const Char* path, const ma_decoder_config* config, ma_decoder* decoder)
		{
			if constexpr (std::is_same_v<Char, wchar_t>)
				return ma_decoder_init_file_w(path, config, decoder);
			else
				return ma_decoder_init_file(path, config, decoder);
		}
	}

	// Reads the header and length only; nothing is decoded and no engine is needed.
	std::optional<AudioFileInfo> GetFileInfo(const std::filesystem::path& filepath)
	{
		const ma_decoder_config config = ma_decoder_config_init(ma_format_unknown, 0, 0); // the file's own format
		ma_decoder decoder;
		ma_result result = OpenDecoder(filepath.c_str(), &config, &decoder);
		if (result != MA_SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot inspect '{0}': {1}", filepath.string(), ma_result_description(result));
			return {};
		}
		ma_format format = ma_format_unknown;
		ma_uint32 channels = 0, sampleRate = 0;
		ma_uint64 frames = 0;
		result = ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0);
		if (result == MA_SUCCESS)
			result = ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
		ma_decoder_uninit(&decoder);
		std::error_code ec;
		const auto size = std::filesystem::file_size(filepath, ec);
		if (result != MA_SUCCESS || sampleRate == 0 || ec)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot read audio metadata for '{0}': {1}; {2}", filepath.string(), ma_result_description(result), ec.message());
			return {};
		}
		return AudioFileInfo{ static_cast<double>(frames) / sampleRate, sampleRate,
			static_cast<uint16_t>(ma_get_bytes_per_sample(format) * 8), static_cast<uint16_t>(channels), size };
	}
}
