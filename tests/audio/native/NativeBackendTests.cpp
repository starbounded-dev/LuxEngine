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
#include "Lux/Audio/AudioLegacyCleanup.h"
#include "Lux/Audio/AudioFileUtils.h"
#include "Lux/Audio/AudioListener.h"
#include "Lux/Audio/AudioPerformance.h"
#include "Lux/Audio/Sound.h"
#include "Lux/Audio/SoundBank.h"
#include "Lux/Audio/Native/NativeInternal.h"
#include "Lux/Audio/Native/Miniaudio.h"
#include "Lux/Audio/PhysicsAudioSystem.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Serialization/FileStream.h"
#include "Lux/Audio/AudioSurfaceTable.h"
#include "Lux/Audio/AudioAccessibilityMixer.h"
#include "Lux/Audio/MusicDirector.h"
#include "Lux/Audio/DialogueTable.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <numbers>
#include <atomic>
#include <thread>

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
	Lux::AudioMixerSettings s_Mixer;

	std::map<uint64_t, Lux::Ref<Lux::Sound>> s_Sounds;
	std::map<uint64_t, std::filesystem::path> s_Clips;
	// Clips inside a pack file, as a runtime asset pack delivers them: the file, and where the clip's bytes sit.
	struct PackedClip { std::filesystem::path Pack; uint64_t Offset = 0, Size = 0; };
	std::map<uint64_t, PackedClip> s_PackedClips;
	std::map<uint64_t, Lux::Ref<Lux::SoundBank>> s_Banks;
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
		config.Mixer = s_Mixer;
		return config;
	}

	bool NativeInternal::ResolveSound(const std::string& reference, Ref<Sound>& outSound, std::string& outError)
	{
		uint64_t handle = 0;
		const auto it = ParseHandle(reference, handle) ? s_Sounds.find(handle) : s_Sounds.end();
		if (it == s_Sounds.end())
		{
			outError = "not in the test table";
			return false;
		}
		outSound = it->second;
		return true;
	}

	bool NativeInternal::ResolveClip(AssetHandle clip, std::filesystem::path& outPath, std::string& outError)
	{
		// The runtime resolver's packed branch (NativeSoundResolver.cpp), over the test table.
		if (const auto packed = s_PackedClips.find(static_cast<uint64_t>(clip)); packed != s_PackedClips.end())
		{
			outPath = std::filesystem::path("luxpack") / (std::to_string(packed->first) + ".wav");
			if (!NativeInternal::RegisterPackedClip(outPath, packed->second.Pack, packed->second.Offset, packed->second.Size))
			{
				outError = "the packed clip cannot be decoded";
				return false;
			}
			return true;
		}
		const auto it = s_Clips.find(static_cast<uint64_t>(clip));
		if (it == s_Clips.end())
		{
			outError = "clip not in the test table";
			return false;
		}
		outPath = it->second;
		return true;
	}

	bool NativeInternal::ResolveSoundBank(AssetHandle bank, Ref<SoundBank>& outBank, std::string& outError)
	{
		const auto it = s_Banks.find(static_cast<uint64_t>(bank));
		if (it == s_Banks.end())
		{
			outError = "bank not in the test table";
			return false;
		}
		outBank = it->second;
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

	// An audio asset handle for a file.
	AssetHandle AddClip(const std::filesystem::path& file)
	{
		const uint64_t handle = s_NextHandle++;
		s_Clips[handle] = file;
		return handle;
	}

	// A Sound in the table; its root node plays the clip when one is given.
	std::string AddSound(const std::filesystem::path& clip, const std::function<void(Sound&)>& configure = {})
	{
		auto sound = Ref<Sound>::Create();
		sound->Handle = s_NextHandle++;
		if (!clip.empty())
			sound->Nodes.front().Clip = AddClip(clip);
		if (configure)
			configure(*sound);
		std::string error;
		if (!sound->Validate(&error))
			std::fprintf(stderr, "invalid test sound: %s\n", error.c_str());
		CHECK(error.empty());
		s_Sounds[static_cast<uint64_t>(sound->Handle)] = sound;
		return std::to_string(static_cast<uint64_t>(sound->Handle));
	}

	// A bank in the test table over Sound references (AddSound's return values).
	AssetHandle AddBank(const std::vector<std::string>& sounds)
	{
		auto bank = Ref<SoundBank>::Create();
		bank->Handle = s_NextHandle++;
		for (const auto& sound : sounds)
			CHECK(bank->Add(AssetHandle(std::stoull(sound))));
		s_Banks[static_cast<uint64_t>(bank->Handle)] = bank;
		return bank->Handle;
	}

	// Waits for a bank's clips to finish decoding on the streaming thread.
	bool WaitBankLoaded(AssetHandle bank)
	{
		for (int i = 0; i < 2000 && !AudioEngine::IsSoundBankLoaded(bank); ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		return AudioEngine::IsSoundBankLoaded(bank);
	}

	// Copies a file into a pack-like file between junk, and returns where its bytes sit.
	PackedClip PackFile(const std::filesystem::path& file, const std::string& packName)
	{
		std::ifstream in(file, std::ios::binary);
		const std::vector<char> bytes{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
		CHECK(!bytes.empty());
		PackedClip packed{ s_Directory / packName, 4096, bytes.size() };
		std::ofstream out(packed.Pack, std::ios::binary);
		const std::vector<char> junk(4096, '\x5A');
		out.write(junk.data(), junk.size());
		out.write(bytes.data(), bytes.size());
		out.write(junk.data(), 100);   // past the clip: reads must stop at its end
		CHECK(out.good());
		return packed;
	}

	// A long mono 16-bit WAV, written in one go (WriteTone writes sample by sample).
	std::filesystem::path WriteLongTone(const std::string& name, double hz, double seconds, double amplitude)
	{
		const auto path = s_Directory / name;
		const uint32_t frames = static_cast<uint32_t>(seconds * kRate);
		std::vector<int16_t> samples(frames);
		for (uint32_t i = 0; i < frames; ++i)
			samples[i] = static_cast<int16_t>(std::lround(amplitude * std::sin(2.0 * std::numbers::pi * hz * i / kRate) * 32767.0));
		std::ofstream out(path, std::ios::binary);
		const auto u32 = [&](uint32_t value) { out.write(reinterpret_cast<const char*>(&value), 4); };
		const auto u16 = [&](uint16_t value) { out.write(reinterpret_cast<const char*>(&value), 2); };
		out.write("RIFF", 4); u32(36 + frames * 2); out.write("WAVEfmt ", 8);
		u32(16); u16(1); u16(1); u32(kRate); u32(kRate * 2); u16(2); u16(16);
		out.write("data", 4); u32(frames * 2);
		out.write(reinterpret_cast<const char*>(samples.data()), samples.size() * sizeof(int16_t));
		CHECK(out.good());
		return path;
	}

	SoundNode ClipNode(AssetHandle clip, const std::function<void(SoundNode&)>& configure = {})
	{
		SoundNode node;
		node.Clip = clip;
		if (configure)
			configure(node);
		return node;
	}

	// A root container of the given type over clip children.
	void Container(Sound& sound, SoundNodeType type, std::vector<SoundNode> children)
	{
		sound.Nodes.assign(1, SoundNode{});
		sound.Nodes[0].Type = type;
		for (auto& child : children)
		{
			sound.Nodes[0].Children.push_back(static_cast<uint32_t>(sound.Nodes.size()));
			sound.Nodes.push_back(std::move(child));
		}
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
			// With nothing attached to the endpoint miniaudio renders nothing (a device fills silence);
			// the buffer is already zero.
			CHECK(read == count || read == 0);
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

		// Version 2: every field of the tree survives a roundtrip, and writing it again is identical.
		Sound sound;
		sound.Bus = "bus:/SFX";
		sound.Volume = 0.5f;
		sound.Pitch = 1.25f;
		sound.Spatial = { false, SoundAttenuation::Linear, 2.0f, 30.0f, 0.5f };
		sound.InstanceLimit = 4;
		sound.Steal = SoundStealPolicy::Quietest;
		sound.Cooldown = 0.25f;
		sound.Stream = true;
		sound.Parameters = { { "Speed", 0.0f, 10.0f, 2.5f, {} }, { "Surface", 0.0f, 2.0f, 1.0f, { "Grass", "Stone", "Water" } } };
		Container(sound, SoundNodeType::Layer, {});
		sound.Nodes[0].Name = "Root";
		sound.Nodes[0].Curves.push_back({ "Speed", SoundCurveTarget::Volume, { { 0.0f, 1.0f }, { 10.0f, 0.5f } } });
		sound.Nodes[0].Children = { 1, 2 };
		SoundNode random;
		random.Type = SoundNodeType::Random;
		random.AvoidRepeat = true;
		random.Children = { 3, 4 };
		random.VolumeVariation = 6.0f;
		random.PitchVariation = 2.0f;
		SoundNode surface;
		surface.Type = SoundNodeType::Switch;
		surface.SwitchParameter = "Surface";
		surface.Children = { 5 };
		surface.Curves.push_back({ "Speed", SoundCurveTarget::LowPass, { { 0.0f, 500.0f }, { 5.0f, 8000.0f } } });
		surface.Curves.push_back({ "Speed", SoundCurveTarget::HighPass, { { 0.0f, 20.0f } } });
		sound.Nodes.push_back(random);
		sound.Nodes.push_back(surface);
		sound.Nodes.push_back(ClipNode(77, [](SoundNode& n) { n.Weight = 3.0f; n.StartOffset = 0.5f; }));
		sound.Nodes.push_back(ClipNode(78, [](SoundNode& n) { n.Loop = true; n.HasLoopRegion = true; n.LoopStart = 0.25f; n.LoopEnd = 1.5f; n.Volume = 0.75f; n.Pitch = 0.5f; }));
		sound.Nodes.push_back(ClipNode(78, [](SoundNode& n) { n.Name = "Step"; }));
		std::string error;
		CHECK(sound.Validate(&error));
		Sound restored;
		CHECK(restored.FromYAML(sound.ToYAML()));
		CHECK(restored.ToYAML() == sound.ToYAML());
		CHECK(restored.Bus == "bus:/SFX" && restored.Volume == 0.5f && restored.Pitch == 1.25f && !restored.Spatial.Enabled
			&& restored.Spatial.Attenuation == SoundAttenuation::Linear && restored.Spatial.MinDistance == 2.0f && restored.Spatial.MaxDistance == 30.0f
			&& restored.Spatial.Rolloff == 0.5f && restored.InstanceLimit == 4 && restored.Steal == SoundStealPolicy::Quietest
			&& restored.Cooldown == 0.25f && restored.Stream && restored.Loops());
		CHECK(restored.Nodes.size() == 6 && restored.Nodes[2].SwitchParameter == "Surface" && restored.Nodes[1].AvoidRepeat
			&& restored.Nodes[4].HasLoopRegion && restored.Nodes[4].LoopEnd == 1.5f && restored.Nodes[3].Weight == 3.0f
			&& restored.Nodes[2].Curves.size() == 2 && restored.Nodes[2].Curves[1].Target == SoundCurveTarget::HighPass);
		CHECK(restored.Parameters.size() == 2 && restored.Parameters[1].Labels.size() == 3 && restored.Parameters[1].Maximum == 2.0f
			&& restored.Parameters[0].Default == 2.5f);
		CHECK(restored.GetClips() == std::vector<AssetHandle>({ 77, 78 }));
		CHECK(restored.FindParameter("speed") == &restored.Parameters[0] && !restored.FindParameter("Pace"));

		// Curves are piecewise linear and flat outside their points.
		const SoundCurve& lowPass = restored.Nodes[2].Curves[0];
		CHECK_NEAR(lowPass.Evaluate(-1.0f), 500.0, 0.0);
		CHECK_NEAR(lowPass.Evaluate(2.5f), 4250.0, 1e-3);
		CHECK_NEAR(lowPass.Evaluate(99.0f), 8000.0, 0.0);

		// Version 1 (Phase 2's single clip) becomes a root clip node.
		Sound migrated;
		CHECK(migrated.FromYAML("Sound: { Version: 1, Clip: 77, Loop: true, Volume: 0.5 }"));
		CHECK(migrated.Nodes.size() == 1 && migrated.Nodes[0].Clip == 77 && migrated.Nodes[0].Loop && migrated.Volume == 0.5f && migrated.Loops());
		Sound defaults;
		CHECK(defaults.FromYAML("Sound: { Version: 1 }") && defaults.Bus == "bus:/" && defaults.Spatial.Enabled && defaults.Nodes[0].Clip == 0
			&& !defaults.Loops() && defaults.GetClips().empty());
		CHECK(Sound::CreateForClip(5)->Nodes[0].Clip == 5);

		// Tree editing, as the Sound editor does it: every result is a valid tree.
		Sound edited = *Sound::CreateForClip(5);
		CHECK(edited.WrapNode(0, SoundNodeType::Random) == 0); // the root clip moves under a new Random root
		CHECK(edited.Nodes.size() == 2 && edited.Nodes[0].Type == SoundNodeType::Random && edited.Nodes[0].Children == std::vector<uint32_t>{ 1 }
			&& edited.Nodes[1].Clip == 5 && edited.Validate());
		const uint32_t layer = edited.AddNode(0, SoundNodeType::Layer);
		const uint32_t a = edited.AddNode(layer, SoundNodeType::Clip), b = edited.AddNode(layer, SoundNodeType::Clip);
		edited.Nodes[a].Clip = 6;
		edited.Nodes[b].Clip = 7;
		CHECK(edited.Validate() && edited.GetParents() == std::vector<int>({ -1, 0, 0, 2, 2 }));
		const uint32_t wrapped = edited.WrapNode(a, SoundNodeType::Sequence);
		CHECK(edited.Nodes[layer].Children == std::vector<uint32_t>({ wrapped, b }) && edited.Nodes[wrapped].Children == std::vector<uint32_t>{ a }
			&& edited.Validate());
		edited.RemoveNode(layer); // the layer, its sequence and both clips go; indices close up
		CHECK(edited.Nodes.size() == 2 && edited.Nodes[0].Children == std::vector<uint32_t>{ 1 } && edited.Nodes[1].Clip == 5 && edited.Validate());
		edited.RemoveNode(0); // the root stays
		CHECK(edited.Nodes.size() == 2);
		edited.Parameters = { { "Surface", 0.0f, 1.0f, 0.0f, { "Grass", "Stone" } } };
		edited.Nodes[0].Type = SoundNodeType::Switch;
		edited.Nodes[0].SwitchParameter = "surface";
		edited.Nodes[1].Curves.push_back({ "SURFACE", SoundCurveTarget::Volume, { { 0.0f, 1.0f } } });
		edited.Parameters[0].Name = "Ground";
		edited.RenameParameterUses("Surface", "Ground");
		CHECK(edited.Nodes[0].SwitchParameter == "Ground" && edited.Nodes[1].Curves[0].Parameter == "Ground" && edited.Validate());
		edited.RemoveParameterUses("ground");
		CHECK(edited.Nodes[0].SwitchParameter.empty() && edited.Nodes[1].Curves.empty());

		// Depth: a chain of 64 nodes is valid, 65 is not.
		Sound chain;
		chain.Nodes.assign(64, SoundNode{});
		for (uint32_t i = 0; i + 1 < 64; ++i)
		{
			chain.Nodes[i].Type = SoundNodeType::Layer;
			chain.Nodes[i].Children = { i + 1 };
		}
		CHECK(chain.Validate());
		chain.Nodes.back().Type = SoundNodeType::Layer;
		chain.Nodes.back().Children = { 64 };
		chain.Nodes.push_back(SoundNode{});
		CHECK(!chain.Validate(&error) && error.find("deeper") != std::string::npos);

		for (const char* bad : { "Sound: { Pitch: 0 }", "Sound: { Volume: 5 }", "Sound: { Bus: SFX }", "Sound: { Version: 3 }",
			"Sound: { Spatial: { MinDistance: 10, MaxDistance: 5 } }", "Sound: { Spatial: { Attenuation: Cubic } }", "Other: {}", "Sound: [1]",
			"Sound: { Version: 2 }", "Sound: { Version: 2, Nodes: [] }", "Sound: { Version: 2, Nodes: [{ Type: Blend }] }",
			"Sound: { Version: 2, Steal: Newest, Nodes: [{ Type: Clip }] }", "Sound: { Version: 2, Cooldown: 61, Nodes: [{ Type: Clip }] }",
			"Sound: { Version: 2, Nodes: [{ Type: Layer, Children: [1] }, { Type: Clip, Children: [0] }] }",
			"Sound: { Version: 2, Nodes: [{ Type: Layer, Children: [1, 1] }, { Type: Clip }] }",
			"Sound: { Version: 2, Nodes: [{ Type: Layer, Children: [2] }, { Type: Clip }, { Type: Clip }] }",
			"Sound: { Version: 2, Nodes: [{ Type: Layer, Children: [5] }, { Type: Clip }] }",
			"Sound: { Version: 2, Nodes: [{ Type: Clip, Children: [1] }, { Type: Clip }] }",
			"Sound: { Version: 2, Parameters: [{ Name: A }, { Name: a }], Nodes: [{ Type: Clip }] }",
			"Sound: { Version: 2, Parameters: [{ Name: A, Minimum: 1, Maximum: 1 }], Nodes: [{ Type: Clip }] }",
			"Sound: { Version: 2, Parameters: [{ Name: A, Labels: [X, X] }], Nodes: [{ Type: Clip }] }",
			"Sound: { Version: 2, Parameters: [{ Name: A }], Nodes: [{ Type: Switch, SwitchParameter: A, Children: [1] }, { Type: Clip }] }",
			"Sound: { Version: 2, Nodes: [{ Type: Clip, Curves: [{ Parameter: Missing, Target: Volume, Points: [[0, 1]] }] }] }",
			"Sound: { Version: 2, Parameters: [{ Name: A }], Nodes: [{ Type: Clip, Curves: [{ Parameter: A, Target: Volume, Points: [[1, 1], [0, 1]] }] }] }",
			"Sound: { Version: 2, Parameters: [{ Name: A }], Nodes: [{ Type: Clip, Curves: [{ Parameter: A, Target: LowPass, Points: [[0, 5]] }] }] }",
			"Sound: { Version: 2, Nodes: [{ Type: Clip, LoopRegion: [2, 1] }] }",
			"Sound: { Version: 2, Nodes: [{ Type: Clip, VolumeVariation: 30 }] }" })
		{
			Sound target = restored;
			CHECK(!target.FromYAML(bad));
			CHECK(target.ToYAML() == restored.ToYAML()); // a failed load changes nothing
		}
		std::puts("PASS: Sound YAML v2 tree and v1 migration, validation, curves, and both AudioEventRef forms roundtrip");
	}

	// Ported from the FMOD suite (tests/audio: AccessibilityTests, DialogueTests, PerformanceTests,
	// PhysicsAudioTests): the project's audio settings round-trip through YAML and the runtime binary,
	// refuse bad values without changing, and a short file fails to read. Sounds stand in for events.
	void TestProjectAudioSettings()
	{
		const std::string hit = "9117899157295655424";   // a Sound handle, the key FMOD's event GUID was
		const auto bin = [](const std::string& name) { return s_Directory / name; };

		AudioAccessibilityConfig config;
		config.BusPaths[1] = "bus:/Music";
		config.BusPaths[2] = "bus:/SFX";
		config.BusPaths[3] = "bus:/Dialogue";
		config.Defaults.Captions = config.Defaults.VisualCues = true;
		config.Events[hit].Captions["en"] = "[impact]";
		config.Events[hit].Captions["fr"] = "[choc]";
		config.Events[hit].VisualCue = true;
		config.Events[hit].MaxDistance = 10;
		config.SpeakerColors["Guard"] = { 0.2f, 0.4f, 0.6f, 1.0f };
		CHECK(config.Validate());
		Yaml::Writer yaml;
		config.SerializeYAML(yaml);
		AudioAccessibilityConfig restored;
		CHECK(restored.DeserializeYAML(Yaml::Load(yaml.c_str())));
		CHECK(restored.Events.at(hit).Captions.at("fr") == "[choc]");
		CHECK(restored.Defaults.Captions && restored.Defaults.MaxLines == 3);
		CHECK(!restored.DeserializeYAML(Yaml::Load("Defaults: {MaxLines: 0}")) && restored.Events.contains(hit));
		CHECK(!restored.DeserializeYAML(Yaml::Load("DescriptionDuck: .nan")));
		CHECK(!restored.DeserializeYAML(Yaml::Load("Buses: {Master: 'bus:/', Dialogue: 'bus:/'}")));
		CHECK(!restored.DeserializeYAML(Yaml::Load("Buses: {Music: 'bus:/Music', SFX: 'bus:/Music/SFX'}")));
		AudioAccessibilityConfig defaults;
		CHECK(defaults.DeserializeYAML(Yaml::Node()));
		CHECK(!defaults.Defaults.Mono && defaults.Defaults.Subtitles && defaults.Events.empty());
		{
			FileStreamWriter writer(bin("accessibility.bin"));
			CHECK(config.Serialize(writer));
			writer.WriteRaw<uint32_t>(0x12345678);
		}
		{
			FileStreamReader reader(bin("accessibility.bin"));
			AudioAccessibilityConfig read;
			CHECK(read.Deserialize(reader));
			uint32_t sentinel = 0;
			reader.ReadRaw(sentinel);
			CHECK(sentinel == 0x12345678 && read.Events.at(hit).VisualCue);   // reads exactly its own bytes
		}
		{
			FileStreamWriter writer(bin("bad-accessibility.bin"));
			writer.WriteRaw<uint32_t>(0xffffffff);
		}
		{
			FileStreamReader reader(bin("bad-accessibility.bin"));
			CHECK(!restored.Deserialize(reader));
		}

		auto table = Ref<DialogueTable>::Create();
		auto& greeting = table->Lines["greeting"];
		greeting.Event.Sound = std::stoull(hit);
		greeting.Translations["en"] = { "Hello", "Guard", "" };
		greeting.Translations["fr"] = { "Bonjour", "Garde", "" };
		table->Lines["urgent"] = greeting;
		table->Lines["urgent"].Priority = DialoguePriority::Critical;
		table->Lines["locked"] = greeting;
		table->Lines["locked"].Interruptible = false;
		CHECK(table->Validate());
		DialogueTable loaded;
		CHECK(loaded.FromYAML(table->ToYAML()));
		CHECK(loaded.Lines.at("greeting").Translations.at("fr").Text == "Bonjour" && loaded.Lines.at("greeting").Event.Sound == greeting.Event.Sound);
		CHECK(!loaded.FromYAML("Version: 999\n") && loaded.Lines.size() == 3);
		CHECK(!loaded.FromYAML("DefaultLanguage: invalid/code\n"));
		CHECK(!DialogueTable::ValidLanguage(std::string("en\0x", 4)));
		DialogueSettings settings;
		settings.Table = 123;
		settings.Language = "fr-CA";
		{
			FileStreamWriter writer(bin("dialogue-settings.bin"));
			CHECK(settings.Serialize(writer));
		}
		{
			DialogueSettings read;
			FileStreamReader reader(bin("dialogue-settings.bin"));
			CHECK(read.Deserialize(reader) && read.Table == settings.Table && read.Language == settings.Language);
		}
		{
			FileStreamWriter writer(bin("bad-dialogue-settings.bin"));
			writer.WriteRaw<uint64_t>(0);
			writer.WriteRaw<uint32_t>(0xffffffff);
		}
		{
			FileStreamReader reader(bin("bad-dialogue-settings.bin"));
			CHECK(!settings.Deserialize(reader) && settings.Table == 123 && settings.Language == "fr-CA");
		}

		AudioPerformanceSettings performance;
		CHECK(performance.Validate() && performance.RealVoices == 64);
		for (const char* bad : { "RealVoices: 0", "RealVoices: 513", "CPUPercent: .nan", "BankMemoryMiB: -1",
			"RaytracingMilliseconds: 0", "BusVoices: { 'bus:/': 0 }", "BusVoices: { nonsense: 2 }", "BusVoices: { 'bus:/': 2, 'bus:/': 3 }" })
			CHECK(!performance.DeserializeYAML(Yaml::Load(bad)) && performance.RealVoices == 64);
		performance.RealVoices = 32;
		performance.BusVoices = { { "bus:/", 16 }, { "bus:/SFX", 4 } };
		const auto file = bin("performance-settings.bin");
		{
			FileStreamWriter writer(file);
			CHECK(performance.Serialize(writer));
		}
		AudioPerformanceSettings copied;
		{
			FileStreamReader reader(file);
			CHECK(copied.Deserialize(reader) && copied.RealVoices == 32 && copied.BusVoices == performance.BusVoices);
		}
		std::filesystem::resize_file(file, std::filesystem::file_size(file) - 1);
		{
			FileStreamReader reader(file);
			CHECK(!copied.Deserialize(reader));
		}

		AudioSurfaceTable surfaces;
		surfaces.Surfaces[2].Impact.Sound = std::stoull(hit);
		surfaces.ImpactCooldown = 0.3f;
		AudioSurfaceTable roundtrip;
		CHECK(roundtrip.FromYAML(surfaces.ToYAML()));
		CHECK(roundtrip.Surfaces[2].Impact.Sound == surfaces.Surfaces[2].Impact.Sound && roundtrip.ImpactCooldown == 0.3f);
		CHECK(roundtrip.FromYAML("AudioSurfaceTable: {}") && roundtrip.ImpactCooldown == 0.15f && !roundtrip.Surfaces[2].Impact.HasReference());
		CHECK(!roundtrip.FromYAML("AudioSurfaceTable: {MinimumImpulse: -.inf}"));
		std::puts("PASS: project audio settings: accessibility, dialogue, performance and surface tables round-trip and refuse bad data");
	}

	void TestSoundBankAsset()
	{
		SoundBank bank;
		CHECK(!bank.Resident);
		CHECK(bank.Add(AssetHandle(11)) && bank.Add(AssetHandle(22)));
		CHECK(!bank.Add(AssetHandle(11)) && !bank.Add(AssetHandle(0)));   // no duplicates, no empty handle
		CHECK(bank.Sounds.size() == 2 && bank.Contains(AssetHandle(22)));
		bank.Resident = true;

		SoundBank restored;
		CHECK(restored.FromYAML(bank.ToYAML()));
		CHECK(restored.Resident && restored.Sounds == bank.Sounds);
		CHECK(restored.Remove(AssetHandle(11)) && !restored.Remove(AssetHandle(11)));
		CHECK(restored.Sounds == std::vector<AssetHandle>{ AssetHandle(22) });

		// A full bank still fits the 64 KiB file limit, and refuses more.
		SoundBank full;
		for (uint64_t i = 0; i < SoundBank::MaxSounds; ++i)
			CHECK(full.Add(AssetHandle(UINT64_MAX - i)));
		CHECK(!full.Add(AssetHandle(1)));
		CHECK(full.ToYAML().size() <= SoundBank::MaxFileBytes);

		for (const char* bad : {
			"SoundBank: { Version: 1, Sounds: [5, 5] }",
			"SoundBank: { Version: 1, Sounds: [0] }",
			"SoundBank: { Version: 2, Sounds: [] }",
			"SoundBank: { Version: 1, Sounds: 5 }",
			"Sound: { Version: 1 }" })
		{
			SoundBank target = bank;
			CHECK(!target.FromYAML(bad));
			CHECK(target.ToYAML() == bank.ToYAML()); // a failed load changes nothing
		}
		CHECK(restored.FromYAML("SoundBank: { Version: 1 }") && restored.Sounds.empty() && !restored.Resident);
		std::puts("PASS: SoundBank YAML roundtrip, duplicates and empty handles refused, 2048-Sound limit within 64 KiB");
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
		CHECK(AudioEngine::GetLoadedSoundBanks().empty() && AudioEngine::GetLoadedClips().empty());
		AudioEventParameterInfo parameter;
		CHECK(!AudioEngine::GetEventParameter("1", "Speed", parameter));
		CHECK(!AudioEngine::SetBusVolume("bus:/NotInTheMixer", 0.5f) && !AudioEngine::SetVCAVolume("vca:/Music", 0.5f));
		CHECK(!AudioEventInstance::Create("event:/Door") && !AudioEventInstance::Create("424242") && !AudioEventInstance::Create(""));
		AudioEngine::Shutdown();
		CHECK(!AudioEngine::HasInitializedEngine() && !NativeInternal::GetEngine());
		std::puts("PASS: engine lifecycle, stats, and unknown references refused cleanly");
	}

	void TestPanAndAttenuation()
	{
		Start();
		const auto tone = WriteTone("pan.wav", 500.0, 1.0);
		const auto flat = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Attenuation = SoundAttenuation::None; });
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

		const auto inverse = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; });
		const Level oneMetre = PlayAt(inverse, { 0.0f, 0.0f, -1.0f });
		const Level fourMetres = PlayAt(inverse, { 0.0f, 0.0f, -4.0f });
		CHECK_NEAR(fourMetres.Left / oneMetre.Left, 0.25, 0.002);
		const auto linear = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Attenuation = SoundAttenuation::Linear; s.Spatial.MaxDistance = 11.0f; });
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
		const auto sound = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; });
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

		// No active listener: a neutral one at the origin.
		AudioListener::Apply({});
		CHECK_NEAR(PlayAt(sound, source).Left / alone, 1.0, 1e-4);
		AudioEngine::Shutdown();
		std::puts("PASS: attenuation targets, normalized listener weights, neutral listener when none is active");
	}

	void TestOcclusion()
	{
		Start();
		const auto low = AddSound(WriteTone("low.wav", 100.0, 1.0), [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Attenuation = SoundAttenuation::None; });
		const auto high = AddSound(WriteTone("high.wav", 10000.0, 1.0), [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Attenuation = SoundAttenuation::None; });
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
		const auto flat = AddSound(WriteTone("flat.wav", 100.0, 1.0), [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; });
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
		const auto sound = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; });
		const auto quiet = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; s.Volume = 0.5f; });
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

	void TestMixer()
	{
		// Settings: parents before children, unique well-formed paths, values in range, YAML round trip.
		AudioMixerSettings settings;
		CHECK(settings.Validate() && settings.Buses.size() == 4);
		CHECK(AudioMixerSettings::ParentPath("bus:/SFX") == "bus:/" && AudioMixerSettings::ParentPath("bus:/SFX/Steps") == "bus:/SFX");
		settings.Buses = { { "bus:/SFX/Steps" }, { "bus:/SFX" } };
		CHECK(!settings.Validate());
		settings.Buses = { { "bus:/SFX" }, { "bus:/SFX" } };
		CHECK(!settings.Validate());
		for (const char* bad : { "bus:/", "bus:/SFX/", "bus://SFX", "SFX" })
		{
			settings.Buses = { { bad } };
			CHECK(!settings.Validate());
		}
		settings.Buses = { { "bus:/SFX", 0.25f, true }, { "bus:/SFX/Steps", 4.0f } };
		settings.Reverb.RoomSize = 0.3f;
		CHECK(settings.Validate());
		Yaml::Writer out;
		settings.SerializeYAML(out);
		AudioMixerSettings loaded;
		CHECK(loaded.DeserializeYAML(Yaml::Load(out.c_str())));
		CHECK(loaded.Buses.size() == 2 && loaded.Buses[1].Path == "bus:/SFX/Steps" && loaded.Buses[0].Muted && loaded.Buses[0].Volume == 0.25f);
		CHECK(loaded.Reverb.RoomSize == 0.3f);
		CHECK(!loaded.DeserializeYAML(Yaml::Load("Buses: [{Path: 'bus:/A', Volume: 9}]")) && loaded.Buses.size() == 2);

		// Bus volume and mute reach only that bus's sounds.
		Start();
		const auto tone = WriteTone("mixer.wav", 500.0, 1.0);
		const auto master = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; });
		const auto sfx = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; s.Bus = "bus:/SFX"; });
		const glm::vec3 front{ 0.0f, 0.0f, -1.0f };
		const double full = PlayAt(master, front).Left;
		CHECK(full > 0.3);
		CHECK_NEAR(PlayAt(sfx, front).Left / full, 1.0, 1e-4);
		CHECK(AudioEngine::SetBusVolume("bus:/SFX", 0.5f) && AudioEngine::GetBusVolume("bus:/SFX") == 0.5f);
		CHECK_NEAR(PlayAt(sfx, front).Left / full, 0.5, 1e-3);
		CHECK_NEAR(PlayAt(master, front).Left / full, 1.0, 1e-4);
		CHECK(AudioEngine::SetBusMuted("bus:/SFX", true));
		CHECK(PlayAt(sfx, front).Left == 0.0);
		CHECK(AudioEngine::SetBusMuted("bus:/SFX", false) && AudioEngine::SetBusVolume("bus:/SFX", 1.0f));
		CHECK(!AudioEngine::SetBusVolume("bus:/Missing", 0.5f) && AudioEngine::GetBusVolume("bus:/Missing") == 0.0f);
		AudioEngine::Shutdown();

		// Nested buses multiply; a sound on a bus the mixer lacks plays through the master.
		s_Mixer.Buses = { { "bus:/SFX", 0.5f }, { "bus:/SFX/Steps", 0.5f } };
		Start();
		const auto steps = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; s.Bus = "bus:/SFX/Steps"; });
		const auto lost = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; s.Bus = "bus:/Nope"; });
		CHECK_NEAR(PlayAt(steps, front).Left / full, 0.25, 1e-3);
		CHECK_NEAR(PlayAt(lost, front).Left / full, 1.0, 1e-4);
		AudioEngine::Shutdown();

		// The reverb return: a short hit leaves a tail after it ends only when it is sent.
		s_Mixer = {};
		Start();
		const auto hit = AddSound(WriteTone("hit.wav", 500.0, 0.05), [](Sound& s) { s.Spatial.Enabled = false; });
		const auto tail = [&](float send)
		{
			auto instance = Play(hit, front);
			instance->SetAcoustics({ 1.0f, send });
			Render(kRate / 10);                // the 50 ms hit, and its end
			const double level = Rms(Render(kRate / 10), 0);
			instance->Stop(false);
			Render(kRate);                     // let any tail die before the next case
			return level;
		};
		CHECK(tail(0.0f) < 1e-6);
		CHECK(tail(1.0f) > 1e-3);
		AudioEngine::Shutdown();
		std::puts("PASS: mixer settings validation and YAML, bus volume and mute, nested buses, unknown bus fallback, reverb return tail");
	}

	void TestSnapshots()
	{
		// Settings: snapshot IDs and names are unique, and they override existing buses once each.
		AudioMixerSettings settings;
		settings.Snapshots = { { 501, "Underwater", 0, { { "bus:/SFX", 0.0f } } } };
		CHECK(settings.Validate());
		settings.Snapshots.push_back({ 501, "Other" });
		CHECK(!settings.Validate());
		settings.Snapshots.back() = { 502, "Underwater" };
		CHECK(!settings.Validate());
		settings.Snapshots.back() = { 502, "Bad", 0, { { "bus:/Nope", 0.5f } } };
		CHECK(!settings.Validate());
		settings.Snapshots.back() = { 502, "Combat", 1, { { "bus:/SFX", 0.5f } } };
		CHECK(settings.Validate());
		Yaml::Writer out;
		settings.SerializeYAML(out);
		AudioMixerSettings loaded;
		CHECK(loaded.DeserializeYAML(Yaml::Load(out.c_str())) && loaded == settings);

		// The calls zones make (AudioZoneSystem::Prepare/Play): create from the reference, intensity 0,
		// start, then the zone weight as intensity.
		s_Mixer = settings;
		Start();
		const auto tone = WriteTone("snapshot.wav", 500.0, 1.0);
		const auto sfx = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; s.Bus = "bus:/SFX"; });
		const glm::vec3 front{ 0.0f, 0.0f, -1.0f };
		auto voice = Play(sfx, front);
		const auto level = [&] { return Measure().Left; };
		const double full = level();
		CHECK(full > 0.3);
		auto underwater = AudioEventInstance::Create("501");
		CHECK(underwater && underwater->IsSnapshot() && !underwater->IsOneShot());
		CHECK(underwater->SetSnapshotIntensity(0.0f) && underwater->Start() && underwater->IsPlaying());
		CHECK_NEAR(level() / full, 1.0, 1e-3);
		CHECK(underwater->SetSnapshotIntensity(0.5f));
		CHECK_NEAR(level() / full, 0.5, 2e-3);    // halfway from 1 to 0
		CHECK(underwater->SetSnapshotIntensity(1.0f));
		CHECK(level() < 1e-4);
		// A higher priority wins the bus while active; stopping it hands the bus back.
		auto combat = AudioEventInstance::Create("502");
		CHECK(combat && combat->Start());
		CHECK_NEAR(level() / full, 0.5, 2e-3);
		combat->Stop(false);
		CHECK(level() < 1e-4);
		// Releasing an active snapshot removes it; the bus's own volume still applies under snapshots.
		underwater = nullptr;
		CHECK_NEAR(level() / full, 1.0, 1e-3);
		CHECK(AudioEngine::SetBusVolume("bus:/SFX", 0.5f));
		CHECK(combat->Start() && combat->SetSnapshotIntensity(0.5f));
		CHECK_NEAR(level() / full, 0.5, 2e-3);    // 0.5 toward 0.5 stays 0.5
		CHECK(!combat->SetSnapshotIntensity(1.5f) && !voice->SetSnapshotIntensity(0.5f) && !voice->IsSnapshot());
		// A volume the game sets owns the bus: snapshots leave it alone until it is cleared.
		CHECK(combat->SetSnapshotIntensity(1.0f) && AudioEngine::SetBusVolume("bus:/SFX", 1.0f));
		CHECK_NEAR(level() / full, 1.0, 1e-3);
		CHECK(AudioEngine::ClearBusVolume("bus:/SFX") && AudioEngine::GetBusVolume("bus:/SFX") == 1.0f);
		CHECK_NEAR(level() / full, 0.5, 2e-3);    // back under Combat's 0.5
		voice->Stop(false);
		AudioEngine::Shutdown();
		s_Mixer = {};
		std::puts("PASS: mixer snapshots: validation and YAML, intensity blend, priority, stop and release, zone call sequence");
	}

	void TestEffects()
	{
		using Type = AudioMixerEffectType;
		const auto effect = [](Type type, const std::function<void(AudioMixerEffect&)>& configure = {})
		{
			AudioMixerEffect stage;
			stage.Type = type;
			if (configure)
				configure(stage);
			return stage;
		};
		// Settings: values in range, at most 8 stages, YAML round trip of every type.
		AudioMixerSettings settings;
		settings.Buses = { { "bus:/SFX" } };
		settings.Buses[0].Effects = { effect(Type::LowPass), effect(Type::HighPass), effect(Type::PeakingEQ), effect(Type::Delay), effect(Type::Compressor) };
		settings.Buses[0].Effects[1].Bypass = true;
		CHECK(settings.Validate());
		Yaml::Writer out;
		settings.SerializeYAML(out);
		AudioMixerSettings loaded;
		CHECK(loaded.DeserializeYAML(Yaml::Load(out.c_str())) && loaded == settings);
		settings.Buses[0].Effects[0].FrequencyHz = 5.0f;
		CHECK(!settings.Validate());
		settings.Buses[0].Effects.assign(9, effect(Type::LowPass));
		CHECK(!settings.Validate());
		CHECK(!loaded.DeserializeYAML(Yaml::Load("Buses: [{Path: 'bus:/A', Effects: [{Type: Flanger}]}]")));

		const auto low = WriteTone("fx-low.wav", 100.0, 1.0);
		const auto mid = WriteTone("fx-mid.wav", 500.0, 1.0);
		const auto high = WriteTone("fx-high.wav", 10000.0, 1.0);
		const glm::vec3 front{ 0.0f, 0.0f, -1.0f };
		const auto onBus = [](const std::filesystem::path& tone, const char* bus)
		{
			return AddSound(tone, [bus](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; s.Bus = bus; });
		};
		// Each case: the level of a tone through an SFX chain over the same tone on the master.
		const auto ratio = [&](const std::vector<AudioMixerEffect>& chain, const std::filesystem::path& tone)
		{
			s_Mixer.Buses = { { "bus:/SFX" } };
			s_Mixer.Buses[0].Effects = chain;
			Start();
			const double dry = PlayAt(onBus(tone, "bus:/"), front).Left;
			const double wet = PlayAt(onBus(tone, "bus:/SFX"), front).Left;
			AudioEngine::Shutdown();
			return wet / dry;
		};
		const auto lowPass = std::vector{ effect(Type::LowPass, [](AudioMixerEffect& e) { e.FrequencyHz = 1000.0f; }) };
		CHECK_NEAR(ratio(lowPass, low), 1.0, 0.02);
		CHECK(ratio(lowPass, high) < 0.05);                      // second order: about -40 dB a decade
		const auto highPass = std::vector{ effect(Type::HighPass, [](AudioMixerEffect& e) { e.FrequencyHz = 1000.0f; }) };
		CHECK(ratio(highPass, low) < 0.05);
		CHECK_NEAR(ratio(highPass, high), 1.0, 0.02);
		const auto peak = std::vector{ effect(Type::PeakingEQ, [](AudioMixerEffect& e) { e.FrequencyHz = 500.0f; e.GainDb = 12.0f; e.Q = 1.0f; }) };
		CHECK_NEAR(ratio(peak, mid), std::pow(10.0, 12.0 / 20.0), 0.1);
		CHECK_NEAR(ratio(peak, low), 1.0, 0.25);                 // far from the centre
		auto bypassed = lowPass;
		bypassed[0].Bypass = true;
		CHECK_NEAR(ratio(bypassed, high), 1.0, 1e-4);
		// -6 dBFS peaks over a -20 dB threshold at 4:1 lose (20 - 6) * 3/4 = 10.5 dB.
		const auto compressor = std::vector{ effect(Type::Compressor, [](AudioMixerEffect& e) { e.ThresholdDb = -20.0f; e.Ratio = 4.0f; e.AttackMs = 1.0f; e.ReleaseMs = 50.0f; }) };
		CHECK_NEAR(ratio(compressor, mid), std::pow(10.0, -10.5 / 20.0), 0.05);
		auto makeup = compressor;
		makeup[0].MakeupDb = 6.0f;
		CHECK_NEAR(ratio(makeup, mid) / ratio(compressor, mid), std::pow(10.0, 6.0 / 20.0), 0.02);

		// The delay repeats a short hit after it has ended.
		const auto hit = WriteTone("fx-hit.wav", 500.0, 0.05);
		const auto echo = [&](float wet)
		{
			s_Mixer.Buses = { { "bus:/SFX" } };
			s_Mixer.Buses[0].Effects = { effect(Type::Delay, [wet](AudioMixerEffect& e) { e.DelaySeconds = 0.2f; e.Feedback = 0.0f; e.Wet = wet; }) };
			Start();
			auto instance = Play(AddSound(hit, [](Sound& s) { s.Spatial.Enabled = false; s.Bus = "bus:/SFX"; }), front);
			Render(kRate / 10);                                     // the hit, and silence until 100 ms
			const double before = Rms(Render(kRate / 10), 0);       // 100-200 ms: nothing yet
			const double repeat = Rms(Render(kRate / 10), 0);       // 200-300 ms: the echo
			AudioEngine::Shutdown();
			return std::pair{ before, repeat };
		};
		const auto [quiet, repeated] = echo(1.0f);
		CHECK(quiet < 1e-6 && repeated > 0.05);
		CHECK(echo(0.0f).second < 1e-6);
		s_Mixer = {};
		std::puts("PASS: bus effects: validation and YAML, low/high-pass, peaking EQ, bypass, compressor and makeup, delay echo");
	}

	void TestVCAsAndGlobals()
	{
		// Settings: vca:/ plus one unique name, listing existing buses once each.
		AudioMixerSettings settings;
		settings.VCAs = { { "vca:/World", { "bus:/SFX", "bus:/Ambience" } } };
		CHECK(settings.Validate());
		Yaml::Writer out;
		settings.SerializeYAML(out);
		AudioMixerSettings loaded;
		CHECK(loaded.DeserializeYAML(Yaml::Load(out.c_str())) && loaded == settings);
		for (const AudioMixerVCA& bad : { AudioMixerVCA{ "vca:/" }, AudioMixerVCA{ "World" }, AudioMixerVCA{ "vca:/A/B" }, AudioMixerVCA{ "vca:/X", { "bus:/Nope" } },
			AudioMixerVCA{ "vca:/X", { "bus:/SFX", "bus:/SFX" } } })
		{
			settings.VCAs = { bad };
			CHECK(!settings.Validate());
		}

		// A VCA scales its buses only, on top of each bus's own volume.
		s_Mixer.VCAs = { { "vca:/World", { "bus:/SFX" } } };
		Start();
		const auto tone = WriteTone("vca.wav", 500.0, 1.0);
		const auto onBus = [&](const char* bus) { return AddSound(tone, [bus](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; s.Bus = bus; }); };
		const auto sfx = onBus("bus:/SFX"), music = onBus("bus:/Music");
		const glm::vec3 front{ 0.0f, 0.0f, -1.0f };
		const double full = PlayAt(sfx, front).Left;
		CHECK(AudioEngine::SetVCAVolume("vca:/World", 0.5f));
		CHECK_NEAR(PlayAt(sfx, front).Left / full, 0.5, 2e-3);
		CHECK_NEAR(PlayAt(music, front).Left / full, 1.0, 1e-3);
		CHECK(AudioEngine::SetBusVolume("bus:/SFX", 0.5f));
		CHECK_NEAR(PlayAt(sfx, front).Left / full, 0.25, 2e-3);
		CHECK(!AudioEngine::SetVCAVolume("vca:/Missing", 0.5f));

		// A global parameter: one value for every Sound that marks a parameter of that name Global.
		const auto global = [&](float minimum)
		{
			return AddSound(tone, [minimum](Sound& s)
			{
				s.Nodes[0].Loop = true;
				s.Spatial.Enabled = false;
				s.Parameters.push_back({ "TimeOfDay", minimum, 1.0f, 1.0f });
				s.Parameters.back().Global = true;
				s.Nodes[0].Curves.push_back({ "TimeOfDay", SoundCurveTarget::Volume, { { 0.0f, 0.0f }, { 1.0f, 1.0f } } });
			});
		};
		const auto day = global(0.0f), clamped = global(0.5f);
		CHECK_NEAR(PlayAt(day, front).Left / full, 1.0, 2e-3);                 // Default until set
		CHECK(AudioEngine::SetGlobalParameter("timeofday", 0.25f) && AudioEngine::GetGlobalParameter("TimeOfDay") == 0.25f);
		CHECK_NEAR(PlayAt(day, front).Left / full, 0.25, 2e-3);
		CHECK_NEAR(PlayAt(clamped, front).Left / full, 0.5, 2e-3);             // held to this Sound's range
		auto instance = Play(day, front);
		CHECK(!instance->SetParameter("TimeOfDay", 1.0f) && instance->GetParameter("TimeOfDay") == 0.25f);
		CHECK(AudioEngine::SetGlobalParameter("TimeOfDay", 1.0f));            // live on a playing voice
		CHECK_NEAR(Measure().Left / full, 1.0, 2e-3);
		instance->Stop(false);
		AudioEngine::Shutdown();
		CHECK(AudioEngine::GetGlobalParameter("TimeOfDay") == 0.0f);          // cleared with the engine
		s_Mixer = {};
		std::puts("PASS: VCAs over bus volume, global parameters across Sounds, range clamp, live change, instance writes refused");
	}

	void TestAccessibility()
	{
		Start();
		const auto tone = WriteTone("access.wav", 500.0, 1.0);
		const auto onBus = [&](const char* bus) { return AddSound(tone, [bus](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; s.Bus = bus; }); };
		const auto master = onBus("bus:/"), sfx = onBus("bus:/SFX"), dialogue = onBus("bus:/Dialogue");
		const glm::vec3 front{ 0.0f, 0.0f, -1.0f };
		const double full = PlayAt(master, front).Left;

		AudioAccessibilityConfig config;
		config.BusPaths[static_cast<size_t>(AudioCategory::SFX)] = "bus:/SFX";
		config.BusPaths[static_cast<size_t>(AudioCategory::Dialogue)] = "bus:/Dialogue";
		AudioAccessibilityMixer mixer;
		CHECK(mixer.Configure(config) && mixer.HasBus(AudioCategory::Master) && mixer.HasBus(AudioCategory::SFX) && !mixer.HasBus(AudioCategory::Music));
		AudioAccessibilityPreferences preferences;
		preferences.Volumes[static_cast<size_t>(AudioCategory::SFX)] = 0.5f;
		CHECK(mixer.Apply(preferences, false));
		CHECK_NEAR(PlayAt(sfx, front).Left / full, 0.5, 2e-3);
		// Independent of the mix and of gameplay: a bus volume multiplies with the category gain.
		CHECK(AudioEngine::SetBusVolume("bus:/SFX", 0.5f));
		CHECK_NEAR(PlayAt(sfx, front).Left / full, 0.25, 2e-3);
		CHECK(AudioEngine::ClearBusVolume("bus:/SFX"));
		preferences.Volumes[static_cast<size_t>(AudioCategory::Master)] = 0.5f;
		CHECK(mixer.Apply(preferences, false));
		CHECK_NEAR(PlayAt(master, front).Left / full, 0.5, 2e-3);
		CHECK_NEAR(PlayAt(sfx, front).Left / full, 0.25, 2e-3);
		preferences.Volumes = { 1, 1, 1, 1, 1, 1 };
		preferences.DialogueBoost = 2.0f;
		CHECK(mixer.Apply(preferences, true));                    // describing: everything but dialogue and master ducks
		CHECK_NEAR(PlayAt(dialogue, front).Left / full, 2.0, 4e-3);
		CHECK_NEAR(PlayAt(sfx, front).Left / full, config.DescriptionDuck, 2e-3);
		preferences.DialogueBoost = 1.0f;

		// Mono folds a sound panned hard left into both channels.
		const auto panned = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Attenuation = SoundAttenuation::None; });
		const glm::vec3 left{ -5.0f, 0.0f, 0.0f };
		CHECK(mixer.Apply(preferences, false));
		const Level stereo = PlayAt(panned, left);
		CHECK(stereo.Left > 2.0 * stereo.Right);
		preferences.Mono = true;
		CHECK(mixer.Apply(preferences, false));
		const Level mono = PlayAt(panned, left);
		CHECK_NEAR(mono.Left, mono.Right, 1e-6);
		CHECK_NEAR(mono.Left, (stereo.Left + stereo.Right) / 2.0, 0.02);
		preferences.Mono = false;

		// Compression: Full leaves the level alone, Reduced (-12 dB, 3:1) and Night (-24 dB, 6:1) cut a
		// -6 dBFS tone by up to (12 - 6) * 2/3 = 4 dB and (24 - 6) * 5/6 = 15 dB; the 10 ms attack
		// envelope sits a little below the sine's peaks, so the measured cut is somewhat smaller.
		const auto range = [&](AudioDynamicRange dynamicRange)
		{
			preferences.DynamicRange = dynamicRange;
			CHECK(mixer.Apply(preferences, false));
			return PlayAt(master, front).Left / full;
		};
		CHECK_NEAR(range(AudioDynamicRange::Full), 1.0, 1e-4);
		const double reducedDb = 20.0 * std::log10(range(AudioDynamicRange::Reduced));
		const double nightDb = 20.0 * std::log10(range(AudioDynamicRange::Night));
		CHECK(reducedDb < -2.5 && reducedDb > -4.5);
		CHECK(nightDb < -11.0 && nightDb > -15.5);

		// Reset hands everything back.
		mixer.Reset();
		CHECK(!mixer.HasBus(AudioCategory::SFX));
		CHECK_NEAR(PlayAt(sfx, front).Left / full, 1.0, 1e-3);
		CHECK_NEAR(PlayAt(master, front).Left / full, 1.0, 1e-3);
		config.BusPaths[static_cast<size_t>(AudioCategory::Music)] = "bus:/NotInTheMixer";
		CHECK(!mixer.Configure(config) && mixer.HasBus(AudioCategory::SFX) && !mixer.HasBus(AudioCategory::Music));
		mixer.Reset();
		AudioEngine::Shutdown();
		std::puts("PASS: accessibility: category gains over bus volumes, master gain, dialogue boost, description duck, mono fold, compression presets, reset");
	}

	void TestMeters()
	{
		s_Performance.BusVoices = { { "bus:/", 64 }, { "bus:/SFX", 1 } };
		Start();
		const auto tone = WriteTone("meter.wav", 500.0, 1.0);    // amplitude 0.5: peak 0.5, RMS 0.354
		const auto sfx = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; s.Bus = "bus:/SFX"; });
		const glm::vec3 front{ 0.0f, 0.0f, -1.0f };
		const auto meter = [](const std::string& path)
		{
			AudioPerformance::Update();
			const auto& buses = AudioPerformance::GetBuses();
			const auto it = std::find_if(buses.begin(), buses.end(), [&](const AudioBusMeter& entry) { return entry.Path == path; });
			CHECK(it != buses.end());
			return *it;
		};
		auto first = Play(sfx, front);
		Render(kSettle);
		// No device here, so no CPU figure; the clip's decoded samples count as memory (1 s, mono, float).
		const auto& stats = AudioPerformance::GetStats();
		const AudioBusMeter bus = meter("bus:/SFX");
		CHECK(!stats.MixerAvailable && stats.MemoryAvailable);
		CHECK_NEAR(stats.MemoryMiB, kRate * sizeof(float) / (1024.0 * 1024.0), 1e-4);
		CHECK(bus.Available && bus.Voices == 1 && !bus.Exceeded);
		CHECK_NEAR(bus.Peak, 0.5, 0.01);
		CHECK_NEAR(bus.RMS, 0.5 / std::sqrt(2.0), 0.01);
		CHECK(meter("bus:/SFX").Peak == 0.0f);                    // the peak holds until read, then resets
		Render(kBlock);
		const AudioBusMeter master = meter("bus:/");
		CHECK(master.Available && master.Voices == 1);
		CHECK_NEAR(master.Peak, 0.5, 0.01);
		// A second voice on a bus with a limit of 1 exceeds its budget; the master's 64 is fine.
		auto second = Play(sfx, front);
		Render(kBlock);
		CHECK(meter("bus:/SFX").Exceeded && meter("bus:/SFX").Voices == 2 && !meter("bus:/").Exceeded);
		first->Stop(false);
		second->Stop(false);
		AudioEngine::Shutdown();
		s_Performance = {};
		std::puts("PASS: bus meters: peak and RMS of a known sine, peak hold and reset, inclusive voice counts, bus voice budget");
	}

	void TestMusicTimeline()
	{
		// Settings: the tempo map validates and round-trips.
		Sound check;
		check.Tempo = { 120.0f, 4, 4, 0.0f };
		check.Markers = { { "Section:Chorus", 1.25f } };
		std::string error;
		CHECK(check.Validate(&error));
		Sound copy;
		CHECK(copy.FromYAML(check.ToYAML()) && copy.Tempo == check.Tempo && copy.Markers == check.Markers);
		check.Tempo.BPM = 5.0f;
		CHECK(!check.Validate(&error));
		check.Tempo = { 120.0f, 4, 3, 0.0f };
		CHECK(!check.Validate(&error));

		Start();
		const auto bed = [&](const char* name, double hz)
		{
			return AddSound(WriteTone(name, hz, 4.0), [](Sound& s)
			{
				s.Nodes[0].Loop = true;
				s.Spatial.Enabled = false;
				s.Tempo = { 120.0f, 4, 4, 0.0f };    // a beat every 0.5 s, a bar every 2 s
				s.Markers = { { "Section:Chorus", 1.25f }, { "Hit", 3.0f } };
			});
		};
		const auto a = bed("music-a.wav", 500.0), b = bed("music-b.wav", 700.0);
		const auto renderTo = [&](double seconds, double& at, const std::function<void()>& each = {})
		{
			while (at + 1e-9 < seconds)
			{
				Render(kBlock);
				at += static_cast<double>(kBlock) / kRate;
				if (each)
					each();
			}
		};

		// Beats and markers from the cursor, in order, with FMOD's numbering (bars and beats from 1).
		auto instance = AudioEventInstance::Create(a);
		CHECK(instance && instance->EnableTimelineNotifications() && instance->Start());
		double at = 0.0;
		renderTo(1.1, at);
		auto events = instance->DrainTimelineNotifications();
		CHECK(events.size() == 3);
		for (int i = 0; i < 3; ++i)
		{
			CHECK(events[i].IsBeat && events[i].Sequence == static_cast<uint64_t>(i + 1) && events[i].Bar == 1 && events[i].Beat == i + 1);
			CHECK(events[i].Position == i * 500 && events[i].Tempo == 120.0f && events[i].TimeSignatureUpper == 4 && events[i].TimeSignatureLower == 4);
		}
		renderTo(1.3, at);
		events = instance->DrainTimelineNotifications();
		CHECK(events.size() == 1 && !events[0].IsBeat && events[0].Marker == "Section:Chorus" && events[0].Position == 1250 && events[0].Sequence == 4);
		// The sequence counts events before they are drained (a transition request between drains).
		renderTo(1.6, at);
		CHECK(instance->GetTimelineSequence() == 5);
		events = instance->DrainTimelineNotifications();
		CHECK(events.size() == 1 && events[0].Sequence == 5 && events[0].Beat == 4);
		// Through the loop: bar 2, the Hit marker after its beat, then bar 1 again, with no beat at 4 s.
		renderTo(4.1, at);
		events = instance->DrainTimelineNotifications();
		CHECK(events.size() == 6);
		CHECK(events[0].Bar == 2 && events[0].Beat == 1 && events[0].Position == 2000);
		CHECK(events[2].IsBeat && events[2].Position == 3000 && !events[3].IsBeat && events[3].Marker == "Hit");
		CHECK(events[5].IsBeat && events[5].Bar == 1 && events[5].Beat == 1 && events[5].Position == 0);
		instance->Stop(false);

		// A loop region wraps the timeline to the region's start: 1-3 s loops, so after 3 s the next
		// beat is the one at 1 s (bar 1, beat 3), not 0 s.
		const auto region = AddSound(WriteTone("music-region.wav", 500.0, 4.0), [](Sound& s)
		{
			s.Nodes[0].Loop = true;
			s.Nodes[0].HasLoopRegion = true;
			s.Nodes[0].LoopStart = 1.0f;
			s.Nodes[0].LoopEnd = 3.0f;
			s.Spatial.Enabled = false;
			s.Tempo = { 120.0f, 4, 4, 0.0f };
		});
		auto looped = AudioEventInstance::Create(region);
		CHECK(looped && looped->EnableTimelineNotifications() && looped->Start());
		at = 0.0;
		renderTo(2.9, at);
		looped->DrainTimelineNotifications();
		renderTo(3.1, at);
		events = looped->DrainTimelineNotifications();
		CHECK(events.size() == 1 && events[0].Position == 1000 && events[0].Bar == 1 && events[0].Beat == 3);
		looped->Stop(false);

		// The music director: a NextBar transition switches beds on the bar line, not before.
		MusicDirector director;
		std::vector<std::string> markers;
		director.SetMarkerCallback([&](const std::string& marker) { markers.push_back(marker); });
		CHECK(director.Play(a));
		const auto update = [&] { director.Update(false); };
		at = 0.0;
		renderTo(0.7, at, update);
		CHECK(director.GetReference() == a && director.GetCurrentBar() == 1 && director.GetCurrentBeat() == 2);
		CHECK(director.QueueTransition(b, MusicSync::NextBar));
		renderTo(1.95, at, update);
		CHECK(director.GetReference() == a);
		CHECK(markers.size() == 1 && markers[0] == "Section:Chorus");
		renderTo(2.05, at, update);
		CHECK(director.GetReference() == b);
		director.Stop(false);
		director.Clear();

		// Sample-accurate: the next bed's first sample lands on the bar line, scheduled on the audio
		// clock, whenever the main thread notices. The first bed is silent and the next a sine from 0, so
		// its first nonzero sample is its second: the plan's bound of 1 sample allows it at most 2 frames
		// after the boundary (measured: 2).
		const auto silent = AddSound(WriteTone("music-silent.wav", 500.0, 4.0, 0.0), [](Sound& s)
		{
			s.Nodes[0].Loop = true;
			s.Spatial.Enabled = false;
			s.Tempo = { 120.0f, 4, 4, 0.0f };
		});
		MusicDirector precise;
		CHECK(precise.Play(silent));
		std::vector<float> recorded;
		const auto record = [&](double seconds)
		{
			while (recorded.size() / 2 < static_cast<size_t>(seconds * kRate))
			{
				const auto pcm = Render(kBlock);
				recorded.insert(recorded.end(), pcm.begin(), pcm.end());
				precise.Update(false);
			}
		};
		record(0.7);
		CHECK(precise.QueueTransition(b, MusicSync::NextBar));
		record(2.2);
		CHECK(precise.GetReference() == b);
		size_t first = 0;
		while (first < recorded.size() / 2 && std::abs(recorded[first * 2]) < 1e-6f)
			++first;
		const auto target = static_cast<size_t>(2.0 * kRate);
		CHECK(first >= target && first <= target + 2);
		precise.Clear();
		AudioEngine::Shutdown();
		std::puts("PASS: music timeline: tempo map YAML, beats and markers from the cursor, sequence before drain, loop wrap, NextBar transition on the bar line, sample-accurate switch");
	}

	void TestDialogueClips()
	{
		// A translation's clip saves with the table and loads back.
		DialogueTable table;
		DialogueLine line;
		line.Event.Sound = 77;
		line.Translations["en"] = { "Hello", "Ana", "", 1234 };
		line.Translations["fr"] = { "Bonjour", "Ana", "hello_fr", 0 };
		table.Lines["greeting"] = line;
		DialogueTable loaded;
		CHECK(loaded.FromYAML(table.ToYAML()));
		CHECK(loaded.Lines["greeting"].Translations["en"].Clip == 1234 && loaded.Lines["greeting"].Translations["fr"].Clip == 0
			&& loaded.Lines["greeting"].Translations["fr"].AudioKey == "hello_fr");

		// A clip override replaces the line Sound's clip: its level, and its length for the subtitle.
		Start();
		const auto placeholder = AddSound(WriteTone("line-placeholder.wav", 500.0, 0.5, 0.5), [](Sound& s) { s.Spatial.Enabled = false; });
		const AssetHandle recording = AddClip(WriteTone("line-en.wav", 500.0, 1.25, 0.25));
		auto plain = AudioEventInstance::Create(placeholder);
		CHECK(plain->MonitorPlayback() && plain->Start());
		const double placeholderLevel = Measure().Left;
		CHECK_NEAR(plain->GetPlaybackStatus().Duration, 0.5, 1e-3);
		plain->Stop(false);
		auto spoken = AudioEventInstance::Create(placeholder);
		CHECK(spoken->MonitorPlayback() && spoken->SetClipOverride(recording) && spoken->Start());
		CHECK_NEAR(Measure().Left / placeholderLevel, 0.5, 2e-3);
		const auto status = spoken->GetPlaybackStatus();
		CHECK(status.Started && status.SoundStarted);
		CHECK_NEAR(status.Duration, 1.25, 1e-3);
		CHECK(!spoken->SetClipOverride(recording));                   // only before Start
		spoken->Stop(false);
		auto missing = AudioEventInstance::Create(placeholder);
		CHECK(!missing->SetClipOverride(424242) && !missing->SetProgrammerSound("hello_fr"));
		AudioEngine::Shutdown();
		std::puts("PASS: dialogue clips: table YAML, clip override level and subtitle length, before-start rule, missing clip");
	}

	void TestPackedClips()
	{
		// A clip inside a pack file plays like its loose file, decoded from the pack (and streamed from
		// it when the Sound streams), reports its length, and is registered again after a restart.
		const auto file = WriteTone("packed.wav", 500.0, 0.75);
		const uint64_t handle = s_NextHandle++;
		s_PackedClips[handle] = PackFile(file, "packed.lap");
		const glm::vec3 front{ 0.0f, 0.0f, -1.0f };
		const auto loose = AddSound(file, [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; });
		const auto decoded = AddSound({}, [handle](Sound& s) { s.Nodes[0].Clip = handle; s.Nodes[0].Loop = true; s.Spatial.Enabled = false; });
		const auto streamed = AddSound({}, [handle](Sound& s) { s.Nodes[0].Clip = handle; s.Nodes[0].Loop = true; s.Spatial.Enabled = false; s.Stream = true; });
		for (int run = 0; run < 2; ++run)                          // twice: shutdown unregisters, a restart registers again
		{
			Start();
			const double expected = PlayAt(loose, front).Left;
			CHECK(expected > 0.3);
			CHECK_NEAR(PlayAt(decoded, front).Left / expected, 1.0, 1e-4);
			CHECK_NEAR(PlayAt(streamed, front).Left / expected, 1.0, 1e-4);
			auto instance = AudioEventInstance::Create(streamed);
			CHECK(instance && instance->GetLength() == 750);
			AudioEngine::Shutdown();
		}
		s_PackedClips[handle].Offset = 1;                           // inside the junk: not audio
		Start();
		auto broken = AudioEventInstance::Create(decoded);
		CHECK(broken && !broken->Start());
		AudioEngine::Shutdown();
		s_PackedClips.clear();
		std::puts("PASS: packed clips: decoded and streamed from the pack like the file, length, re-registered after restart, bad data refused");
	}

	void TestSoundBanks()
	{
		// Banks load their clips (decoded off the main thread), count their loads, share clips, and free
		// a clip once no loaded bank and no playing voice holds it.
		const auto x = AddSound(WriteTone("bank-x.wav", 300.0, 1.0), [](Sound& s) { s.Spatial.Enabled = false; });
		const auto y = AddSound(WriteTone("bank-y.wav", 400.0, 1.0), [](Sound& s) { s.Spatial.Enabled = false; });
		const auto z = AddSound(WriteTone("bank-z.wav", 500.0, 1.0), [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; });
		const auto streamed = AddSound(WriteTone("bank-stream.wav", 600.0, 1.0), [](Sound& s) { s.Stream = true; s.Spatial.Enabled = false; });
		const AssetHandle a = AddBank({ x, y }), b = AddBank({ y, z, streamed });
		constexpr uint64_t kClip = 48000 * sizeof(float);   // one second of mono, decoded to float
		Start();
		CHECK(NativeInternal::GetClipMemoryBytes() == 0);
		CHECK(!AudioEngine::IsSoundBankLoaded(a) && !AudioEngine::LoadSoundBank(AssetHandle(424242)));

		CHECK(AudioEngine::LoadSoundBank(a) && WaitBankLoaded(a));
		CHECK(NativeInternal::GetClipMemoryBytes() == 2 * kClip);
		CHECK(AudioEngine::LoadSoundBank(b) && WaitBankLoaded(b));
		CHECK(NativeInternal::GetClipMemoryBytes() == 3 * kClip);    // y once; the streamed clip holds nothing

		CHECK(AudioEngine::LoadSoundBank(a));                         // counted: one unload keeps it
		AudioEngine::UnloadSoundBank(a);
		CHECK(AudioEngine::IsSoundBankLoaded(a) && NativeInternal::GetClipMemoryBytes() == 3 * kClip);
		AudioEngine::UnloadSoundBank(a);
		CHECK(!AudioEngine::IsSoundBankLoaded(a));
		CHECK(NativeInternal::GetClipMemoryBytes() == 2 * kClip);    // x freed, y kept by b

		auto playing = Play(z, glm::vec3(0.0f, 0.0f, -1.0f));
		CHECK(Measure().Left > 0.3);                                  // a preloaded clip plays at once
		AudioEngine::UnloadSoundBank(b);
		CHECK(NativeInternal::GetClipMemoryBytes() == kClip);         // y freed; z stays while it plays
		CHECK(Measure().Left > 0.3);
		playing->Stop(false);
		CHECK(NativeInternal::GetClipMemoryBytes() == 0);             // z freed with its last voice

		// A Sound in no loaded bank loads when it plays and stays until unused clips are swept.
		CHECK(PlayAt(x, glm::vec3(0.0f, 0.0f, -1.0f)).Left > 0.3);
		CHECK(NativeInternal::GetClipMemoryBytes() == kClip);
		AudioEngine::ReleaseUnusedClips();
		CHECK(NativeInternal::GetClipMemoryBytes() == 0);

		const AssetHandle resident = AddBank({ y });
		s_Banks[static_cast<uint64_t>(resident)]->Resident = true;
		CHECK(AudioEngine::LoadSoundBank(resident) && WaitBankLoaded(resident));
		AudioEngine::UnloadSoundBank(resident);                       // ignored: resident until shutdown
		CHECK(AudioEngine::IsSoundBankLoaded(resident) && NativeInternal::GetClipMemoryBytes() == kClip);

		CHECK(AudioEngine::LoadSoundBank(a) && WaitBankLoaded(a));
		AudioEngine::Shutdown();                                      // frees everything, banks included
		Start();
		CHECK(!AudioEngine::IsSoundBankLoaded(a) && NativeInternal::GetClipMemoryBytes() == 0);
		AudioEngine::Shutdown();
		std::puts("PASS: sound banks: counted loads, shared clips, freed when no bank or voice holds them, on-demand clips swept");
	}

	void TestPackStream()
	{
		// Five minutes of music streamed from inside a pack: it plays like the loose file, seeks deep
		// into the clip, and holds stream pages, not the track.
		const auto file = WriteLongTone("long.wav", 440.0, 300.0, 0.25);
		const uint64_t handle = s_NextHandle++;
		s_PackedClips[handle] = PackFile(file, "long.lap");
		const auto loose = AddSound(file, [](Sound& s) { s.Stream = true; s.Spatial.Enabled = false; });
		const auto packed = AddSound({}, [handle](Sound& s) { s.Nodes[0].Clip = handle; s.Stream = true; s.Spatial.Enabled = false; });
		const glm::vec3 front{ 0.0f, 0.0f, -1.0f };
		Start();
		const double expected = PlayAt(loose, front).Left;
		CHECK(expected > 0.1);
		auto instance = Play(packed, front);
		CHECK(instance->GetLength() == 300000);
		CHECK_NEAR(Measure().Left / expected, 1.0, 1e-4);
		CHECK(NativeInternal::GetClipMemoryBytes() < 1024 * 1024);
		instance->SetTimelinePosition(295000);                        // near the end, past every early page
		CHECK_NEAR(Measure().Left / expected, 1.0, 1e-4);
		instance->Stop(false);
		AudioEngine::Shutdown();
		s_PackedClips.erase(handle);
		std::puts("PASS: pack stream: a 5-minute clip streams from the pack like the file, seeks, under 1 MB of clip memory");
	}

	void TestPackStreamStress()
	{
		// Sixteen streams from one pack while another thread reads the same file: every stream reads
		// its own bytes (the sum is sixteen times one stream, in phase).
		const auto file = WriteLongTone("stress.wav", 440.0, 10.0, 0.02);
		const uint64_t handle = s_NextHandle++;
		s_PackedClips[handle] = PackFile(file, "stress.lap");
		const auto packed = AddSound({}, [handle](Sound& s) { s.Nodes[0].Clip = handle; s.Stream = true; s.Spatial.Enabled = false; });
		const glm::vec3 front{ 0.0f, 0.0f, -1.0f };
		Start();
		const double single = PlayAt(packed, front).Left;
		CHECK(single > 0.005);
		std::atomic<bool> reading{ true };
		std::thread reader([&]
		{
			std::vector<char> buffer(64 * 1024);
			while (reading)
			{
				std::ifstream in(s_PackedClips[handle].Pack, std::ios::binary);
				while (reading && in.read(buffer.data(), buffer.size())) {}
			}
		});
		std::vector<Ref<AudioEventInstance>> instances;
		for (int i = 0; i < 16; ++i)
		{
			instances.push_back(AudioEventInstance::Create(packed));
			instances.back()->Set3DAttributes(front, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		}
		for (auto& instance : instances)
			CHECK(instance->StartAt(ma_engine_get_time_in_pcm_frames(NativeInternal::GetEngine()) + kRate / 10));
		const double all = Measure().Left;
		reading = false;
		reader.join();
		CHECK_NEAR(all / single, 16.0, 16.0 * 1e-3);
		for (auto& instance : instances)
			instance->Stop(false);
		AudioEngine::Shutdown();
		s_PackedClips.erase(handle);
		std::puts("PASS: pack stream stress: 16 streams from one pack beside a concurrent reader, each reading its own bytes");
	}

	void TestVoiceBudget()
	{
		s_Performance.RealVoices = 2;
		Start();
		const auto tone = AddSound(WriteTone("budget.wav", 500.0, 1.0), [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; });
		const glm::vec3 front{ 0.0f, 0.0f, -1.0f };
		auto single = Play(tone, front);
		const double one = Measure().Left;
		single->Stop(false);
		// Three in phase over a budget of two: the lowest priority (highest number) goes virtual.
		std::array<Ref<AudioEventInstance>, 3> voices;
		const int priorities[] = { 10, 20, 200 };
		for (size_t i = 0; i < voices.size(); ++i)
		{
			voices[i] = AudioEventInstance::Create(tone);
			CHECK(voices[i]->SetPriority(priorities[i]) && voices[i]->Start());
		}
		AudioPerformance::Update();
		CHECK(AudioPerformance::GetStats().VirtualVoices == 1 && AudioPerformance::GetStats().RealVoices == 2 && AudioPerformance::GetStats().VoicesExceeded);
		CHECK(!voices[0]->IsVirtual() && !voices[1]->IsVirtual() && voices[2]->IsVirtual() && voices[2]->IsPlaying());
		CHECK_NEAR(Measure().Left / one, 2.0, 0.01);                // two mixing, not three
		// Room frees up: it resumes where its time has reached, still in step with the others.
		voices[0]->Stop(false);
		AudioPerformance::Update();
		CHECK(!voices[2]->IsVirtual() && AudioPerformance::GetStats().VirtualVoices == 0);
		CHECK(std::abs(voices[2]->GetTimelinePosition() - voices[1]->GetTimelinePosition()) <= 15);
		CHECK_NEAR(Measure().Left / one, 2.0, 0.02);
		voices[1]->Stop(false);
		voices[2]->Stop(false);

		// A one-shot whose time runs out while virtual has ended when it would have.
		s_Performance.RealVoices = 1;
		AudioEngine::Shutdown();
		Start();
		auto bed = AudioEventInstance::Create(tone);
		CHECK(bed->SetPriority(0) && bed->Start());
		const auto hit = AddSound(WriteTone("budget-hit.wav", 500.0, 0.2), [](Sound& s) { s.Spatial.Enabled = false; });
		auto shot = AudioEventInstance::Create(hit);
		CHECK(shot->SetPriority(255) && shot->Start());
		AudioPerformance::Update();
		CHECK(shot->IsVirtual());
		Render(kRate / 2);
		bed->Stop(false);
		AudioPerformance::Update();
		CHECK(!shot->IsPlaying() && !shot->IsVirtual());
		AudioEngine::Shutdown();
		s_Performance = {};
		std::puts("PASS: voice budget: lowest priority virtual, silent while virtual, resumes in step, a one-shot that ran out has ended");
	}

	void TestSampleProjectSounds()
	{
		// The sample project's Sounds load, validate, name registered clips that exist, and play.
		const std::filesystem::path assets = LUX_SAMPLE_ASSETS;
		const auto readText = [](const std::filesystem::path& path)
		{
			std::ifstream in(path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		};
		const auto registry = Yaml::Load(readText(assets / "AssetRegistry.lzr"));
		std::map<uint64_t, std::pair<std::string, std::string>> registered; // handle -> path, type
		for (const auto& entry : registry["Assets"])
			registered[entry["Handle"].as<uint64_t>()] = { entry["FilePath"].as<std::string>(), entry["Type"].as<std::string>() };
		Start();
		for (const char* name : { "Audio/Sounds/Fart.lsound", "Audio/Sounds/Music.lsound" })
		{
			auto sound = Ref<Sound>::Create();
			std::string error;
			CHECK(sound->FromYAML(readText(assets / name)) && sound->Validate(&error));
			const auto own = std::find_if(registered.begin(), registered.end(), [&](const auto& item) { return item.second.first == name; });
			CHECK(own != registered.end() && own->second.second == "Sound");
			sound->Handle = own->first;
			for (const AssetHandle clip : sound->GetClips())
			{
				const auto entry = registered.find(static_cast<uint64_t>(clip));
				CHECK(entry != registered.end() && entry->second.second == "Audio" && std::filesystem::is_regular_file(assets / entry->second.first));
				s_Clips[static_cast<uint64_t>(clip)] = assets / entry->second.first;
			}
			s_Sounds[own->first] = sound;
			auto instance = Play(std::to_string(own->first), { 0.0f, 0.0f, -1.0f });
			CHECK(instance->IsPlaying());
			const auto pcm = Render(kRate / 4);
			CHECK(Rms(pcm, 0) > 1e-3);
			instance->Stop(false);
		}
		AudioEngine::Shutdown();
		std::puts("PASS: sample project Sounds: Fart and Music load, validate, use registered clips that exist, and play");
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
		const auto loop = AddSound(WriteTone("loop.wav", 500.0, 0.5), [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; });
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
			&& instance->EnableTimelineNotifications() && !instance->IsVirtual());
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

	// Starts an instance and returns the longest clip the start chose: the tests tell children apart
	// by their clips' lengths.
	int Chosen(AudioEventInstance& instance)
	{
		CHECK(instance.Start());
		return static_cast<int>(std::lround(instance.GetPlaybackStatus().Duration * 1000.0f));
	}

	Ref<AudioEventInstance> Monitored(const std::string& reference)
	{
		auto instance = AudioEventInstance::Create(reference);
		CHECK(instance && instance->MonitorPlayback());
		return instance;
	}

	void TestContainers()
	{
		Start();
		NativeInternal::SeedRandom(7);
		const AssetHandle short_ = AddClip(WriteTone("c100.wav", 500.0, 0.1));
		const AssetHandle medium = AddClip(WriteTone("c200.wav", 500.0, 0.2));
		const AssetHandle long_ = AddClip(WriteTone("c300.wav", 500.0, 0.3));
		const auto flat = [](Sound& s) { s.Spatial.Enabled = false; };

		// Random: by weight, and a weight of 0 is never chosen.
		const auto weighted = AddSound({}, [&](Sound& s) { flat(s); Container(s, SoundNodeType::Random,
			{ ClipNode(short_, [](SoundNode& n) { n.Weight = 3.0f; }), ClipNode(medium), ClipNode(long_, [](SoundNode& n) { n.Weight = 0.0f; }) }); });
		auto random = Monitored(weighted);
		std::map<int, int> counts;
		for (int i = 0; i < 400; ++i)
			++counts[Chosen(*random)];
		CHECK(counts[300] == 0 && counts[100] + counts[200] == 400);
		CHECK(counts[100] > 260 && counts[100] < 340); // 300 expected; 4 standard deviations is 35

		// Avoid repeat: never the same child twice in a row, and every child still plays.
		const auto varied = AddSound({}, [&](Sound& s) { flat(s); Container(s, SoundNodeType::Random, { ClipNode(short_), ClipNode(medium), ClipNode(long_) });
			s.Nodes[0].AvoidRepeat = true; });
		auto avoiding = Monitored(varied);
		counts.clear();
		int previous = 0;
		for (int i = 0; i < 90; ++i)
		{
			const int chosen = Chosen(*avoiding);
			CHECK(chosen != previous);
			previous = chosen;
			++counts[chosen];
		}
		CHECK(counts.size() == 3 && counts[100] > 10 && counts[200] > 10 && counts[300] > 10);

		// Sequence: each start plays the next child, and every instance of the Sound shares the step.
		const auto stepped = AddSound({}, [&](Sound& s) { flat(s); Container(s, SoundNodeType::Sequence, { ClipNode(short_), ClipNode(medium), ClipNode(long_) }); });
		auto first = Monitored(stepped), second = Monitored(stepped);
		CHECK(Chosen(*first) == 100 && Chosen(*first) == 200 && Chosen(*second) == 300 && Chosen(*first) == 100);
		CHECK(first->GetLength() == 300); // the longest clip it can play, before and after starting

		// Switch: the labelled parameter's value picks the child, case-insensitively by name.
		const auto switched = AddSound({}, [&](Sound& s) { flat(s);
			s.Parameters = { { "Surface", 0.0f, 2.0f, 1.0f, { "Grass", "Stone", "Water" } } };
			Container(s, SoundNodeType::Switch, { ClipNode(short_), ClipNode(medium), ClipNode(long_) });
			s.Nodes[0].SwitchParameter = "Surface"; });
		auto surface = Monitored(switched);
		CHECK(Chosen(*surface) == 200); // the default label
		CHECK(surface->SetParameterLabel("surface", "Water") && Chosen(*surface) == 300 && surface->GetParameter("SURFACE") == 2.0f);
		CHECK(surface->SetParameter("Surface", 7.0f) && surface->GetParameter("Surface") == 2.0f); // clamped
		CHECK(surface->SetParameter("Surface", 0.4f) && surface->GetParameter("Surface") == 0.0f && Chosen(*surface) == 100); // rounded
		CHECK(!surface->SetParameterLabel("Surface", "Lava") && !surface->SetParameter("Speed", 1.0f));
		AudioEventParameterInfo info;
		CHECK(AudioEngine::GetEventParameter(switched, "SURFACE", info) && info.Name == "Surface" && info.Labeled && info.Discrete
			&& info.Minimum == 0.0f && info.Maximum == 2.0f && info.Default == 1.0f);
		std::string label, error;
		CHECK(AudioEngine::GetEventParameterLabel(switched, "Surface", 2, label, error) && label == "Water");
		CHECK(!AudioEngine::GetEventParameterLabel(switched, "Surface", 3, label, error) && !error.empty());
		CHECK(!AudioEngine::GetEventParameter(switched, "Speed", info));

		// Let the one-shots above finish before measuring levels.
		Render(kRate / 2);
		CHECK(NativeInternal::CountPlayingVoices() > 0);
		AudioEngine::Update();
		CHECK(NativeInternal::CountPlayingVoices() == 0);

		// Layer: every child together. Two copies of one tone in phase are twice as loud as one.
		const AssetHandle loop = AddClip(WriteTone("layer.wav", 500.0, 0.5));
		const auto looped = [](SoundNode& n) { n.Loop = true; };
		const auto single = AddSound({}, [&](Sound& s) { flat(s); Container(s, SoundNodeType::Layer, { ClipNode(loop, looped) }); });
		const auto doubled = AddSound({}, [&](Sound& s) { flat(s); Container(s, SoundNodeType::Layer, { ClipNode(loop, looped), ClipNode(loop, looped) }); });
		const double one = PlayAt(single, glm::vec3(0.0f)).Left;
		CHECK(one > 0.3);
		CHECK_NEAR(PlayAt(doubled, glm::vec3(0.0f)).Left / one, 2.0, 1e-4);

		// Node volumes multiply down the path.
		const auto nested = AddSound({}, [&](Sound& s) { flat(s); s.Volume = 0.5f;
			Container(s, SoundNodeType::Layer, { ClipNode(loop, [](SoundNode& n) { n.Loop = true; n.Volume = 0.5f; }) }); s.Nodes[0].Volume = 0.8f; });
		CHECK_NEAR(PlayAt(nested, glm::vec3(0.0f)).Left / one, 0.2, 1e-4);

		// What cannot play reports and refuses to start: an unassigned clip, or no child with weight.
		const auto empty = AddSound({}, flat);
		CHECK(!AudioEventInstance::Create(empty)->Start());
		const auto weightless = AddSound({}, [&](Sound& s) { flat(s); Container(s, SoundNodeType::Random, { ClipNode(short_, [](SoundNode& n) { n.Weight = 0.0f; }) }); });
		CHECK(!AudioEventInstance::Create(weightless)->Start());
		AudioEngine::Shutdown();
		std::puts("PASS: random by weight, avoid-repeat, step sequence, switch by label, layers, node volumes, unplayable trees refused");
	}

	void TestCurvesAndVariation()
	{
		Start();
		NativeInternal::SeedRandom(11);
		const auto tone = WriteTone("curve.wav", 500.0, 1.0);
		const auto distance = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false;
			s.Parameters = { { "Distance", 0.0f, 1.0f, 0.0f, {} } };
			s.Nodes[0].Curves.push_back({ "Distance", SoundCurveTarget::Volume, { { 0.0f, 1.0f }, { 1.0f, 0.25f } } }); });
		auto instance = Play(distance, glm::vec3(0.0f));
		const double full = Measure().Left;
		CHECK(instance->SetParameter("distance", 1.0f)); // applies to a playing voice
		CHECK_NEAR(Measure().Left / full, 0.25, 1e-4);
		CHECK(instance->SetParameter("Distance", 0.5f));
		CHECK_NEAR(Measure().Left / full, 0.625, 1e-4);
		instance->Stop();

		// Filter curves: a low-pass at 5 kHz costs a 10 kHz tone about 6.4 dB, as occlusion does; a
		// high-pass at 10 kHz all but removes 100 Hz, and one at 20 Hz leaves 10 kHz alone.
		const auto filtered = [](float lowPass, float highPass)
		{
			return [=](Sound& s)
			{
				s.Nodes[0].Loop = true;
				s.Spatial.Enabled = false;
				s.Parameters = { { "Any", 0.0f, 1.0f, 0.0f, {} } };
				if (lowPass > 0.0f)
					s.Nodes[0].Curves.push_back({ "Any", SoundCurveTarget::LowPass, { { 0.0f, lowPass } } });
				if (highPass > 0.0f)
					s.Nodes[0].Curves.push_back({ "Any", SoundCurveTarget::HighPass, { { 0.0f, highPass } } });
			};
		};
		const auto high = WriteTone("curve-high.wav", 10000.0, 1.0), low = WriteTone("curve-low.wav", 100.0, 1.0);
		const double highOpen = PlayAt(AddSound(high, filtered(0.0f, 0.0f)), glm::vec3(0.0f)).Left;
		const double lowOpen = PlayAt(AddSound(low, filtered(0.0f, 0.0f)), glm::vec3(0.0f)).Left;
		CHECK_NEAR(PlayAt(AddSound(high, filtered(5000.0f, 0.0f)), glm::vec3(0.0f)).Left / highOpen, 0.48, 0.01);
		CHECK(PlayAt(AddSound(low, filtered(0.0f, 10000.0f)), glm::vec3(0.0f)).Left / lowOpen < 0.05);
		CHECK_NEAR(PlayAt(AddSound(high, filtered(0.0f, 20.0f)), glm::vec3(0.0f)).Left / highOpen, 1.0, 0.01);

		// A pitch curve: at 2x the 0.1 s clip ends within 0.06 s.
		const auto shot = AddSound(WriteTone("curve-shot.wav", 500.0, 0.1), [](Sound& s) { s.Spatial.Enabled = false;
			s.Parameters = { { "Rate", 0.0f, 1.0f, 0.0f, {} } };
			s.Nodes[0].Curves.push_back({ "Rate", SoundCurveTarget::Pitch, { { 0.0f, 1.0f }, { 1.0f, 2.0f } } }); });
		auto fast = AudioEventInstance::Create(shot);
		CHECK(fast->SetParameter("Rate", 1.0f) && fast->Start());
		Render(kRate * 6 / 100);
		CHECK(!fast->IsPlaying());

		// Volume variation lowers each start by up to its decibels, and differs between starts.
		const auto varied = AddSound(tone, [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; s.Nodes[0].VolumeVariation = 12.0f; });
		double quietest = 1.0, loudest = 0.0;
		for (int i = 0; i < 16; ++i)
		{
			const double ratio = PlayAt(varied, glm::vec3(0.0f)).Left / full;
			CHECK(ratio > 0.25 - 1e-4 && ratio < 1.0 + 1e-4);
			quietest = std::min(quietest, ratio);
			loudest = std::max(loudest, ratio);
		}
		CHECK(loudest / quietest > 1.5);
		AudioEngine::Shutdown();
		std::puts("PASS: volume, pitch, low-pass and high-pass curves, live parameter changes, volume variation");
	}

	void TestLimits()
	{
		Start();
		const auto tone = WriteTone("limit.wav", 500.0, 1.0);
		const auto limited = [](SoundStealPolicy steal)
		{
			return [=](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; s.InstanceLimit = 2; s.Steal = steal; };
		};

		const auto oldest = AddSound(tone, limited(SoundStealPolicy::Oldest));
		auto a = Monitored(oldest), b = Monitored(oldest), c = Monitored(oldest);
		CHECK(a->Start());
		Render(kBlock);
		CHECK(b->Start());
		Render(kBlock);
		CHECK(c->Start() && !a->IsPlaying() && a->GetPlaybackStatus().Stopped && b->IsPlaying() && c->IsPlaying());
		CHECK(b->Start() && b->IsPlaying() && c->IsPlaying()); // restarting a playing instance takes no new slot
		for (auto* instance : { a.Raw(), b.Raw(), c.Raw() })
			instance->Stop();

		const auto quietest = AddSound(tone, limited(SoundStealPolicy::Quietest));
		a = Monitored(quietest), b = Monitored(quietest), c = Monitored(quietest);
		b->SetVolume(0.2f);
		CHECK(a->Start() && b->Start() && c->Start() && a->IsPlaying() && !b->IsPlaying() && c->IsPlaying());
		for (auto* instance : { a.Raw(), c.Raw() })
			instance->Stop();

		const auto refuse = AddSound(tone, limited(SoundStealPolicy::None));
		a = Monitored(refuse), b = Monitored(refuse), c = Monitored(refuse);
		CHECK(a->Start() && b->Start() && !c->Start() && a->IsPlaying() && b->IsPlaying() && !c->IsPlaying());
		a->Stop();
		CHECK(c->Start()); // a slot is free again
		b->Stop();
		c->Stop();

		// Cooldown: a start sooner than the cooldown after the last start is refused. It is measured in
		// real time, which keeps running while nothing plays.
		const auto cooled = AddSound(tone, [](Sound& s) { s.Spatial.Enabled = false; s.Cooldown = 0.2f; });
		a = Monitored(cooled), b = Monitored(cooled);
		CHECK(a->Start() && !b->Start());
		a->Stop();
		CHECK(!b->Start());
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		CHECK(b->Start());
		AudioEngine::Shutdown();
		std::puts("PASS: instance limits with oldest, quietest and no stealing; cooldown");
	}

	void TestStreaming()
	{
		Start();
		// Longer than miniaudio's one-second stream pages, so playing it needs the streaming thread
		// to read pages while the voice plays.
		const auto tone = WriteTone("stream.wav", 500.0, 2.5);
		const auto streamed = AddSound(tone, [](Sound& s) { s.Spatial.Enabled = false; s.Stream = true; });
		auto instance = Monitored(streamed);
		CHECK(instance->GetLength() == 2500 && instance->Start());
		const uint64_t blocks = kRate * 5 / 2 / kBlock;
		for (uint64_t block = 0; block < blocks; ++block)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1)); // a tenth of real time
			CHECK(Rms(Render(kBlock), 0) > 0.3);
		}
		Render(kBlock * 2);
		AudioEngine::Update();
		CHECK(!instance->IsPlaying() && instance->GetPlaybackStatus().Stopped);

		// Every frame of a clip plays, streamed or decoded: the last block of the 2.5 s clip is full.
		for (const bool stream : { true, false })
		{
			const auto whole = AddSound(tone, [=](Sound& s) { s.Spatial.Enabled = false; s.Stream = stream; });
			auto voice = Play(whole, glm::vec3(0.0f));
			for (uint64_t block = 0; block + 1 < blocks; ++block)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1)); // time for the streaming thread
				Render(kBlock);
			}
			const auto tail = Render(kBlock * 2);
			int last = -1;
			for (int frame = 0; frame < static_cast<int>(kBlock * 2); ++frame)
				last = tail[frame * 2] != 0.0f ? frame : last;
			CHECK(last == static_cast<int>(kBlock) - 1); // the clip's last frame is the block's last
		}

		// Seeking a stream, and shutting down with streams still open.
		const auto looped = AddSound(tone, [](Sound& s) { s.Spatial.Enabled = false; s.Stream = true; s.Nodes[0].Loop = true; });
		auto first = Play(looped, glm::vec3(0.0f)), second = Play(looped, glm::vec3(0.0f));
		first->SetTimelinePosition(2000);
		Render(kBlock); // the voice applies the seek, and the streaming thread loads the page
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		Render(kBlock);
		const int position = first->GetTimelinePosition();
		CHECK(position >= 2010 && position <= 2020);
		AudioEngine::Shutdown();
		CHECK(!first->IsValid() && !second->IsValid());
		std::puts("PASS: streamed Sounds play through page loads, seek, and shut down cleanly");
	}

	// Level of the mix over the next 100 ms (no settle: one-shots are short).
	double Level100()
	{
		return Rms(Render(kRate / 10), 0);
	}

	// PhysicsAudioSystem against Sounds:
	// a Surface switch picks the clip, and Impulse, Speed and Weight curves set the volume.
	void TestPhysicsAudio()
	{
		Start();
		// The surface labels are the acoustic material names, so a material's index is its label.
		SoundParameter surface{ "Surface", 0.0f, static_cast<float>(AcousticMaterialCount - 1), 0.0f, {} };
		for (const char* name : AcousticMaterialNames)
			surface.Labels.push_back(name);
		const AssetHandle shortClip = AddClip(WriteTone("surface-short.wav", 500.0, 0.2));
		const AssetHandle longClip = AddClip(WriteTone("surface-long.wav", 500.0, 0.6));
		const AssetHandle loopClip = AddClip(WriteTone("surface-loop.wav", 500.0, 0.5));
		const auto bySurface = [&](Sound& s, bool loop)
		{
			s.Spatial.Enabled = false;
			s.Parameters.push_back(surface);
			std::vector<SoundNode> children;
			for (size_t material = 0; material < AcousticMaterialCount; ++material)
			{
				// Metal and Grass are told apart by length: 0.2 s and 0.6 s.
				const AssetHandle clip = loop ? loopClip : material == static_cast<size_t>(AcousticMaterial::Grass) ? longClip : shortClip;
				children.push_back(ClipNode(clip, [=](SoundNode& n) { n.Loop = loop; }));
			}
			Container(s, SoundNodeType::Switch, std::move(children));
			s.Nodes[0].SwitchParameter = "Surface";
		};
		const auto hit = AddSound({}, [&](Sound& s)
		{
			bySurface(s, false);
			s.Parameters.push_back({ "Impulse", 0.0f, 10.0f, 0.0f, {} });
			s.Parameters.push_back({ "Speed", 0.0f, 10.0f, 0.0f, {} });
			s.Parameters.push_back({ "Weight", 0.0f, 100.0f, 0.0f, {} });
			s.Nodes[0].Curves.push_back({ "Impulse", SoundCurveTarget::Volume, { { 0.0f, 1.0f }, { 2.0f, 0.5f }, { 4.0f, 1.0f } } });
			s.Nodes[0].Curves.push_back({ "Weight", SoundCurveTarget::Volume, { { 0.0f, 1.0f }, { 80.0f, 0.8f } } });
			s.Nodes[0].Curves.push_back({ "Speed", SoundCurveTarget::Volume, { { 0.0f, 1.0f }, { 4.0f, 0.5f } } });
		});
		const auto motion = AddSound({}, [&](Sound& s)
		{
			bySurface(s, true);
			s.Parameters.push_back({ "Speed", 0.0f, 10.0f, 0.0f, {} });
			s.Nodes[0].Curves.push_back({ "Speed", SoundCurveTarget::Volume, { { 0.0f, 0.0f }, { 6.0f, 1.0f } } });
		});
		const double full = PlayAt(AddSound(WriteTone("surface-ref.wav", 500.0, 1.0), [](Sound& s) { s.Nodes[0].Loop = true; s.Spatial.Enabled = false; }),
			glm::vec3(0.0f)).Left;

		AudioSurfaceTable table;
		PhysicsAudioSystem system;
		PhysicsAudioContact contact;
		contact.Key = { 1, 2, 3, 4 };
		contact.Owner = 10;
		contact.Other = 20;
		contact.Impulse = 2.0f;
		contact.SlipSpeed = 3.0f;
		contact.Material = AcousticMaterial::Metal;
		contact.Sounds.Impact = AudioEventRef::FromReference(hit);
		contact.Sounds.Scrape = AudioEventRef::FromReference(motion);
		system.Contact(contact, table);
		CHECK(NativeInternal::CountPlayingVoices() == 1);
		CHECK_NEAR(Level100() / full, 0.5, 1e-3); // the Impulse curve at 2
		system.Contact(contact, table); // the same manifold again: no second impact
		CHECK(NativeInternal::CountPlayingVoices() == 1);
		Render(kRate / 5);
		AudioEngine::Update();
		CHECK(NativeInternal::CountPlayingVoices() == 0); // Metal is the 0.2 s clip

		// A new manifold starts the shared scrape loop, whose Speed curve sets its volume.
		contact.Key[2] = 5;
		system.Contact(contact, table);
		system.Update(0.0f, false, table);
		CHECK(system.GetContactCount() == 2 && NativeInternal::CountPlayingVoices() == 1); // the impact is cooling down
		Render(kSettle);
		CHECK_NEAR(Level100() / full, 0.5, 1e-3);
		contact.SlipSpeed = 6.0f;
		system.Contact(contact, table);
		system.Update(0.0f, false, table);
		Render(kSettle);
		CHECK_NEAR(Level100() / full, 1.0, 1e-3);
		system.Update(1.0f, true, table); // paused: silent, and the cooldown does not advance
		CHECK(Level100() == 0.0);
		system.Update(0.4f, false, table);
		contact.Key[2] = 7;
		system.Contact(contact, table);
		CHECK(NativeInternal::CountPlayingVoices() == 2); // the cooldown passed: a second impact
		system.Remove(20);
		CHECK(system.GetContactCount() == 0);
		Render(kBlock);
		AudioEngine::Update();
		CHECK(NativeInternal::CountPlayingVoices() == 0);

		// Footsteps: Surface picks the 0.6 s Grass clip; Speed 4 and Weight 80 scale it by 0.5 x 0.8.
		CHECK(system.Footstep(10, AudioEventRef::FromReference(hit), AcousticMaterial::Grass, glm::vec3(0.0f), 4.0f, 80.0f));
		CHECK_NEAR(Level100() / full, 0.4, 1e-3);
		Render(kRate * 4 / 10);
		AudioEngine::Update();
		CHECK(NativeInternal::CountPlayingVoices() == 1);
		Render(kRate / 5);
		AudioEngine::Update();
		CHECK(NativeInternal::CountPlayingVoices() == 0);
		CHECK(!system.Footstep(10, AudioEventRef::FromReference(motion), AcousticMaterial::Grass, glm::vec3(0.0f), 4.0f, 80.0f)); // a loop
		CHECK(!system.Footstep(10, AudioEventRef::FromReference(hit), AcousticMaterial::Grass, glm::vec3(0.0f), NAN, 80.0f));
		system.Clear();
		AudioEngine::Shutdown();
		std::puts("PASS: physics audio on native Sounds: Surface switch, Impulse/Speed/Weight curves, scrape loop, cooldown, footsteps");
	}

	void TestFileInfo()
	{
		const auto info = AudioFileUtils::GetFileInfo(std::filesystem::path(LUX_NATIVE_AUDIO_FIXTURES) / "tone.wav");
		CHECK(info && info->SamplingRate == 48000 && info->NumChannels == 2 && info->BitDepth == 16 && info->FileSize == 96044);
		CHECK_NEAR(info->Duration, 0.5, 1e-9);
		const auto ogg = AudioFileUtils::GetFileInfo(std::filesystem::path(LUX_NATIVE_AUDIO_FIXTURES) / "tone.ogg");
		CHECK(ogg && ogg->SamplingRate == 48000 && ogg->NumChannels == 2);
		CHECK_NEAR(ogg->Duration, 0.5, 0.01); // Vorbis may not store its length; it is counted
		CHECK(!AudioFileUtils::GetFileInfo(s_Directory / "missing.wav"));
		// Waveform peaks: a constant-level tone peaks at its amplitude in every slice.
		const auto tone = WriteTone("waveform.wav", 500.0, 0.5, 0.5);
		const auto peaks = AudioFileUtils::GetWaveform(tone, 100);
		CHECK(peaks && peaks->size() == 100);
		for (const float peak : *peaks)
			CHECK_NEAR(peak, 0.5, 0.01);
		CHECK(!AudioFileUtils::GetWaveform(tone, 0) && !AudioFileUtils::GetWaveform(s_Directory / "missing.wav", 100));
		const auto stereo = AudioFileUtils::GetWaveform(std::filesystem::path(LUX_NATIVE_AUDIO_FIXTURES) / "tone.ogg", 16);
		CHECK(stereo && stereo->size() == 16 && *std::min_element(stereo->begin(), stereo->end()) > 0.1f);
		std::puts("PASS: audio file metadata and waveform peaks without an engine");
	}

	// Saving a Sound reaches its playing instances: a volume edit applies in place (the voice keeps
	// its cursor and its parameter values), a tree edit restarts it, and a stopped instance waits.
	void TestLiveSoundEdits()
	{
		s_Mixer.Buses = { { "bus:/SFX", 0.5f } };
		Start();
		const auto tone = WriteTone("live.wav", 500.0, 1.0);
		const auto reference = AddSound(tone, [](Sound& s)
		{
			s.Nodes[0].Loop = true;
			s.Spatial.Enabled = false;
			s.Parameters.push_back({ "Drive", 0.0f, 1.0f, 0.0f });
		});
		const Ref<Sound> asset = s_Sounds[std::stoull(reference)];
		auto instance = AudioEventInstance::Create(reference);
		CHECK(instance && instance->Start() && instance->SetParameter("Drive", 0.75f));
		const double full = Measure().Left;
		CHECK(full > 0.3);
		const int before = instance->GetTimelinePosition();
		CHECK(before > 0);

		Sound edited(*asset);
		edited.Volume = 0.5f;
		AudioEngine::ApplySoundEdit(edited);
		CHECK_NEAR(Measure().Left / full, 0.5, 1e-3);
		CHECK(instance->IsPlaying() && instance->GetTimelinePosition() > before);   // not restarted
		CHECK(instance->GetParameter("Drive") == 0.75f);
		// A bus change re-routes the playing voice.
		edited.Bus = "bus:/SFX";
		AudioEngine::ApplySoundEdit(edited);
		CHECK_NEAR(Measure().Left / full, 0.25, 1e-3);
		edited.Bus = "bus:/";
		AudioEngine::ApplySoundEdit(edited);
		CHECK_NEAR(Measure().Left / full, 0.5, 1e-3);

		// The parameter's new range holds the value; a new parameter starts at its default.
		edited.Parameters[0].Maximum = 0.5f;
		edited.Parameters.push_back({ "Extra", 0.0f, 2.0f, 1.5f });
		AudioEngine::ApplySoundEdit(edited);
		CHECK(instance->GetParameter("Drive") == 0.5f && instance->GetParameter("Extra") == 1.5f);

		// Turning the loop off changes what the voice was built from: it restarts from the top.
		Measure();
		const int playedFor = instance->GetTimelinePosition();
		edited.Nodes[0].Loop = false;
		AudioEngine::ApplySoundEdit(edited);
		CHECK(instance->IsPlaying() && instance->GetTimelinePosition() < playedFor);

		// A stopped instance keeps quiet and plays the latest edit when it starts again.
		instance->Stop(false);
		edited.Volume = 0.25f;
		AudioEngine::ApplySoundEdit(edited);
		CHECK(!instance->IsPlaying());
		edited.Nodes[0].Loop = true;
		AudioEngine::ApplySoundEdit(edited);
		CHECK(instance->Start());
		CHECK_NEAR(Measure().Left / full, 0.25, 1e-3);
		instance->Stop(false);
		instance = nullptr;
		AudioEngine::Shutdown();
		s_Mixer = {};
		std::puts("PASS: a saved Sound reaches playing instances: in place for levels and parameters, restarted for a tree change");
	}

	// Remove FMOD data: a scene with Guid-only and mixed references, a project with Studio settings,
	// a dialogue table and a registry. The scan lists each reference, and the cleaned files hold none.
	void TestLegacyCleanup()
	{
		using namespace AudioLegacyCleanup;
		const std::string scene = R"(Scene: Old
Entities:
  - Entity: 1
    TextComponent:
      TextString: 'FMOD DEMO

        EventGuid: not a key'
    AudioSourceComponent:
      PlayOnAwake: true
      EventGuid: '{755abc01-cd89-4daf-a27b-04d57b807b3d}'
      EventPath: ""
      EventBank: ""
      EventSound: 8743721474366396702
      ParameterOverrides: []
  - Entity: 2
    AudioZoneComponent:
      AmbienceEvent:
        Guid: '{0e6cd051-9f6f-4b17-95e6-f2b1b5a4cc40}'
        Path: event:/Ambience/Wind
        BankName: Master.bank
      Snapshot:
        Guid: ""
        Path: ""
        BankName: ""
        Sound: 42
    MusicDirectorComponent:
      Event:
        Guid: '{11111111-2222-3333-4444-555555555555}'
      State: Calm
)";
		std::string cleaned, error;
		std::vector<Finding> findings;
		CHECK(CleanText(scene, FileKind::Asset, cleaned, findings, error));
		CHECK(findings.size() == 5); // the source GUID, three ambience fields, the music GUID; empty keys are not listed
		CHECK(findings[0].Where == "AudioSourceComponent.EventGuid" && findings[0].Line == 10 && findings[0].Value == "{755abc01-cd89-4daf-a27b-04d57b807b3d}");
		CHECK(findings[2].Where == "AmbienceEvent.Path" && findings[2].Value == "event:/Ambience/Wind");
		CHECK(cleaned.find("Guid: '") == std::string::npos && cleaned.find("BankName") == std::string::npos && cleaned.find("EventPath") == std::string::npos);
		CHECK(cleaned.find("        EventGuid: not a key'") != std::string::npos); // inside a string, untouched
		CHECK(cleaned.find("      AmbienceEvent: {}\n") != std::string::npos && cleaned.find("      Event: {}\n") != std::string::npos);
		{
			const Yaml::Node root = Yaml::Load(cleaned);
			const Yaml::Node zone = root["Entities"][1]["AudioZoneComponent"];
			CHECK(AudioEventRefSerializer::Read(zone["Snapshot"]).Sound == 42);
			CHECK(!AudioEventRefSerializer::Read(zone["AmbienceEvent"]).HasReference());
			const AudioEventRef source = AudioEventRefSerializer::ReadFlat(root["Entities"][0]["AudioSourceComponent"], "Event");
			CHECK(source.Sound == 8743721474366396702ull && source.Guid.empty());
			CHECK(root["Entities"][1]["MusicDirectorComponent"]["State"].as<std::string>() == "Calm");
		}
		// Clean is a fixed point, and line endings are kept.
		std::string again;
		std::vector<Finding> none;
		CHECK(CleanText(cleaned, FileKind::Asset, again, none, error) && again == cleaned && none.empty());
		std::string crlf;
		for (const char c : scene)
			crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
		CHECK(CleanText(crlf, FileKind::Asset, again, none, error));
		size_t newlines = 0, pairs = 0;
		for (size_t i = 0; i < again.size(); ++i)
			if (again[i] == '\n')
				++newlines, pairs += i > 0 && again[i - 1] == '\r';
		CHECK(newlines > 0 && pairs == newlines && again.find(" {}\r\n") != std::string::npos);

		// The writer leaves out what the cleanup removed.
		Yaml::Writer out;
		out << Yaml::BeginMap;
		AudioEventRef sound;
		sound.Sound = 7;
		AudioEventRefSerializer::Write(out, "Event", sound);
		AudioEventRefSerializer::WriteFlat(out, "Event", sound);
		out << Yaml::EndMap;
		const std::string written = out.c_str();
		CHECK(written.find("Guid") == std::string::npos && written.find("Path") == std::string::npos && written.find("Bank") == std::string::npos);

		const std::string project = R"(Project:
  Name: Old
  Audio:
    FileStreamingDurationThreshold: 1
    StudioProjectPath: Audio/SampleProject/SampleProject.fspro
    StudioPlatform: Desktop
    Windows:
      Enabled: false
      StudioPlatform: Desktop
      BankOutputPath: Build/Desktop
      Performance:
        RealVoices: 64
    StudioBankOutputPath: Build/Desktop
    RebuildBanksOnPlay: true
    EnableLiveUpdate: true
    Accessibility:
      Events:
        '{755abc01-cd89-4daf-a27b-04d57b807b3d}':
          Captions:
            en: '[music]'
        '9117899157295655424':
          Captions:
            en: '[impact]'
    DialogueLanguage: en
)";
		findings.clear();
		CHECK(CleanText(project, FileKind::Project, cleaned, findings, error));
		CHECK(findings.size() == 8);
		CHECK(findings.back().Where == "Audio.Accessibility.Events" && findings.back().Value == "{755abc01-cd89-4daf-a27b-04d57b807b3d}");
		CHECK(cleaned.find("Studio") == std::string::npos && cleaned.find("Bank") == std::string::npos && cleaned.find("LiveUpdate") == std::string::npos);
		CHECK(cleaned.find("[music]") == std::string::npos && cleaned.find("[impact]") != std::string::npos);
		CHECK(cleaned.find("      Enabled: false\n      Performance:") != std::string::npos && cleaned.find("    DialogueLanguage: en") != std::string::npos);

		const std::string registry = R"(Assets:
  - Handle: 1
    FilePath: Scenes/Old.luxscene
    Type: Scene
  - Handle: 2
    FilePath: Audio/SampleProject/SampleProject.fspro
    Type: AudioProject
  - Handle: 3
    FilePath: Audio/Build/Master.bank
    Type: AudioBank
)";
		findings.clear();
		CHECK(CleanText(registry, FileKind::Registry, cleaned, findings, error));
		CHECK(findings.size() == 2 && findings[1].Value == "Audio/Build/Master.bank" && findings[1].Line == 8);
		CHECK(cleaned == "Assets:\n  - Handle: 1\n    FilePath: Scenes/Old.luxscene\n    Type: Scene\n");

		const std::string dialogue = "Lines:\n  - Key: hello\n    Translations:\n      en:\n        Text: Hello\n        AudioKey: hello_en\n";
		findings.clear();
		CHECK(CleanText(dialogue, FileKind::Dialogue, cleaned, findings, error) && findings.size() == 1 && cleaned.find("AudioKey") == std::string::npos);

		// A legacy key that opens a list item cannot go on its own: the file is refused, unchanged.
		findings.clear();
		CHECK(!CleanText("AudioSourceComponent:\n  - EventGuid: x\n    EventSound: 1\n", FileKind::Asset, cleaned, findings, error) && findings.empty() && !error.empty());

		// Scan and Apply on a project folder; a second scan finds nothing.
		const auto root = s_Directory / "legacy-project";
		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root / "Assets" / "Scenes");
		const auto write = [](const std::filesystem::path& path, const std::string& text) { std::ofstream(path, std::ios::binary) << text; };
		write(root / "Old.luxproj", project);
		write(root / "Assets" / "AssetRegistry.lzr", registry);
		write(root / "Assets" / "Scenes" / "Old.luxscene", scene);
		write(root / "Assets" / "Scenes" / "Clean.luxscene", "Scene: Clean\nEntities: []\n");
		const ScanResult scan = Scan(root / "Old.luxproj", root / "Assets", root / "Assets" / "AssetRegistry.lzr");
		CHECK(scan.Errors.empty() && scan.Files.size() == 3 && scan.Findings.size() == 8 + 2 + 5);
		std::vector<std::string> errors;
		CHECK(Apply(scan, errors) && errors.empty());
		const ScanResult after = Scan(root / "Old.luxproj", root / "Assets", root / "Assets" / "AssetRegistry.lzr");
		CHECK(after.Errors.empty() && after.Files.empty() && after.Findings.empty());
		CHECK(!Apply(scan, errors) && errors.size() == 3); // the old scan is stale now
		std::puts("PASS: Remove FMOD data lists every FMOD reference, removes them all and keeps every other line");
	}
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0); // a crash keeps every PASS line before it
	s_Directory = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path() / "lux-native-backend-tests";
	std::filesystem::create_directories(s_Directory);
	TestReferences();
	TestSerialization();
	TestSoundBankAsset();
	TestProjectAudioSettings();
	TestCurves();
	TestEngine();
	TestPanAndAttenuation();
	TestListeners();
	TestOcclusion();
	TestVolumeAndBuses();
	TestMixer();
	TestSnapshots();
	TestEffects();
	TestVCAsAndGlobals();
	TestAccessibility();
	TestMeters();
	TestMusicTimeline();
	TestDialogueClips();
	TestPackedClips();
	TestSoundBanks();
	TestPackStream();
	TestPackStreamStress();
	TestVoiceBudget();
	TestSampleProjectSounds();
	TestPauseLayering();
	TestPlaybackLifecycle();
	TestContainers();
	TestCurvesAndVariation();
	TestLimits();
	TestStreaming();
	TestPhysicsAudio();
	TestFileInfo();
	TestLiveSoundEdits();
	TestLegacyCleanup();
	std::puts("All native backend checks passed");
	return 0;
}
