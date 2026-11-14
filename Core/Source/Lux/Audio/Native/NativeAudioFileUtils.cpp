// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioFileUtils.h"
#include "Miniaudio.h"

#include <algorithm>
#include <cmath>
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

		// Some decoders (Vorbis from a file on Windows) report a length of 0; then count the frames by
		// decoding once and rewind.
		ma_result GetLength(ma_decoder* decoder, ma_uint64* length)
		{
			ma_result result = ma_decoder_get_length_in_pcm_frames(decoder, length);
			if (result != MA_SUCCESS || *length != 0)
				return result;
			ma_format format = ma_format_unknown;
			ma_uint32 channels = 0;
			result = ma_decoder_get_data_format(decoder, &format, &channels, nullptr, nullptr, 0);
			if (result != MA_SUCCESS)
				return result;
			constexpr ma_uint64 kChunkFrames = 4096;
			std::vector<uint8_t> chunk(kChunkFrames * ma_get_bytes_per_frame(format, channels));
			ma_uint64 read = 0;
			do
			{
				result = ma_decoder_read_pcm_frames(decoder, chunk.data(), kChunkFrames, &read);
				*length += read;
			} while (result == MA_SUCCESS && read != 0);
			return result == MA_SUCCESS || result == MA_AT_END ? ma_decoder_seek_to_pcm_frame(decoder, 0) : result;
		}
	}

	// Reads the header and length only (decoding only when the format does not store its length);
	// no engine is needed.
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
			result = GetLength(&decoder, &frames);
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

	std::optional<std::vector<float>> GetWaveform(const std::filesystem::path& filepath, uint32_t buckets)
	{
		constexpr uint32_t kMaxBuckets = 65536;
		constexpr ma_uint64 kChunkFrames = 4096;
		if (buckets == 0 || buckets > kMaxBuckets)
			return {};
		// Float at the file's own channel count and rate: peaks are measured, not resampled.
		const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
		ma_decoder decoder;
		ma_result result = OpenDecoder(filepath.c_str(), &config, &decoder);
		if (result != MA_SUCCESS)
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot read the waveform of '{0}': {1}", filepath.string(), ma_result_description(result));
			return {};
		}
		ma_uint32 channels = 0;
		ma_uint64 length = 0;
		result = ma_decoder_get_data_format(&decoder, nullptr, &channels, nullptr, nullptr, 0);
		if (result == MA_SUCCESS)
			result = GetLength(&decoder, &length);
		if (result != MA_SUCCESS || channels == 0 || length == 0)
		{
			ma_decoder_uninit(&decoder);
			LUX_CORE_ERROR_TAG("Audio", "Cannot read the waveform of '{0}': {1}", filepath.string(),
				result != MA_SUCCESS ? ma_result_description(result) : "it has no audio");
			return {};
		}
		std::vector<float> peaks(buckets, 0.0f);
		std::vector<float> chunk(kChunkFrames * channels);
		ma_uint64 position = 0;
		while (position < length)
		{
			ma_uint64 read = 0;
			result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), kChunkFrames, &read);
			for (ma_uint64 frame = 0; frame < read; ++frame)
			{
				const size_t bucket = static_cast<size_t>(std::min<ma_uint64>((position + frame) * buckets / length, buckets - 1));
				for (ma_uint32 channel = 0; channel < channels; ++channel)
					peaks[bucket] = std::max(peaks[bucket], std::min(1.0f, std::abs(chunk[frame * channels + channel])));
			}
			position += read;
			if (result != MA_SUCCESS || read == 0)
				break; // MA_AT_END, or a decode error part way: show what was read
		}
		ma_decoder_uninit(&decoder);
		return peaks;
	}
}
