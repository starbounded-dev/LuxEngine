// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// Phase 0 checks for the native audio backend (docs/AUDIO_NATIVE_BACKEND_PLAN.md). Everything renders
// offline through ma_engine with noDevice, so the results are deterministic and need no sound card.

#include "NativeAudioTestUtils.h"

#include <extras/nodes/ma_reverb_node/ma_reverb_node.h>

#include <algorithm>
#include <array>
#include <numbers>
#include <string>

using namespace NativeAudioTest;

namespace
{
	constexpr uint32_t kRate = 48000;
	constexpr uint64_t kToneFrames = kRate / 2;
	constexpr double kLeftHz = 440.0, kLeftAmplitude = 0.5;
	constexpr double kRightHz = 660.0, kRightAmplitude = 0.25;
	constexpr std::array<const char*, 4> kFormats = { "tone.wav", "tone.flac", "tone.ogg", "tone.mp3" };

	int16_t ToneSample(double hz, double amplitude, uint64_t frame)
	{
		return static_cast<int16_t>(std::lround(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(frame) / kRate) * 32767.0));
	}

	float MaxDifference(const Pcm& a, const Pcm& b, uint64_t frames)
	{
		float worst = 0.0f;
		for (uint64_t i = 0; i < frames * a.Channels; ++i)
			worst = std::max(worst, std::abs(a.Samples[i] - b.Samples[i]));
		return worst;
	}

	// Signal-to-noise ratio of a lossy decode against the reference, after finding the encoder delay.
	// The two tones repeat together every 2400 frames (11 cycles of 220 Hz at 48 kHz), so searching
	// half of that either side finds the one lag that lines up both channels.
	double AlignedSnrDb(const Pcm& reference, const Pcm& decoded)
	{
		constexpr int64_t kMaxLag = 1200;
		constexpr uint64_t kWindow = 8192, kStart = 4096;
		int64_t bestLag = 0;
		double bestCorrelation = -1.0e30;
		for (int64_t lag = -kMaxLag; lag < kMaxLag; ++lag)
		{
			double correlation = 0.0;
			for (uint64_t frame = kStart; frame < kStart + kWindow; ++frame)
			{
				const int64_t other = static_cast<int64_t>(frame) + lag;
				if (other < 0 || static_cast<uint64_t>(other) >= decoded.FrameCount())
					continue;
				for (uint32_t channel = 0; channel < reference.Channels; ++channel)
					correlation += static_cast<double>(reference.At(frame, channel)) * decoded.At(static_cast<uint64_t>(other), channel);
			}
			if (correlation > bestCorrelation)
			{
				bestCorrelation = correlation;
				bestLag = lag;
			}
		}

		double signal = 0.0, noise = 0.0;
		for (uint64_t frame = kStart; frame + kStart < reference.FrameCount(); ++frame)
		{
			const int64_t other = static_cast<int64_t>(frame) + bestLag;
			if (other < 0 || static_cast<uint64_t>(other) >= decoded.FrameCount())
				continue;
			for (uint32_t channel = 0; channel < reference.Channels; ++channel)
			{
				const double expected = reference.At(frame, channel);
				const double error = decoded.At(static_cast<uint64_t>(other), channel) - expected;
				signal += expected * expected;
				noise += error * error;
			}
		}
		return 10.0 * std::log10(signal / std::max(noise, 1.0e-30));
	}

	double ZeroCrossingHz(const Pcm& pcm, uint32_t channel, uint64_t first, uint64_t count)
	{
		uint64_t crossings = 0;
		for (uint64_t frame = first + 1; frame < first + count; ++frame)
		{
			if ((pcm.At(frame - 1, channel) < 0.0f) != (pcm.At(frame, channel) < 0.0f))
				++crossings;
		}
		return static_cast<double>(crossings) * 0.5 * pcm.SampleRate / static_cast<double>(count);
	}

	uint64_t FirstNonSilentFrame(const Pcm& pcm)
	{
		for (uint64_t frame = 0; frame < pcm.FrameCount(); ++frame)
		{
			for (uint32_t channel = 0; channel < pcm.Channels; ++channel)
			{
				if (pcm.At(frame, channel) != 0.0f)
					return frame;
			}
		}
		return pcm.FrameCount();
	}

	uint64_t LastNonSilentFrame(const Pcm& pcm)
	{
		for (uint64_t frame = pcm.FrameCount(); frame-- > 0;)
		{
			for (uint32_t channel = 0; channel < pcm.Channels; ++channel)
			{
				if (std::abs(pcm.At(frame, channel)) > 1.0e-4f)
					return frame;
			}
		}
		return 0;
	}

	void WavDecodesToTheAuthoredSamples()
	{
		const Pcm pcm = DecodeFile(FixtureDirectory() / "tone.wav");
		CHECK(pcm.Channels == 2 && pcm.SampleRate == kRate && pcm.FrameCount() == kToneFrames);
		for (uint64_t frame = 0; frame < kToneFrames; ++frame)
		{
			CHECK(pcm.At(frame, 0) == ToneSample(kLeftHz, kLeftAmplitude, frame) / 32768.0f);
			CHECK(pcm.At(frame, 1) == ToneSample(kRightHz, kRightAmplitude, frame) / 32768.0f);
		}
	}

	void FlacIsLossless()
	{
		const Pcm wav = DecodeFile(FixtureDirectory() / "tone.wav");
		const Pcm flac = DecodeFile(FixtureDirectory() / "tone.flac");
		CHECK(flac.Channels == 2 && flac.SampleRate == kRate && flac.FrameCount() == kToneFrames);
		CHECK(MaxDifference(wav, flac, kToneFrames) == 0.0f);
	}

	void LossyFormatsStayCloseToTheSource()
	{
		const Pcm wav = DecodeFile(FixtureDirectory() / "tone.wav");
		for (const char* name : { "tone.ogg", "tone.mp3" })
		{
			const Pcm decoded = DecodeFile(FixtureDirectory() / name);
			CHECK(decoded.Channels == 2 && decoded.SampleRate == kRate);
			// Encoders add priming and padding; the length may drift by a few frames of that.
			CHECK(decoded.FrameCount() + 4096 > kToneFrames && decoded.FrameCount() < kToneFrames + 4096);
			const double snr = AlignedSnrDb(wav, decoded);
			std::cout << "  " << name << ": " << decoded.FrameCount() << " frames, SNR " << snr << " dB\n";
			CHECK(snr > 25.0);
		}
	}

	// A plain, non-spatialized sound must reach the output exactly as the decoder produces it. Pitch is
	// disabled: miniaudio runs its linear resampler whenever pitch is enabled, even at pitch 1, which
	// PitchedPlaybackAtUnityIsOneFrameLate covers separately.
	void EnginePlaybackMatchesTheDecoder(ma_uint32 flags, const char* label)
	{
		for (const char* name : kFormats)
		{
			const auto path = FixtureDirectory() / name;
			const Pcm decoded = DecodeFile(path);
			const ma_engine_config config = OfflineEngineConfig(2, kRate);
			ma_engine engine;
			CHECK_MA(ma_engine_init(&config, &engine));
			ma_sound sound;
			CHECK_MA(ma_sound_init_from_file(&engine, path.string().c_str(), flags | MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH,
				nullptr, nullptr, &sound));
			CHECK_MA(ma_sound_start(&sound));
			const Pcm rendered = Render(engine, decoded.FrameCount() + 1024);
			const float difference = MaxDifference(decoded, rendered, decoded.FrameCount());
			std::cout << "  " << label << ' ' << name << ": max difference " << difference << '\n';
			CHECK(difference <= 1.0e-6f);
			CHECK(FirstNonSilentFrame(rendered) < 64);
			CHECK(LastNonSilentFrame(rendered) < decoded.FrameCount());
			CHECK(ma_sound_at_end(&sound));
			ma_sound_uninit(&sound);
			ma_engine_uninit(&engine);
		}
	}

	// With pitch enabled the linear resampler adds exactly one frame of latency at pitch 1, passes the
	// samples through unchanged, and drops the sound's final frame when it ends. Phase 2 accounts for
	// the latency when aligning voices; the dropped frame is inaudible.
	void PitchedPlaybackAtUnityIsOneFrameLate()
	{
		const auto path = FixtureDirectory() / "tone.wav";
		const Pcm decoded = DecodeFile(path);
		const ma_engine_config config = OfflineEngineConfig(2, kRate);
		ma_engine engine;
		CHECK_MA(ma_engine_init(&config, &engine));
		ma_sound sound;
		CHECK_MA(ma_sound_init_from_file(&engine, path.string().c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, nullptr, &sound));
		CHECK_MA(ma_sound_start(&sound));
		const Pcm rendered = Render(engine, decoded.FrameCount() + 64);
		CHECK(rendered.At(0, 0) == 0.0f && rendered.At(0, 1) == 0.0f);
		float worst = 0.0f;
		for (uint64_t frame = 0; frame + 1 < decoded.FrameCount(); ++frame)
		{
			for (uint32_t channel = 0; channel < 2; ++channel)
				worst = std::max(worst, std::abs(rendered.At(frame + 1, channel) - decoded.At(frame, channel)));
		}
		std::cout << "  pitch-enabled path at pitch 1: one frame late, max difference " << worst
			<< ", last source frame " << (LastNonSilentFrame(rendered) == decoded.FrameCount() - 1 ? "dropped" : "kept") << '\n';
		CHECK(worst <= 1.0e-6f);
		CHECK(LastNonSilentFrame(rendered) <= decoded.FrameCount());
		ma_sound_uninit(&sound);
		ma_engine_uninit(&engine);
	}

	void PlaybackResamplesToTheEngineRate()
	{
		constexpr uint32_t kEngineRate = 44100;
		const ma_engine_config config = OfflineEngineConfig(2, kEngineRate);
		ma_engine engine;
		CHECK_MA(ma_engine_init(&config, &engine));
		ma_sound sound;
		CHECK_MA(ma_sound_init_from_file(&engine, (FixtureDirectory() / "tone.wav").string().c_str(),
			MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, nullptr, &sound));
		CHECK_MA(ma_sound_start(&sound));
		const Pcm rendered = Render(engine, kEngineRate);
		const uint64_t length = LastNonSilentFrame(rendered) + 1;
		const double hz = ZeroCrossingHz(rendered, 0, 2048, 16384);
		std::cout << "  44.1 kHz engine: " << length << " frames, left " << hz << " Hz\n";
		CHECK(length > 22050 - 64 && length < 22050 + 64);
		CHECK(std::abs(hz - kLeftHz) < 2.0);
		ma_sound_uninit(&sound);
		ma_engine_uninit(&engine);
	}

	struct SineVoice
	{
		ma_waveform Waveform;
		ma_sound Sound;

		SineVoice(ma_engine& engine, double hz, ma_uint32 flags)
		{
			const ma_waveform_config config = ma_waveform_config_init(ma_format_f32, 1, ma_engine_get_sample_rate(&engine), ma_waveform_type_sine, 0.5, hz);
			CHECK_MA(ma_waveform_init(&config, &Waveform));
			CHECK_MA(ma_sound_init_from_data_source(&engine, &Waveform, flags, nullptr, &Sound));
		}

		~SineVoice()
		{
			ma_sound_uninit(&Sound);
			ma_waveform_uninit(&Waveform);
		}
	};

	// Lux's listener convention is -Z forward, +Y up, +X right, the same as miniaudio's defaults.
	void SpatializerPansAndAttenuatesLikeTheEngineConvention()
	{
		const auto measure = [](float x, float z, double& left, double& right)
		{
			const ma_engine_config config = OfflineEngineConfig(2, kRate);
			ma_engine engine;
			CHECK_MA(ma_engine_init(&config, &engine));
			{
				SineVoice voice(engine, 440.0, 0);
				ma_sound_set_position(&voice.Sound, x, 0.0f, z);
				CHECK_MA(ma_sound_start(&voice.Sound));
				const Pcm rendered = Render(engine, kRate / 2);
				left = Rms(rendered, 0, kRate / 4, kRate / 4);
				right = Rms(rendered, 1, kRate / 4, kRate / 4);
			}
			ma_engine_uninit(&engine);
		};

		double left = 0, right = 0;
		measure(3.0f, 0.0f, left, right);
		std::cout << "  source at +X: L " << left << " R " << right << '\n';
		CHECK(right > 1.5 * left);
		measure(-3.0f, 0.0f, left, right);
		std::cout << "  source at -X: L " << left << " R " << right << '\n';
		CHECK(left > 1.5 * right);

		double nearLeft = 0, nearRight = 0, farLeft = 0, farRight = 0;
		measure(0.0f, -1.0f, nearLeft, nearRight);
		measure(0.0f, -4.0f, farLeft, farRight);
		// The default inverse model with min distance 1 and rolloff 1 gives 1 / (1 + (4 - 1)).
		const double ratio = (farLeft + farRight) / (nearLeft + nearRight);
		std::cout << "  distance 4 vs 1: gain ratio " << ratio << '\n';
		CHECK(std::abs(nearLeft - nearRight) < 1.0e-3 * nearLeft);
		CHECK(std::abs(ratio - 0.25) < 0.01);
	}

	double FilteredGainDb(double hz)
	{
		const ma_engine_config config = OfflineEngineConfig(1, kRate);
		ma_engine engine;
		CHECK_MA(ma_engine_init(&config, &engine));
		double gainDb = 0.0;
		{
			SineVoice voice(engine, hz, MA_SOUND_FLAG_NO_SPATIALIZATION);
			const ma_lpf_node_config lpfConfig = ma_lpf_node_config_init(1, kRate, 1000.0, 4);
			ma_lpf_node lpf;
			CHECK_MA(ma_lpf_node_init(ma_engine_get_node_graph(&engine), &lpfConfig, nullptr, &lpf));
			CHECK_MA(ma_node_attach_output_bus(&voice.Sound, 0, &lpf, 0));
			CHECK_MA(ma_node_attach_output_bus(&lpf, 0, ma_engine_get_endpoint(&engine), 0));
			CHECK_MA(ma_sound_start(&voice.Sound));
			const Pcm rendered = Render(engine, kRate / 2);
			gainDb = 20.0 * std::log10(Rms(rendered, 0, kRate / 4, kRate / 4) / (0.5 / std::numbers::sqrt2));
			ma_node_detach_output_bus(&voice.Sound, 0);
			ma_lpf_node_uninit(&lpf, nullptr);
		}
		ma_engine_uninit(&engine);
		return gainDb;
	}

	// Occlusion in Phase 2 is a per-voice low-pass; it must pass lows and cut highs.
	void LowPassNodeFilters()
	{
		const double low = FilteredGainDb(200.0);
		const double high = FilteredGainDb(8000.0);
		std::cout << "  1 kHz order-4 low-pass: 200 Hz " << low << " dB, 8 kHz " << high << " dB\n";
		CHECK(low > -1.0 && low < 0.5);
		CHECK(high < -30.0);
	}

	void ReverbNodeLeavesADecayingTail()
	{
		const ma_engine_config config = OfflineEngineConfig(2, kRate);
		ma_engine engine;
		CHECK_MA(ma_engine_init(&config, &engine));
		{
			// A 10 ms burst; anything after it comes from the reverb.
			std::vector<float> burst(kRate / 100, 0.0f);
			for (size_t i = 0; i < burst.size(); i += 2)
				burst[i] = 0.5f;
			const ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(ma_format_f32, 1, burst.size(), burst.data(), nullptr);
			ma_audio_buffer buffer;
			CHECK_MA(ma_audio_buffer_init(&bufferConfig, &buffer));
			ma_sound sound;
			CHECK_MA(ma_sound_init_from_data_source(&engine, &buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &sound));
			const ma_reverb_node_config reverbConfig = ma_reverb_node_config_init(2, kRate);
			ma_reverb_node reverb;
			CHECK_MA(ma_reverb_node_init(ma_engine_get_node_graph(&engine), &reverbConfig, nullptr, &reverb));
			CHECK_MA(ma_node_attach_output_bus(&sound, 0, &reverb, 0));
			CHECK_MA(ma_node_attach_output_bus(&reverb, 0, ma_engine_get_endpoint(&engine), 0));
			CHECK_MA(ma_sound_start(&sound));
			const Pcm rendered = Render(engine, 2 * kRate);
			CHECK(std::all_of(rendered.Samples.begin(), rendered.Samples.end(), [](float value) { return std::isfinite(value); }));
			const double early = Rms(rendered, 0, kRate / 20, kRate / 4);
			const double late = Rms(rendered, 0, 3 * kRate / 2, kRate / 2);
			std::cout << "  reverb tail: 50-300 ms RMS " << early << ", 1.5-2 s RMS " << late << '\n';
			CHECK(early > 1.0e-3);
			CHECK(late < 0.5 * early);
			ma_node_detach_output_bus(&sound, 0);
			ma_reverb_node_uninit(&reverb, nullptr);
			ma_sound_uninit(&sound);
			ma_audio_buffer_uninit(&buffer);
		}
		ma_engine_uninit(&engine);
	}

	// Phase 6 relies on starting a bed on an exact engine frame, even mid-block. Music beds need no
	// pitch, so they skip the resampler's one-frame latency.
	void ScheduledStartIsSampleAccurate()
	{
		const ma_engine_config config = OfflineEngineConfig(2, kRate);
		ma_engine engine;
		CHECK_MA(ma_engine_init(&config, &engine));
		{
			std::vector<float> ones(kRate, 1.0f);
			const ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(ma_format_f32, 1, ones.size(), ones.data(), nullptr);
			ma_audio_buffer buffer;
			CHECK_MA(ma_audio_buffer_init(&bufferConfig, &buffer));
			ma_sound sound;
			CHECK_MA(ma_sound_init_from_data_source(&engine, &buffer, MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH, nullptr, &sound));

			Render(engine, 300); // Move the clock off zero.
			constexpr uint64_t kOffset = 777;
			const uint64_t start = ma_engine_get_time_in_pcm_frames(&engine) + kOffset;
			ma_sound_set_start_time_in_pcm_frames(&sound, start);
			CHECK_MA(ma_sound_start(&sound));
			Pcm rendered;
			rendered.Channels = 2;
			rendered.SampleRate = kRate;
			for (int block = 0; block < 8; ++block)
			{
				const Pcm part = Render(engine, 256);
				rendered.Samples.insert(rendered.Samples.end(), part.Samples.begin(), part.Samples.end());
			}
			const uint64_t first = FirstNonSilentFrame(rendered);
			std::cout << "  scheduled start: first sample at offset " << first << " (expected " << kOffset << ")\n";
			CHECK(first == kOffset);
			CHECK(rendered.At(kOffset, 1) != 0.0f);
			ma_sound_uninit(&sound);
			ma_audio_buffer_uninit(&buffer);
		}
		ma_engine_uninit(&engine);
	}
}

int main()
{
	std::cout << "miniaudio " << ma_version_string() << '\n';
	WavDecodesToTheAuthoredSamples();
	std::cout << "WAV decodes to the authored samples\n";
	FlacIsLossless();
	std::cout << "FLAC is lossless\n";
	LossyFormatsStayCloseToTheSource();
	std::cout << "Lossy formats stay close to the source\n";
	EnginePlaybackMatchesTheDecoder(MA_SOUND_FLAG_DECODE, "decoded");
	EnginePlaybackMatchesTheDecoder(MA_SOUND_FLAG_STREAM, "streamed");
	std::cout << "Engine playback matches the decoder (decoded and streamed)\n";
	PitchedPlaybackAtUnityIsOneFrameLate();
	std::cout << "Pitched playback at unity is one frame late\n";
	PlaybackResamplesToTheEngineRate();
	std::cout << "Playback resamples to the engine rate\n";
	SpatializerPansAndAttenuatesLikeTheEngineConvention();
	std::cout << "Spatializer pans and attenuates\n";
	LowPassNodeFilters();
	std::cout << "Low-pass node filters\n";
	ReverbNodeLeavesADecayingTail();
	std::cout << "Reverb node leaves a decaying tail\n";
	ScheduledStartIsSampleAccurate();
	std::cout << "Scheduled start is sample-accurate\n";
	std::cout << "All native audio checks passed\n";
	return EXIT_SUCCESS;
}
