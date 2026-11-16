// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// Phase 0 stress benchmark (docs/AUDIO_NATIVE_BACKEND_PLAN.md). The graph is the shape Phase 2-4
// will build: every voice is 3D, goes through its own low-pass (occlusion) and a splitter, and feeds
// one of four buses plus a shared reverb return. Rendering is offline, so the timed call is exactly
// the work the device thread would do per period.
//   lux_native_audio_bench [voices=64] [seconds=10]

#include "NativeAudioTestUtils.h"

#include <extras/nodes/ma_reverb_node/ma_reverb_node.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <numbers>
#include <string>

using namespace NativeAudioTest;

namespace
{
	constexpr uint32_t kRate = 48000;
	constexpr uint32_t kPeriod = 480; // 10 ms, a typical shared-mode WASAPI/PulseAudio period
	constexpr uint32_t kBusCount = 4;
	constexpr uint32_t kStreamedVoices = 4;
	constexpr uint32_t kWarmupBlocks = 50;

	using Clock = std::chrono::steady_clock;

	double Microseconds(Clock::duration duration)
	{
		return std::chrono::duration<double, std::micro>(duration).count();
	}

	struct Voice
	{
		ma_sound Sound;
		ma_lpf_node LowPass;
		ma_splitter_node Splitter;
		float Radius = 0.0f, Phase = 0.0f, Speed = 0.0f;
	};
}

int main(int argc, char** argv)
{
	const uint32_t voiceCount = argc > 1 ? static_cast<uint32_t>(std::stoul(argv[1])) : 64;
	const double seconds = argc > 2 ? std::stod(argv[2]) : 10.0;
	const uint32_t blocks = static_cast<uint32_t>(seconds * kRate / kPeriod);

	const auto initStart = Clock::now();
	const ma_engine_config config = OfflineEngineConfig(2, kRate);
	ma_engine engine;
	CHECK_MA(ma_engine_init(&config, &engine));
	const double initUs = Microseconds(Clock::now() - initStart);
	ma_node_graph* graph = ma_engine_get_node_graph(&engine);

	std::array<ma_sound_group, kBusCount> buses;
	for (auto& bus : buses)
		CHECK_MA(ma_sound_group_init(&engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &bus));

	ma_reverb_node reverb;
	const ma_reverb_node_config reverbConfig = ma_reverb_node_config_init(2, kRate);
	CHECK_MA(ma_reverb_node_init(graph, &reverbConfig, nullptr, &reverb));
	CHECK_MA(ma_node_attach_output_bus(&reverb, 0, ma_engine_get_endpoint(&engine), 0));

	const std::array<std::string, 2> decodedFiles = { (FixtureDirectory() / "tone.wav").string(), (FixtureDirectory() / "tone.ogg").string() };
	const auto loadStart = Clock::now();
	std::vector<std::unique_ptr<Voice>> voices;
	voices.reserve(voiceCount);
	for (uint32_t i = 0; i < voiceCount; ++i)
	{
		auto voice = std::make_unique<Voice>();
		const bool streamed = i < kStreamedVoices;
		const std::string& file = streamed ? std::string(LUX_SAMPLE_MUSIC) : decodedFiles[i % decodedFiles.size()];
		CHECK_MA(ma_sound_init_from_file(&engine, file.c_str(), streamed ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE, nullptr, nullptr, &voice->Sound));
		ma_sound_set_looping(&voice->Sound, MA_TRUE);
		ma_sound_set_volume(&voice->Sound, 0.1f);
		ma_sound_set_doppler_factor(&voice->Sound, 1.0f);

		const ma_lpf_node_config lpfConfig = ma_lpf_node_config_init(2, kRate, 2000.0 + 100.0 * i, 2);
		CHECK_MA(ma_lpf_node_init(graph, &lpfConfig, nullptr, &voice->LowPass));
		const ma_splitter_node_config splitterConfig = ma_splitter_node_config_init(2);
		CHECK_MA(ma_splitter_node_init(graph, &splitterConfig, nullptr, &voice->Splitter));

		CHECK_MA(ma_node_attach_output_bus(&voice->Sound, 0, &voice->LowPass, 0));
		CHECK_MA(ma_node_attach_output_bus(&voice->LowPass, 0, &voice->Splitter, 0));
		CHECK_MA(ma_node_attach_output_bus(&voice->Splitter, 0, &buses[i % kBusCount], 0));
		CHECK_MA(ma_node_attach_output_bus(&voice->Splitter, 1, &reverb, 0));
		CHECK_MA(ma_node_set_output_bus_volume(&voice->Splitter, 1, 0.3f));

		voice->Radius = 2.0f + static_cast<float>(i % 16) * 2.0f;
		voice->Phase = static_cast<float>(i) * 0.7f;
		voice->Speed = 0.2f + static_cast<float>(i % 5) * 0.1f;
		CHECK_MA(ma_sound_start(&voice->Sound));
		voices.push_back(std::move(voice));
	}
	const double loadMs = Microseconds(Clock::now() - loadStart) / 1000.0;

	std::vector<double> renderUs, updateUs;
	renderUs.reserve(blocks);
	updateUs.reserve(blocks);
	std::vector<float> output(kPeriod * 2);
	for (uint32_t block = 0; block < kWarmupBlocks + blocks; ++block)
	{
		// Main-thread work per frame: move every voice, turn the listener, retune some filters.
		const auto updateStart = Clock::now();
		const float time = static_cast<float>(block) * kPeriod / kRate;
		for (uint32_t i = 0; i < voiceCount; ++i)
		{
			Voice& voice = *voices[i];
			const float angle = voice.Phase + voice.Speed * time;
			ma_sound_set_position(&voice.Sound, voice.Radius * std::cos(angle), 1.0f, voice.Radius * std::sin(angle));
			ma_sound_set_velocity(&voice.Sound, -voice.Radius * voice.Speed * std::sin(angle), 0.0f, voice.Radius * voice.Speed * std::cos(angle));
			if ((block + i) % 10 == 0)
			{
				const ma_lpf_config retune = ma_lpf_config_init(ma_format_f32, 2, kRate, 500.0 + 400.0 * (1.0 + std::sin(time + i)), 2);
				CHECK_MA(ma_lpf_node_reinit(&retune, &voice.LowPass));
			}
		}
		ma_engine_listener_set_direction(&engine, 0, std::sin(time * 0.3f), 0.0f, -std::cos(time * 0.3f));
		const auto renderStart = Clock::now();

		// The device thread's work for one period.
		ma_uint64 read = 0;
		CHECK_MA(ma_engine_read_pcm_frames(&engine, output.data(), kPeriod, &read));
		CHECK(read == kPeriod);
		const auto renderEnd = Clock::now();
		if (block >= kWarmupBlocks)
		{
			updateUs.push_back(Microseconds(renderStart - updateStart));
			renderUs.push_back(Microseconds(renderEnd - renderStart));
		}
	}

	const auto summarize = [](std::vector<double> values, const char* label)
	{
		std::sort(values.begin(), values.end());
		double total = 0.0;
		for (const double value : values)
			total += value;
		const double mean = total / static_cast<double>(values.size());
		const double p99 = values[values.size() * 99 / 100];
		std::cout << "  " << label << ": mean " << mean << " us, p50 " << values[values.size() / 2] << " us, p99 " << p99
			<< " us, max " << values.back() << " us\n";
		return mean;
	};

	std::cout << "miniaudio " << ma_version_string() << ", " << voiceCount << " voices (" << kStreamedVoices << " streamed), "
		<< kBusCount << " buses + reverb, " << kRate << " Hz stereo, " << kPeriod << "-frame periods, " << blocks << " periods\n";
	std::cout << "  engine init (no device): " << initUs << " us; voice load: " << loadMs << " ms\n";
	const double renderMean = summarize(renderUs, "render per period (device thread)");
	summarize(updateUs, "update per period (main thread)");
	std::cout << "  device-thread load: " << 100.0 * renderMean / (1.0e6 * kPeriod / kRate) << " % of one core\n";

	for (auto& voice : voices)
	{
		ma_sound_uninit(&voice->Sound);
		ma_splitter_node_uninit(&voice->Splitter, nullptr);
		ma_lpf_node_uninit(&voice->LowPass, nullptr);
	}
	ma_reverb_node_uninit(&reverb, nullptr);
	for (auto& bus : buses)
		ma_sound_group_uninit(&bus);
	ma_engine_uninit(&engine);
	return EXIT_SUCCESS;
}
