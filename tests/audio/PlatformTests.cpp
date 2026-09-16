#include "AudioTestHost.h"
#include "Lux/Audio/AudioPerformance.h"
#include "Lux/Audio/AudioBankBuilder.h"
#include "Lux/Project/Project.h"
#include "Lux/Serialization/FileStream.h"
#include <yaml-cpp/yaml.h>
#include <fstream>

int main(int argc, char** argv)
{
	assert(argc == 3);
	const std::filesystem::path output = argv[2];
	Project project;
	auto& audio = project.GetConfig().Audio;
	assert(project.GetStudioPlatform() == "Desktop" && project.GetAudioPerformance().RealVoices == 64);
	audio.Windows.Enabled = true;
	audio.Windows.StudioPlatform = "Windows Desktop";
	audio.Windows.Performance.RealVoices = 24;
	audio.Linux.Enabled = true;
	audio.Linux.StudioPlatform = "Linux Desktop";
	audio.Linux.BankOutputPath = "Build/Linux";
	audio.Linux.Performance.RealVoices = 32;
	audio.Linux.Performance.MuteWhenUnfocused = true;
	assert(project.GetStudioPlatform() == "Linux Desktop" && project.GetAudioPerformance().RealVoices == 32);
	assert(project.GetStudioBankDirectory().filename() == "Linux");
	YAML::Emitter yaml;
	audio.Linux.SerializeYAML(yaml);
	AudioDesktopProfile copied;
	assert(copied.DeserializeYAML(YAML::Load(yaml.c_str())));
	assert(copied.Enabled && copied.BankOutputPath == "Build/Linux" && copied.Performance.MuteWhenUnfocused);
	for (const char* invalid : { "StudioPlatform: ''", "StudioPlatform: 'Linux,Windows'", "BankOutputPath: ''", "Performance: { RealVoices: 0 }" })
		assert(!copied.DeserializeYAML(YAML::Load(invalid)) && copied.Enabled);
	AudioDesktopProfile legacy;
	const auto legacyAudio = YAML::Load("{}");
	assert(legacy.DeserializeYAML(legacyAudio["Linux"]) && !legacy.Enabled);
	const auto runtimeFile = output / "platform-runtime.bin";
	{
		FileStreamWriter writer(runtimeFile);
		assert(project.GetAudioPerformance().Serialize(writer));
	}
	AudioPerformanceSettings runtime;
	{
		FileStreamReader reader(runtimeFile);
		assert(runtime.Deserialize(reader));
	}
	assert(runtime.RealVoices == 32 && runtime.MuteWhenUnfocused);
	audio.Linux.Enabled = false;
	assert(project.GetAudioPerformance().RealVoices == 64 && project.GetStudioPlatform() == "Desktop");
	audio.RuntimeBanks.Directory = "Audio/PackagedBanks";
	assert(project.GetStudioBankDirectory().filename() == "PackagedBanks");

	const auto script = output / "fake studio cli.sh";
	const auto arguments = output / "bank-arguments.txt";
	const auto studio = output / "test project.fspro";
	std::ofstream(studio) << "fixture";
	std::ofstream(script) << "#!/bin/sh\nprintf '%s\\n' \"$@\" > '" << arguments.string() << "'\n";
	std::filesystem::permissions(script, std::filesystem::perms::owner_all);
	assert(setenv("LUX_FMOD_STUDIO_CL", script.c_str(), 1) == 0);
	assert(AudioBankBuilder::Build(studio, "Linux Desktop"));
	std::ifstream input(arguments);
	const std::string actual{ std::istreambuf_iterator<char>(input), {} };
	assert(actual == "-build\n-export-guids\n-platforms\nLinux Desktop\n" + studio.string() + "\n");
	assert(!AudioBankBuilder::Build(studio, "Linux; echo injected"));

	BankDirectory = argv[1];
	AudioEngine::Init();
	Load();
	assert(AudioPerformance::Configure(runtime));
	auto event = AudioEventInstance::Create("event:/MusicBed");
	assert(event && event->Start());
	event->SetPaused(true);
	FMOD::Studio::Bus* bus = nullptr;
	assert(AudioEngine::GetStudioSystem()->getBus("bus:/Music", &bus) == FMOD_OK);
	assert(bus->setMute(true) == FMOD_OK);
	FMOD::ChannelGroup* master = nullptr;
	assert(AudioEngine::GetEngine()->getMasterChannelGroup(&master) == FMOD_OK);
	for (bool focused : { false, true, false, true })
	{
		AudioEngine::SetApplicationFocused(focused);
		Tick();
		bool muted = false, busMuted = false;
		assert(master->getMute(&muted) == FMOD_OK && muted == !focused);
		assert(bus->getMute(&busMuted) == FMOD_OK && busMuted && event->IsPaused());
	}
	runtime.MuteWhenUnfocused = false;
	assert(AudioPerformance::Configure(runtime));
	AudioEngine::SetApplicationFocused(false);
	bool muted = true;
	assert(master->getMute(&muted) == FMOD_OK && !muted);
	event = nullptr;
	AudioPerformance::Reset();
	AudioEngine::Shutdown();
	std::cout << "PASS: desktop profiles/defaults, packaged settings, build target quoting and independent focus mute\n";
}
