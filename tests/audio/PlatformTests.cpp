#include "AudioTestHost.h"
#include "Lux/Audio/AudioPerformance.h"
#include "Lux/Audio/AudioBankBuilder.h"
#include "Lux/Project/Project.h"
#include "Lux/Serialization/FileStream.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/FileSystem.h"
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
	Yaml::Writer yaml;
	audio.Linux.SerializeYAML(yaml);
	AudioDesktopProfile copied;
	assert(copied.DeserializeYAML(Yaml::Load(yaml.c_str())));
	assert(copied.Enabled && copied.BankOutputPath == "Build/Linux" && copied.Performance.MuteWhenUnfocused);
	for (const char* invalid : { "StudioPlatform: ''", "StudioPlatform: 'Linux,Windows'", "BankOutputPath: ''", "Performance: { RealVoices: 0 }" })
		assert(!copied.DeserializeYAML(Yaml::Load(invalid)) && copied.Enabled);
	AudioDesktopProfile legacy;
	const auto legacyAudio = Yaml::Load("{}");
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

	// Shell quoting: a crafted file name stays one literal argument for /bin/sh.
	assert(FileSystem::QuoteShellArgument("a'b$(touch x)`y`") == "'a'\\''b$(touch x)`y`'");
	// Atomic writes: a successful save replaces the file; a failed one leaves the old file intact.
	const auto saved = output / "atomic save.yaml";
	assert(FileSystem::WriteFileAtomically(saved, "first\n") && FileSystem::WriteFileAtomically(saved, "second\n"));
	{
		std::ifstream file(saved);
		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		assert(text == "second\n" && !std::filesystem::exists(saved.string() + ".tmp"));
	}
	assert(!FileSystem::WriteFileAtomically(output / "missing directory" / "scene.yaml", "lost\n"));
	std::cout << "PASS: desktop profiles/defaults, packaged settings, build target quoting, independent focus mute, shell quoting and atomic saves\n";
}
