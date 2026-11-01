// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// The native audio backend (Core/Source/Lux/Audio/Native) through the engine's own API: AudioEngine,
// AudioEventInstance and AudioListener, rendered offline with no audio device and measured on the
// PCM it produces. Only the host is replaced: the engine config (no device) and Sound lookup (an
// in-memory table instead of a project's asset manager).

#include "AudioTestLog.h"

#include "Lux/Audio/AudioAccessibility.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Audio/AudioEventInstance.h"
#include "Lux/Audio/AudioEventRefSerializer.h"
#include "Lux/Audio/AudioFileUtils.h"
#include "Lux/Audio/AudioListener.h"
#include "Lux/Audio/AudioPerformance.h"
#include "Lux/Audio/Sound.h"
#include "Lux/Audio/Native/NativeInternal.h"
#include "Lux/Audio/Native/Miniaudio.h"
#include "Lux/Serialization/Yaml.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <numbers>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); std::exit(EXIT_FAILURE); } } while (0)
#define CHECK_NEAR(value, expected, tolerance) do { const double v_ = (value), e_ = (expected); if (!(std::abs(v_ - e_) <= (tolerance))) { \
	std::fprintf(stderr, "FAIL %s:%d: %s = %.6f, expected %.6f +- %.6f\n", __FILE__, __LINE__, #value, v_, e_, static_cast<double>(tolerance)); std::exit(EXIT_FAILURE); } } while (0)

namespace
{
	constexpr uint32_t kRate = 48000;
	constexpr uint64_t kBlock = 480;      // render in 10 ms periods, as a device would
	constexpr uint64_t kSettle = 4800;    // past miniaudio's 8 ms pan smoothing and one gain ramp
	constexpr uint64_t kMeasure = 9600;   // whole periods of every tone used below

	std::filesystem::path s_Directory;
	Lux::AudioPerformanceSettings s_Performance;

	struct Entry
	{
		Lux::Ref<Lux::Sound> Asset;
		std::filesystem::path Clip;
	};
	std::map<uint64_t, Entry> s_Sounds;
	uint64_t s_NextHandle = 1000;
}

// The host: no device, and Sounds from the table above.
namespace Lux
{
	NativeInternal::EngineConfig NativeInternal::GetEngineConfig()
	{
		EngineConfig config;
		config.NoDevice = true;
		config.Channels = 2;
		config.SampleRate = kRate;
		config.Performance = s_Performance;
		return config;
	}

	bool NativeInternal::ResolveSound(const std::string& reference, ResolvedSound& outSound, std::string& outError)
	{
		uint64_t handle = 0;
		const auto it = ParseHandle(reference, handle) ? s_Sounds.find(handle) : s_Sounds.end();
		if (it == s_Sounds.end())
		{
			outError = "not in the test table";
			return false;
		}
		outSound.Asset = it->second.Asset;
		outSound.Clip = it->second.Clip;
		return true;
	}

	void AudioAccessibility::Track(AudioEventInstance*) {}
	void AudioAccessibility::ReleaseMixer() {}
}

namespace
{
	// A mono 16-bit WAV, so the input level is known exactly.
	std::filesystem::path WriteTone(const std::string& name, double hz, double seconds, double amplitude = 0.5)
	{
		const auto path = s_Directory / name;
		const uint32_t frames = static_cast<uint32_t>(seconds * kRate);
		std::ofstream out(path, std::ios::binary);
		const auto u32 = [&](uint32_t value) { out.write(reinterpret_cast<const char*>(&value), 4); };
		const auto u16 = [&](uint16_t value) { out.write(reinterpret_cast<const char*>(&value), 2); };
		out.write("RIFF", 4); u32(36 + frames * 2); out.write("WAVEfmt ", 8);
		u32(16); u16(1); u16(1); u32(kRate); u32(kRate * 2); u16(2); u16(16);
		out.write("data", 4); u32(frames * 2);
		for (uint32_t i = 0; i < frames; ++i)
		{
			const auto sample = static_cast<int16_t>(std::lround(amplitude * std::sin(2.0 * std::numbers::pi * hz * i / kRate) * 32767.0));
			out.write(reinterpret_cast<const char*>(&sample), 2);
		}
		CHECK(out.good());
		return path;
	}

	std::string AddSound(const std::filesystem::path& clip, const std::function<void(Sound&)>& configure = {})
	{
		auto sound = Ref<Sound>::Create();
		sound->Handle = s_NextHandle++;
		sound->Clip = 1; // any audio asset; the table supplies the file
		if (configure)
			configure(*sound);
		CHECK(sound->Validate());
		s_Sounds[static_cast<uint64_t>(sound->Handle)] = { sound, clip };
		return std::to_string(static_cast<uint64_t>(sound->Handle));
	}

	std::vector<float> Render(uint64_t frames)
	{
		ma_engine* engine = NativeInternal::GetEngine();
		CHECK(engine);
		std::vector<float> pcm(frames * 2);
		for (uint64_t done = 0; done < frames;)
		{
			const uint64_t count = std::min(kBlock, frames - done);
			ma_uint64 read = 0;
			CHECK(ma_engine_read_pcm_frames(engine, pcm.data() + done * 2, count, &read) == MA_SUCCESS);
			CHECK(read == count);
			done += count;
		}
		return pcm;
	}

	double Rms(const std::vector<float>& pcm, int channel)
	{
		double sum = 0.0;
		for (size_t frame = 0; frame < pcm.size() / 2; ++frame)
			sum += static_cast<double>(pcm[frame * 2 + channel]) * pcm[frame * 2 + channel];
		return std::sqrt(sum / static_cast<double>(pcm.size() / 2));
	}

	struct Level
	{
		double Left = 0.0, Right = 0.0;
	};
	Level Measure()
	{
		Render(kSettle);
		const auto pcm = Render(kMeasure);
		return { Rms(pcm, 0), Rms(pcm, 1) };
	}

	AudioListener::States OneListener(const glm::vec3& position = glm::vec3(0.0f))
	{
		AudioListener::States listeners{};
		listeners[0].Weight = 1.0f;
		listeners[0].Position = position;
		return listeners;
	}

	void Start()
	{
		AudioEngine::Init();
		CHECK(AudioEngine::HasInitializedEngine());
		AudioListener::Apply(OneListener());
	}

	Ref<AudioEventInstance> Play(const std::string& reference, const glm::vec3& position)
	{
		auto instance = AudioEventInstance::Create(reference);
		CHECK(instance);
		instance->Set3DAttributes(position, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		CHECK(instance->Start());
		return instance;
	}

	// Plays a looping 3D tone at a position, with optional extra setup, and measures it.
	Level PlayAt(const std::string& reference, const glm::vec3& position, const std::function<void(AudioEventInstance&)>& setup = {})
	{
		auto instance = Play(reference, position);
		if (setup)
			setup(*instance);
		const Level level = Measure();
		instance->Stop(false);
		return level;
	}

	void TestReferences()
	{
		AudioEventRef none;
		CHECK(!none.IsValid() && !none.HasReference() && none.GetReference().empty());
		AudioEventRef studio{ "{12345678-1234-1234-1234-123456789abc}", "event:/Door", "Master.bank" };
		CHECK(!studio.IsValid() && studio.HasReference() && studio.GetReference().empty());
		AudioEventRef both = studio;
		both.Sound = 42;
		CHECK(both.IsValid() && both.GetReference() == "42" && !both.SameAssignment(studio));
		CHECK(AudioEventRef::FromReference("42").Sound == 42 && AudioEventRef::FromReference("42").Guid.empty());
		for (const char* bad : { "", "0", "-4", "+4", "4x", " 4", "event:/Door", "{12345678-1234-1234-1234-123456789abc}", "99999999999999999999999" })
			CHECK(AudioEventRef::FromReference(bad).Sound == 0);
		uint64_t handle = 0;
		CHECK(NativeInternal::ParseHandle("18446744073709551615", handle) && handle == UINT64_MAX);
		CHECK(AudioEngine::GetBackend() == AudioBackend::Native);
		std::puts("PASS: references resolve to the Sound handle; FMOD GUIDs are kept but not played");
	}

	void TestSerialization()
	{
		// Without a Sound the output is exactly what the per-file writers produced before.
		AudioEventRef studio{ "{12345678-1234-1234-1234-123456789abc}", "event:/Door", "Master.bank" };
		{
			Yaml::Writer helper, manual;
			helper << Yaml::BeginMap;
			AudioEventRefSerializer::Write(helper, "Event", studio);
			AudioEventRefSerializer::WriteFlat(helper, "Event", studio);
			helper << Yaml::EndMap;
			manual << Yaml::BeginMap << Yaml::Key << "Event" << Yaml::BeginMap;
			manual << Yaml::Key << "Guid" << Yaml::Value << studio.Guid;
			manual << Yaml::Key << "Path" << Yaml::Value << studio.Path;
			manual << Yaml::Key << "BankName" << Yaml::Value << studio.BankName << Yaml::EndMap;
			manual << Yaml::Key << "EventGuid" << Yaml::Value << studio.Guid;
			manual << Yaml::Key << "EventPath" << Yaml::Value << studio.Path;
			manual << Yaml::Key << "EventBank" << Yaml::Value << studio.BankName;
			manual << Yaml::EndMap;
			CHECK(std::string(helper.c_str()) == manual.c_str());
		}
		// Both references survive both forms, and a file without the key reads Sound = 0.
		AudioEventRef both = studio;
		both.Sound = 0xABCDEF0123ull;
		Yaml::Writer out;
		out << Yaml::BeginMap;
		AudioEventRefSerializer::Write(out, "Event", both);
		AudioEventRefSerializer::WriteFlat(out, "Event", both);
		out << Yaml::EndMap;
		const auto root = Yaml::Load(out.c_str());
		for (const auto& read : { AudioEventRefSerializer::Read(root["Event"]), AudioEventRefSerializer::ReadFlat(root, "Event") })
			CHECK(read.Guid == both.Guid && read.Path == both.Path && read.BankName == both.BankName && read.Sound == both.Sound);
		const auto missing = AudioEventRefSerializer::Read(root["Missing"]);
		CHECK(!missing.HasReference() && missing.Path.empty());
		const auto old = Yaml::Load("Event: { Guid: '{1}', Path: event:/A, BankName: B }");
		CHECK(AudioEventRefSerializer::Read(old["Event"]).Sound == 0 && AudioEventRefSerializer::Read(old["Event"]).Guid == "{1}");

		Sound sound;
		sound.Clip = 77;
		sound.Bus = "bus:/SFX";
		sound.Volume = 0.5f;
		sound.Pitch = 1.25f;
		sound.Loop = true;
		sound.Spatial = { false, SoundAttenuation::Linear, 2.0f, 30.0f, 0.5f };
		Sound restored;
		CHECK(restored.FromYAML(sound.ToYAML()));
		CHECK(restored.Clip == 77 && restored.Bus == "bus:/SFX" && restored.Volume == 0.5f && restored.Pitch == 1.25f && restored.Loop
			&& !restored.Spatial.Enabled && restored.Spatial.Attenuation == SoundAttenuation::Linear && restored.Spatial.MinDistance == 2.0f
			&& restored.Spatial.MaxDistance == 30.0f && restored.Spatial.Rolloff == 0.5f);
		Sound defaults;
		CHECK(defaults.FromYAML("Sound: { Version: 1 }") && defaults.Bus == "bus:/" && defaults.Spatial.Enabled && defaults.Clip == 0);
		for (const char* bad : { "Sound: { Pitch: 0 }", "Sound: { Volume: 5 }", "Sound: { Bus: SFX }", "Sound: { Version: 2 }",
			"Sound: { Spatial: { MinDistance: 10, MaxDistance: 5 } }", "Sound: { Spatial: { Attenuation: Cubic } }", "Other: {}", "Sound: [1]" })
		{
			Sound target = restored;
			CHECK(!target.FromYAML(bad));
			CHECK(target.Clip == 77 && target.Bus == "bus:/SFX"); // a failed load changes nothing
		}
		std::puts("PASS: Sound YAML and both AudioEventRef forms roundtrip; output without a Sound is unchanged");
	}

	void TestCurves()
	{
		SoundSpatialSettings inverse{ true, SoundAttenuation::Inverse, 1.0f, 50.0f, 1.0f };
		CHECK_NEAR(NativeInternal::Attenuate(inverse, 0.5f), 1.0, 1e-7);
		CHECK_NEAR(NativeInternal::Attenuate(inverse, 4.0f), 0.25, 1e-7);
		CHECK_NEAR(NativeInternal::Attenuate(inverse, 1000.0f), 1.0 / 50.0, 1e-7); // holds past the maximum
		SoundSpatialSettings linear{ true, SoundAttenuation::Linear, 1.0f, 11.0f, 1.0f };
		CHECK_NEAR(NativeInternal::Attenuate(linear, 6.0f), 0.5, 1e-7);
		CHECK_NEAR(NativeInternal::Attenuate(linear, 20.0f), 0.0, 1e-7);
		SoundSpatialSettings none{ true, SoundAttenuation::None, 1.0f, 2.0f, 1.0f };
		CHECK_NEAR(NativeInternal::Attenuate(none, 100.0f), 1.0, 0.0);
		CHECK_NEAR(NativeInternal::OcclusionCutoffHz(1.0f), 20000.0, 0.0);
		CHECK_NEAR(NativeInternal::OcclusionCutoffHz(0.5f), 10000.0, 0.0);
		CHECK_NEAR(NativeInternal::OcclusionCutoffHz(0.0f), 125.0, 0.0);
		CHECK_NEAR(NativeInternal::OcclusionCutoffHz(NAN), 20000.0, 0.0);
		std::puts("PASS: attenuation curves and the occlusion cutoff");
	}

	void TestEngine()
	{
		CHECK(!AudioEngine::GetStats().Initialized);
		Start();
		const auto stats = AudioEngine::GetStats();
		CHECK(stats.Initialized && std::string(stats.BackendName) == "miniaudio" && stats.SampleRate == static_cast<int>(kRate) && !stats.HasMixerStats);
		CHECK(AudioEngine::GetLoadedBanks().empty() && AudioEngine::GetEvents().empty());
		AudioEventParameterInfo parameter;
		CHECK(!AudioEngine::GetEventParameter("1", "Speed", parameter));
		CHECK(!AudioEngine::SetBusVolume("bus:/SFX", 0.5f) && !AudioEngine::SetVCAVolume("vca:/Music", 0.5f));
		CHECK(AudioEngine::LoadRuntimeBanks("Assets", {}));
		CHECK(!AudioEngine::LoadBanks("Build/Desktop"));
		CHECK(!AudioEventInstance::Create("event:/Door") && !AudioEventInstance::Create("424242") && !AudioEventInstance::Create(""));
		AudioEngine::Shutdown();
		CHECK(!AudioEngine::HasInitializedEngine() && !NativeInternal::GetEngine());
		std::puts("PASS: engine lifecycle, stats, and FMOD-only calls refused cleanly");
	}

	void TestPanAndAttenuation()
	{
		Start();
		const auto tone = WriteTone("pan.wav", 500.0, 1.0);
		const auto flat = AddSound(tone, [](Sound& s) { s.Loop = true; s.Spatial.Attenuation = SoundAttenuation::None; });
		const Level right = PlayAt(flat, { 2.0f, 0.0f, 0.0f });
		const Level left = PlayAt(flat, { -2.0f, 0.0f, 0.0f });
		const Level front = PlayAt(flat, { 0.0f, 0.0f, -2.0f });
		CHECK(right.Right > 0.05 && right.Right > 2.0 * right.Left);
		CHECK(left.Left > 0.05 && left.Left > 2.0 * left.Right);
		CHECK_NEAR(front.Left, front.Right, 1e-4);
		CHECK_NEAR(right.Right, left.Left, 1e-4); // mirror images
		// miniaudio's pan law: straight ahead, each channel gets half the level of the near channel at
		// the side (its own comment calls out the loss). Pinned so an update that changes it is noticed.
		CHECK_NEAR(front.Left / right.Right, 0.5, 0.01);

		const auto inverse = AddSound(tone, [](Sound& s) { s.Loop = true; });
		const Level oneMetre = PlayAt(inverse, { 0.0f, 0.0f, -1.0f });
		const Level fourMetres = PlayAt(inverse, { 0.0f, 0.0f, -4.0f });
		CHECK_NEAR(fourMetres.Left / oneMetre.Left, 0.25, 0.002);
		const auto linear = AddSound(tone, [](Sound& s) { s.Loop = true; s.Spatial.Attenuation = SoundAttenuation::Linear; s.Spatial.MaxDistance = 11.0f; });
		CHECK_NEAR(PlayAt(linear, { 0.0f, 0.0f, -6.0f }).Left / PlayAt(linear, { 0.0f, 0.0f, -1.0f }).Left, 0.5, 0.002);

		// Panning follows the listener: moving it past the source swaps the sides.
		AudioListener::Apply(OneListener({ 4.0f, 0.0f, 0.0f }));
		const Level moved = PlayAt(flat, { 2.0f, 0.0f, 0.0f });
		CHECK(moved.Left > 2.0 * moved.Right);
		AudioEngine::Shutdown();
		std::puts("PASS: 3D panning from position and listener, inverse and linear distance attenuation");
	}

	void TestListeners()
	{
		Start();
		const auto tone = WriteTone("listeners.wav", 500.0, 1.0);
		const auto sound = AddSound(tone, [](Sound& s) { s.Loop = true; });
		const glm::vec3 source{ 0.0f, 0.0f, -4.0f };
		const double alone = PlayAt(sound, source).Left;

		// An attenuation target measures distance from the target, but pans from the listener.
		auto targeted = OneListener();
		targeted[0].UseAttenuationPosition = true;
		targeted[0].AttenuationPosition = { 0.0f, 0.0f, -3.0f };
		AudioListener::Apply(targeted);
		CHECK_NEAR(PlayAt(sound, source).Left / alone, 4.0, 0.01);

		// Two equal listeners share the mix: 0.5 x gain 1 (1 m) + 0.5 x gain 0.25 (4 m).
		AudioListener::States two = OneListener();
		two[1].Weight = 1.0f;
		two[1].Position = { 0.0f, 0.0f, 3.0f };
		AudioListener::Apply(two);
		const double shared = PlayAt(sound, { 0.0f, 0.0f, -1.0f }).Left;
		AudioListener::Apply(OneListener());
		const double single = PlayAt(sound, { 0.0f, 0.0f, -1.0f }).Left;
		CHECK_NEAR(shared / single, 0.625, 0.005);

		// No active listener: a neutral one at the origin, like the FMOD bridge.
		AudioListener::Apply({});
		CHECK_NEAR(PlayAt(sound, source).Left / alone, 1.0, 1e-4);
		AudioEngine::Shutdown();
		std::puts("PASS: attenuation targets, normalized listener weights, neutral listener when none is active");
	}

	void TestOcclusion()
	{
		Start();
		const auto low = AddSound(WriteTone("low.wav", 100.0, 1.0), [](Sound& s) { s.Loop = true; s.Spatial.Attenuation = SoundAttenuation::None; });
		const auto high = AddSound(WriteTone("high.wav", 10000.0, 1.0), [](Sound& s) { s.Loop = true; s.Spatial.Attenuation = SoundAttenuation::None; });
		const glm::vec3 front{ 0.0f, 0.0f, -2.0f };
		const auto occlude = [](float gain) { return [gain](AudioEventInstance& instance) { instance.SetAcoustics({ gain, 0.0f }); }; };
		const double lowOpen = PlayAt(low, front).Left;
		const double highOpen = PlayAt(high, front).Left;
		// Unoccluded is the bypass: exactly the level without acoustics.
		CHECK_NEAR(PlayAt(low, front, occlude(1.0f)).Left / lowOpen, 1.0, 1e-6);
		CHECK_NEAR(PlayAt(high, front, occlude(1.0f)).Left / highOpen, 1.0, 1e-6);
		// The low band loses exactly the measured gain; the highs lose more through the low-pass
		// (one pole at 20 kHz x 0.25 = 5 kHz: about -6.4 dB more at 10 kHz).
		const double lowRatio = PlayAt(low, front, occlude(0.25f)).Left / lowOpen;
		const double highRatio = PlayAt(high, front, occlude(0.25f)).Left / highOpen;
		CHECK_NEAR(lowRatio, 0.25, 0.003);
		CHECK_NEAR(highRatio, 0.25 * 0.48, 0.01);
		// 2D voices are never occluded.
		const auto flat = AddSound(WriteTone("flat.wav", 100.0, 1.0), [](Sound& s) { s.Loop = true; s.Spatial.Enabled = false; });
		const double flatOpen = PlayAt(flat, front).Left;
		CHECK_NEAR(PlayAt(flat, { 0.0f, 0.0f, -500.0f }, occlude(0.1f)).Left / flatOpen, 1.0, 1e-6);
		AudioEngine::Shutdown();
		std::puts("PASS: occlusion gain and low-pass, exact bypass when unoccluded, 2D voices unaffected");
	}

	void TestVolumeAndBuses()
	{
		s_Performance.MuteWhenUnfocused = true;
		Start();
		const auto tone = WriteTone("volume.wav", 500.0, 1.0);
		const auto sound = AddSound(tone, [](Sound& s) { s.Loop = true; s.Spatial.Enabled = false; });
		const auto quiet = AddSound(tone, [](Sound& s) { s.Loop = true; s.Spatial.Enabled = false; s.Volume = 0.5f; });
		const glm::vec3 front{ 0.0f, 0.0f, -1.0f };
		const double full = PlayAt(sound, front).Left;
		CHECK(full > 0.3);
		CHECK_NEAR(PlayAt(quiet, front).Left / full, 0.5, 1e-4);
		CHECK_NEAR(PlayAt(sound, front, [](AudioEventInstance& instance) { instance.SetVolume(0.5f); }).Left / full, 0.5, 1e-4);
		CHECK(AudioEngine::SetBusVolume("bus:/", 0.5f) && AudioEngine::GetBusVolume("bus:/") == 0.5f);
		CHECK_NEAR(PlayAt(sound, front).Left / full, 0.5, 1e-4);
		CHECK(AudioEngine::SetBusMuted("bus:/", true));
		CHECK(PlayAt(sound, front).Left == 0.0);
		CHECK(AudioEngine::SetBusMuted("bus:/", false) && AudioEngine::SetBusVolume("bus:/", 1.0f));
		AudioEngine::SetApplicationFocused(false);
		CHECK(PlayAt(sound, front).Left == 0.0);
		AudioEngine::SetApplicationFocused(true);
		CHECK_NEAR(PlayAt(sound, front).Left / full, 1.0, 1e-6);
		AudioEngine::Shutdown();
		s_Performance = {};
		std::puts("PASS: Sound and instance volume, master bus volume and mute, focus mute");
	}

	void TestPauseLayering()
	{
		Start();
		const auto sound = AddSound(WriteTone("pause.wav", 500.0, 2.0), [](Sound& s) { s.Spatial.Enabled = false; });
		auto instance = Play(sound, glm::vec3(0.0f));
		CHECK(Rms(Render(kBlock * 4), 0) > 0.1);
		instance->SetScenePaused(true);
		Render(kBlock);
		const int frozen = instance->GetTimelinePosition();
		CHECK(Rms(Render(kBlock * 4), 0) == 0.0 && instance->GetTimelinePosition() == frozen);
		CHECK(instance->IsPaused() && instance->IsPlaying());
		// Script pause holds while the scene resumes, and the reverse.
		instance->SetPaused(true);
		instance->SetScenePaused(false);
		CHECK(instance->IsPaused() && Rms(Render(kBlock * 4), 0) == 0.0);
		instance->SetPaused(false);
		CHECK(!instance->IsPaused() && Rms(Render(kBlock * 4), 0) > 0.1 && instance->GetTimelinePosition() > frozen);
		// Paused before starting: Start waits for the resume.
		auto waiting = AudioEventInstance::Create(sound);
		CHECK(waiting);
		instance->Stop(false);
		waiting->SetScenePaused(true);
		CHECK(waiting->Start() && waiting->IsPlaying() && Rms(Render(kBlock * 2), 0) == 0.0);
		waiting->SetScenePaused(false);
		CHECK(Rms(Render(kBlock * 2), 0) > 0.1);
		AudioEngine::Shutdown();
		std::puts("PASS: scene pause layered over script pause, timeline frozen while paused");
	}

	void TestPlaybackLifecycle()
	{
		Start();
		// 0.1 s one-shot: it ends by itself, reports through the script mailbox, and restarts on Start.
		const auto shot = AddSound(WriteTone("shot.wav", 500.0, 0.1), [](Sound& s) { s.Spatial.Enabled = false; });
		auto instance = AudioEventInstance::Create(shot);
		CHECK(instance && instance->IsOneShot() && !instance->Is3D() && !instance->IsSnapshot() && instance->GetLength() == 100);
		CHECK(instance->SetCallbackHandle(42) && instance->MonitorPlayback());
		CHECK(instance->Start() && instance->IsPlaying());
		auto status = instance->GetPlaybackStatus();
		CHECK(status.Started && status.SoundStarted && !status.Stopped);
		CHECK_NEAR(status.Duration, 0.1, 1e-4);
		Render(kRate / 5);
		CHECK(!instance->IsPlaying());
		CHECK(AudioEventInstance::DrainNotifications().empty()); // published by the engine update, on the main thread
		AudioEngine::Update();
		auto notifications = AudioEventInstance::DrainNotifications();
		CHECK(notifications.size() == 1 && notifications[0].Handle == 42 && notifications[0].Stopped);
		CHECK(instance->GetPlaybackStatus().Stopped && NativeInternal::CountPlayingVoices() == 0);
		CHECK(instance->Start() && Rms(Render(kBlock * 4), 0) > 0.1); // a finished one-shot plays again
		instance->Stop();
		CHECK(!instance->IsPlaying() && Rms(Render(kBlock), 0) == 0.0);
		notifications = AudioEventInstance::DrainNotifications();
		CHECK(notifications.size() == 1 && notifications[0].Handle == 42);

		// A loop keeps playing, and its timeline wraps.
		const auto loop = AddSound(WriteTone("loop.wav", 500.0, 0.5), [](Sound& s) { s.Loop = true; s.Spatial.Enabled = false; });
		auto looping = AudioEventInstance::Create(loop);
		CHECK(looping && !looping->IsOneShot() && looping->GetLength() == 500);
		looping->SetTimelinePosition(250);
		CHECK_NEAR(looping->GetTimelinePosition(), 250, 1);
		looping->SetTimelinePosition(750);
		CHECK_NEAR(looping->GetTimelinePosition(), 250, 1);
		CHECK(looping->Start());
		Render(kRate);
		AudioEngine::Update();
		CHECK(looping->IsPlaying() && AudioEngine::GetStats().PlayingEventInstances == 1);

		// Pitch 2 plays the 0.1 s clip in about 0.05 s.
		const auto fast = AddSound(WriteTone("fast.wav", 500.0, 0.1), [](Sound& s) { s.Spatial.Enabled = false; s.Pitch = 2.0f; });
		auto quick = Play(fast, glm::vec3(0.0f));
		Render(kRate * 6 / 100);
		CHECK(!quick->IsPlaying());

		// What Sounds cannot do yet fails without touching the voice.
		CHECK(!instance->SetParameter("Speed", 1.0f) && !instance->SetSnapshotIntensity(0.5f) && !instance->SetProgrammerSound("key")
			&& !instance->EnableTimelineNotifications() && !instance->IsVirtual());
		CHECK(instance->SetPriority(12) && instance->GetPriority() == 12 && !instance->SetPriority(300));

		// Shutdown frees every voice; wrappers held past it are inert, and a new engine starts clean.
		AudioEngine::Shutdown();
		CHECK(!looping->IsValid() && !looping->IsPlaying() && !looping->Start());
		looping->SetVolume(0.5f);
		looping->Stop();
		Start();
		auto fresh = Play(loop, glm::vec3(0.0f));
		looping = nullptr; // destroying an old wrapper must not touch the new engine's voices
		CHECK(fresh->IsValid() && Rms(Render(kBlock * 4), 0) > 0.1);
		AudioEngine::Shutdown();
		std::puts("PASS: one-shot end notifications, restart, stop, looping timeline, pitch, unsupported calls, shutdown safety");
	}

	void TestFileInfo()
	{
		const auto info = AudioFileUtils::GetFileInfo(std::filesystem::path(LUX_NATIVE_AUDIO_FIXTURES) / "tone.wav");
		CHECK(info && info->SamplingRate == 48000 && info->NumChannels == 2 && info->BitDepth == 16 && info->FileSize == 96044);
		CHECK_NEAR(info->Duration, 0.5, 1e-9);
		const auto ogg = AudioFileUtils::GetFileInfo(std::filesystem::path(LUX_NATIVE_AUDIO_FIXTURES) / "tone.ogg");
		CHECK(ogg && ogg->SamplingRate == 48000 && ogg->NumChannels == 2);
		CHECK(!AudioFileUtils::GetFileInfo(s_Directory / "missing.wav"));
		std::puts("PASS: audio file metadata without an engine");
	}
}

int main(int argc, char** argv)
{
	s_Directory = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path() / "lux-native-backend-tests";
	std::filesystem::create_directories(s_Directory);
	TestReferences();
	TestSerialization();
	TestCurves();
	TestEngine();
	TestPanAndAttenuation();
	TestListeners();
	TestOcclusion();
	TestVolumeAndBuses();
	TestPauseLayering();
	TestPlaybackLifecycle();
	TestFileInfo();
	std::puts("All native backend checks passed");
	return 0;
}
