#include "AudioTestHost.h"
#include "Lux/Audio/AudioSourcePlayback.h"
#include "Lux/Audio/AudioPerformance.h"
#include "Lux/Audio/AudioValidation.h"
#include "Lux/Scene/Components.h"
#include "Lux/Serialization/FileStream.h"
#include <yaml-cpp/yaml.h>
#include <glm/gtc/matrix_transform.hpp>
#include <thread>
#include <fstream>

int main(int argc, char** argv)
{
	assert(argc == 3);
	const std::filesystem::path output = argv[2];
	AudioPerformanceSettings settings;
	assert(settings.Validate() && settings.RealVoices == 64);
	for (const char* yaml : { "RealVoices: 0", "RealVoices: 513", "CPUPercent: .nan", "BankMemoryMiB: -1",
		"RaytracingMilliseconds: 0", "BusVoices: { 'bus:/': 0 }", "BusVoices: { nonsense: 2 }", "BusVoices: { 'bus:/': 2, 'bus:/': 3 }" })
		assert(!settings.DeserializeYAML(YAML::Load(yaml)) && settings.RealVoices == 64);
	settings.RealVoices = 32;
	settings.BusVoices = { { "bus:/", 16 }, { "bus:/SFX", 4 } };
	const auto file = output / "performance-settings.bin";
	{
		FileStreamWriter writer(file);
		assert(settings.Serialize(writer));
	}
	AudioPerformanceSettings copied;
	{
		FileStreamReader reader(file);
		assert(copied.Deserialize(reader));
	}
	assert(copied.RealVoices == 32 && copied.BusVoices == settings.BusVoices);
	std::filesystem::resize_file(file, std::filesystem::file_size(file) - 1);
	{
		FileStreamReader reader(file);
		assert(!copied.Deserialize(reader));
	}

	BankDirectory = argv[1];
	TestRealVoices = 4;
	AudioEngine::Init();
	Load();
	AudioListener::States listeners{};
	listeners[0].Weight = 1;
	assert(!AudioSourcePlayback::OutOfRange({ 10, 0, 0 }, 10, listeners, false));
	assert(AudioSourcePlayback::OutOfRange({ 9.9f, 0, 0 }, 10, listeners, true));
	listeners[1].Weight = 1;
	listeners[1].UseAttenuationPosition = true;
	listeners[1].AttenuationPosition = { 100, 0, 0 };
	assert(!AudioSourcePlayback::OutOfRange({ 100, 0, 0 }, 10, listeners, false));
	listeners[1].Weight = 0;
	{
		AudioSourceComponent source;
		source.Event.Guid = AudioEngine::ResolveEventReference("event:/SurfaceMotion");
		source.Priority = 10;
		source.DistanceCulling = true;
		source.ParameterOverrides = { { "Speed", 3 } };
		AudioSourcePlayback playback;
		playback.Update(source, glm::mat4(1), listeners, false);
		Tick();
		assert(playback.IsPlaying() && playback.GetInstance()->GetPriority() == 10);
		assert(playback.GetInstance()->Is3D() && !playback.GetInstance()->IsOneShot());
		const float maximum = playback.GetInstance()->GetMaximumDistance();
		assert(maximum > 0);
		const auto distant = glm::translate(glm::mat4(1), glm::vec3(maximum * 2, 0, 0));
		playback.Update(source, distant, listeners, false);
		Tick();
		assert(playback.IsCulled() && playback.IsPlaying() && !playback.GetInstance());
		assert(Count("event:/SurfaceMotion") == 0);
		assert(!playback.SetParameter("MissingParameter", 7));
		assert(playback.SetParameter("Speed", 7));
		Near(playback.GetParameter("speed"), 7);
		assert(playback.SetParameterLabel("localstate", "Loud"));
		Near(playback.GetParameter("LocalState"), 1);
		playback.SetTimelinePosition(100);
		source.ScriptPaused = true;
		playback.Update(source, glm::mat4(1), listeners, true);
		Tick();
		assert(!playback.IsCulled() && playback.GetInstance()->IsPaused());
		Near(playback.GetParameter("speed"), 7);
		assert(playback.GetTimelinePosition() == 100);
		Near(playback.GetParameter("LocalState"), 1);
		source.ScriptPaused = false;
		playback.Update(source, distant, listeners, false);
		playback.Stop(false);
		playback.Update(source, glm::mat4(1), listeners, false);
		Tick();
		assert(!playback.IsPlaying());
		playback.Play();
		Tick();
		assert(playback.IsPlaying());
		source.Priority = 220;
		playback.Update(source, glm::mat4(1), listeners, false);
		assert(playback.GetInstance()->GetPriority() == 220);
		source.Event.Guid = AudioEngine::ResolveEventReference("event:/SurfaceHit");
		playback.Update(source, distant, listeners, false);
		assert(playback.IsCulled() && !playback.IsPlaying());
		playback.Update(source, glm::mat4(1), listeners, false);
		Tick();
		assert(!playback.IsPlaying());
		source.Event.Guid = AudioEngine::ResolveEventReference("event:/MusicBed");
		playback.Update(source, distant, listeners, false);
		Tick();
		assert(!playback.IsCulled() && playback.IsPlaying()); // 2D ignores distance culling.
		assert(playback.SetParameterLabel("State", "Combat"));
		AudioEngine::Shutdown();
		AudioEngine::Init();
		Load();
		playback.Update(source, distant, listeners, false);
		Tick();
		assert(playback.IsPlaying());
		Near(playback.GetParameter("State"), 1);
	}
	Tick();
	{
		AudioPerformanceSettings budgets;
		budgets.RealVoices = 4;
		budgets.BusVoices = { { "bus:/Music", 1 } };
		assert(AudioPerformance::Configure(budgets));
		std::vector<Ref<AudioEventInstance>> voices;
		for (int i = 0; i < 8; ++i)
		{
			auto voice = AudioEventInstance::Create("event:/MusicBed");
			assert(voice && voice->SetPriority(20 + i) && voice->Start());
			voices.push_back(voice);
		}
		for (int i = 0; i < 40; ++i)
		{
			Tick();
			AudioPerformance::SubmitScene(3, 2, true);
			AudioPerformance::Update();
			// NOSOUND advances with flushCommands too; inspect an audible sample, not the
			// deliberate silence between the 1.25-second instrument and two-second loop.
			const auto& sample = AudioPerformance::GetStats();
			const auto& buses = AudioPerformance::GetBuses();
			if (sample.RealVoices > 0 && sample.VirtualVoices > 0 && sample.RaytracingExceeded &&
				!buses.empty() && buses[0].Available && buses[0].Peak > 0)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		const auto& stats = AudioPerformance::GetStats();
		assert(stats.MixerAvailable && stats.MemoryAvailable && stats.MemoryMiB > 0);
		assert(stats.CulledSources == 2 && stats.RaytracingExceeded);
		assert(stats.RealVoices <= 4 && stats.VirtualVoices > 0 && stats.VoicesExceeded);
		const auto& meters = AudioPerformance::GetBuses();
		assert(meters.size() == 1 && meters[0].Available && meters[0].Voices > 1 && meters[0].Exceeded);
		assert(std::isfinite(meters[0].Peak) && meters[0].Peak > 0 && meters[0].RMS > 0);
		AudioPerformance::Reset();
		assert(AudioPerformance::GetBuses().empty());
	}
	Tick();
	const auto generation = AudioEngine::GetEventGeneration();
	const auto guid = AudioEngine::ResolveEventReference("event:/SurfaceMotion");
	std::vector<AudioValidationReference> refs{ { { guid, "event:/SurfaceMotion", "Master.bank" }, "Test source", AudioReferenceKind::Loop } };
	auto report = AudioValidation::ValidateBanks(BankDirectory, refs, { "bus:/SFX" });
	assert(!report.HasErrors() && report.CatalogEvents > 0 && report.ReferencedEvents == 1 && report.BankBytes > 0);
	assert(AudioEngine::GetEventGeneration() == generation && AudioEventInstance::Create(guid));
	refs[0].Event.Guid = "{11111111-1111-1111-1111-111111111111}";
	assert(AudioValidation::ValidateBanks(BankDirectory, refs).HasErrors());
	refs[0].Event.Guid = AudioEngine::ResolveEventReference("event:/SurfaceHit");
	assert(AudioValidation::ValidateBanks(BankDirectory, refs).HasErrors());
	refs.clear();
	assert(AudioValidation::ValidateBanks(BankDirectory, refs, { "bus:/MissingBus" }).HasErrors());
	const auto corrupt = output / "corrupt-banks";
	std::filesystem::create_directories(corrupt);
	std::ofstream(corrupt / "Master.bank") << "not an FMOD bank";
	assert(AudioValidation::ValidateBanks(corrupt, {}).HasErrors());
	AudioEngine::Shutdown();
	std::cout << "PASS: voice priority, culling lifecycle, budgets, real bus meters, settings and isolated bank validation\n";
}
