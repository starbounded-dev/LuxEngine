// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <miniaudio.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

namespace NativeAudioTest
{
	inline void Check(bool condition, const char* expression, int line)
	{
		if (!condition)
		{
			std::cerr << "FAIL at line " << line << ": " << expression << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

#define CHECK(condition) ::NativeAudioTest::Check(static_cast<bool>(condition), #condition, __LINE__)

	inline void CheckResult(ma_result result, const char* operation, int line)
	{
		if (result != MA_SUCCESS)
		{
			std::cerr << "FAIL at line " << line << ": " << operation << " returned " << ma_result_description(result) << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

#define CHECK_MA(call) ::NativeAudioTest::CheckResult((call), #call, __LINE__)

	// Interleaved float frames plus their layout.
	struct Pcm
	{
		std::vector<float> Samples;
		uint32_t Channels = 0;
		uint32_t SampleRate = 0;

		uint64_t FrameCount() const { return Channels ? Samples.size() / Channels : 0; }
		float At(uint64_t frame, uint32_t channel) const { return Samples[frame * Channels + channel]; }
	};

	inline std::filesystem::path FixtureDirectory()
	{
		return std::filesystem::path(LUX_NATIVE_AUDIO_FIXTURES);
	}

	// Decodes a whole file to f32 at its native channel count and rate.
	inline Pcm DecodeFile(const std::filesystem::path& path)
	{
		ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
		ma_decoder decoder;
		CHECK_MA(ma_decoder_init_file(path.string().c_str(), &config, &decoder));
		Pcm pcm;
		pcm.Channels = decoder.outputChannels;
		pcm.SampleRate = decoder.outputSampleRate;
		std::vector<float> block(4096 * pcm.Channels);
		for (;;)
		{
			ma_uint64 read = 0;
			const ma_result result = ma_decoder_read_pcm_frames(&decoder, block.data(), 4096, &read);
			pcm.Samples.insert(pcm.Samples.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(read * pcm.Channels));
			if (result == MA_AT_END || read == 0)
				break;
			CHECK_MA(result);
		}
		ma_decoder_uninit(&decoder);
		return pcm;
	}

	// Root-mean-square of one channel over [first, first + count).
	inline double Rms(const Pcm& pcm, uint32_t channel, uint64_t first, uint64_t count)
	{
		double sum = 0.0;
		for (uint64_t frame = first; frame < first + count && frame < pcm.FrameCount(); ++frame)
			sum += static_cast<double>(pcm.At(frame, channel)) * pcm.At(frame, channel);
		return count ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
	}

	// Reads frameCount frames from an engine created with noDevice.
	inline Pcm Render(ma_engine& engine, uint64_t frameCount)
	{
		Pcm pcm;
		pcm.Channels = ma_engine_get_channels(&engine);
		pcm.SampleRate = ma_engine_get_sample_rate(&engine);
		pcm.Samples.resize(frameCount * pcm.Channels);
		ma_uint64 read = 0;
		CHECK_MA(ma_engine_read_pcm_frames(&engine, pcm.Samples.data(), frameCount, &read));
		CHECK(read == frameCount);
		return pcm;
	}

	inline ma_engine_config OfflineEngineConfig(uint32_t channels, uint32_t sampleRate)
	{
		ma_engine_config config = ma_engine_config_init();
		config.noDevice = MA_TRUE;
		config.channels = channels;
		config.sampleRate = sampleRate;
		return config;
	}
}
