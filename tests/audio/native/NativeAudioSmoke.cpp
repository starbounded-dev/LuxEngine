// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// Plays through the real output device, for the Phase 0 listening check on Windows and Linux:
//   lux_native_audio_smoke [file]
// It prints the backend and device, plays the file (default: the sample project's music) for up to
// five seconds, then sweeps a tone from left to right over three seconds.

#include "NativeAudioTestUtils.h"

#include <chrono>
#include <string>
#include <thread>

using namespace NativeAudioTest;

int main(int argc, char** argv)
{
	const std::string file = argc > 1 ? argv[1] : LUX_SAMPLE_MUSIC;

	const auto initStart = std::chrono::steady_clock::now();
	ma_engine engine;
	const ma_engine_config config = ma_engine_config_init();
	if (ma_engine_init(&config, &engine) != MA_SUCCESS)
	{
		std::cerr << "No output device could be opened\n";
		return EXIT_FAILURE;
	}
	const double initMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - initStart).count();

	ma_device* device = ma_engine_get_device(&engine);
	char deviceName[256] = {};
	ma_device_get_name(device, ma_device_type_playback, deviceName, sizeof(deviceName), nullptr);
	std::cout << "miniaudio " << ma_version_string() << ", backend " << ma_get_backend_name(device->pContext->backend)
		<< ", device \"" << deviceName << "\", " << ma_engine_get_sample_rate(&engine) << " Hz, "
		<< ma_engine_get_channels(&engine) << " channels, period " << device->playback.internalPeriodSizeInFrames
		<< " frames, engine init " << initMs << " ms\n";

	ma_sound sound;
	if (ma_sound_init_from_file(&engine, file.c_str(), MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, nullptr, &sound) != MA_SUCCESS)
	{
		std::cerr << "Cannot open " << file << '\n';
		ma_engine_uninit(&engine);
		return EXIT_FAILURE;
	}
	std::cout << "Playing " << file << '\n';
	ma_sound_start(&sound);
	for (int i = 0; i < 50 && !ma_sound_at_end(&sound); ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	ma_sound_uninit(&sound);

	std::cout << "Sweeping a tone from left to right\n";
	ma_waveform waveform;
	const ma_waveform_config waveConfig = ma_waveform_config_init(ma_format_f32, 1, ma_engine_get_sample_rate(&engine), ma_waveform_type_sine, 0.3, 440.0);
	ma_waveform_init(&waveConfig, &waveform);
	ma_sound tone;
	ma_sound_init_from_data_source(&engine, &waveform, 0, nullptr, &tone);
	ma_sound_set_position(&tone, -3.0f, 0.0f, -1.0f);
	ma_sound_start(&tone);
	for (int i = 0; i <= 60; ++i)
	{
		ma_sound_set_position(&tone, -3.0f + 0.1f * static_cast<float>(i), 0.0f, -1.0f);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	ma_sound_uninit(&tone);
	ma_waveform_uninit(&waveform);
	ma_engine_uninit(&engine);
	return EXIT_SUCCESS;
}
