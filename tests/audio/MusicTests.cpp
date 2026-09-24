#include "AudioTestHost.h"
#include "Lux/Audio/MusicDirector.h"
#include <chrono>
#include <limits>
#include <thread>

void Pump(MusicDirector& music, bool paused = false)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	Tick();
	music.Update(paused);
}

template<typename Predicate>
void Until(MusicDirector& music, Predicate predicate)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!predicate() && std::chrono::steady_clock::now() < deadline)
		Pump(music);
	if (!predicate())
	{
		int position = -1;
		if (Count("event:/MusicBed") == 1)
			Only("event:/MusicBed")->getTimelinePosition(&position);
		std::cerr << "Timed out: playing=" << music.IsPlaying() << " bar=" << music.GetCurrentBar()
			<< " beat=" << music.GetCurrentBeat() << " position=" << position << std::endl;
	}
	assert(predicate());
}

float Parameter(const char* event, const char* name)
{
	float value = 0;
	assert(Only(event)->getParameterByName(name, &value) == FMOD_OK);
	return value;
}

int main(int argc, char** argv)
{
	assert(argc == 2);
	BankDirectory = argv[1];
	AudioEngine::Init();
	Load();
	const std::string bed = AudioEngine::ResolveEventReference("event:/MusicBed");
	const std::string other = AudioEngine::ResolveEventReference("event:/MusicOther");
	{
		MusicDirector music;
		assert(music.SetState("Explore"));
		assert(music.SetIntensity(0.3f));
		assert(music.SetLayerEnabled("Drums", false));
		assert(music.Play(bed));
		Tick();
		Near(Parameter("event:/MusicBed", "Intensity"), 0.3f);
		assert(music.SetState("Combat"));
		assert(music.SetIntensity(0.8f));
		assert(music.SetLayerEnabled("Drums", true));
		Tick();
		Near(Parameter("event:/MusicBed", "State"), 1.0f);
		Near(Parameter("event:/MusicBed", "Layer_Drums"), 1.0f);
		assert(!music.SetState("Missing"));
		assert(!music.SetIntensity(std::numeric_limits<float>::quiet_NaN()));
		assert(!music.SetIntensity(2.0f));
		assert(!music.SetLayerEnabled("Missing", true));
		assert(!music.Play("event:/MusicStinger"));
		assert(!music.Play("event:/SurfaceMotion")); // 3D events are not music beds.
		assert(!music.PlayStinger(bed));
		assert(music.GetReference() == bed && music.IsPlaying());
		assert(music.PlayStinger("event:/MusicStinger"));
		Tick();
		assert(Count("event:/MusicBed") == 1 && Count("event:/MusicStinger") == 1);
		int beats = 0, markers = 0;
		const auto mainThread = std::this_thread::get_id();
		music.SetTempoCallback([&](int bar, int beat)
		{
			assert(std::this_thread::get_id() == mainThread);
			assert(bar > 0 && beat > 0);
			++beats;
		});
		music.SetMarkerCallback([&](const std::string& name)
		{
			assert(std::this_thread::get_id() == mainThread);
			assert(name == "Cue" || name == "Section:Verse" || name == "Loop");
			++markers;
		});
		Until(music, [&] { return beats >= 3 && markers >= 2; });
		music.Update(true);
		Tick();
		const int pausedBeats = beats;
		for (int i = 0; i < 30; ++i)
			Pump(music, true);
		assert(beats == pausedBeats);
		bool paused = false;
		assert(Only("event:/MusicBed")->getPaused(&paused) == FMOD_OK && paused);
		music.Update(false);
		Until(music, [&] { return beats > pausedBeats; });

		int boundaryBeat = 0;
		std::string boundaryMarker;
		bool boundaryIsBeat = false;
		music.SetTempoCallback([&](int, int beat) { boundaryIsBeat = true; boundaryBeat = beat; });
		music.SetMarkerCallback([&](const std::string& name) { boundaryIsBeat = false; boundaryMarker = name; });
		for (auto sync : { MusicSync::NextBeat, MusicSync::NextBar, MusicSync::NextMarker, MusicSync::NextSection })
		{
			assert(music.Play(bed));
			Pump(music);
			assert(music.QueueTransition(other, sync));
			Until(music, [&] { return music.GetReference() == other; });
			if (sync == MusicSync::NextBeat || sync == MusicSync::NextBar)
				assert(boundaryIsBeat && (sync != MusicSync::NextBar || boundaryBeat == 1));
			else
				assert(!boundaryIsBeat && (sync != MusicSync::NextSection || boundaryMarker.starts_with("Section:")));
			Tick();
			assert(Count("event:/MusicBed") == 0);
			assert(Count("event:/MusicOther") == 1);
			Near(Parameter("event:/MusicOther", "Intensity"), 0.8f);
			Near(Parameter("event:/MusicOther", "State"), 1.0f);
		}

		// Arrived notifications must not satisfy a transition requested after their arrival.
		assert(music.Play(bed));
		Tick();
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		Tick();
		assert(music.QueueTransition(other, MusicSync::NextBeat));
		music.Update(false);
		assert(music.GetReference() == bed);
		Until(music, [&] { return music.GetReference() == other; });

		// A request from a beat handler means the next beat, never the current batch.
		assert(music.Play(bed));
		bool queued = false;
		music.SetTempoCallback([&](int, int)
		{
			if (!queued)
			{
				queued = true;
				assert(music.QueueTransition(other, MusicSync::NextBeat));
			}
		});
		Until(music, [&] { return queued; });
		assert(music.GetReference() == bed);
		Until(music, [&] { return music.GetReference() == other; });

		// Reentrant stop invalidates the remainder of the callback batch and any queued bed.
		assert(music.Play(bed));
		music.SetTempoCallback([&](int, int) { music.Stop(false); });
		assert(music.QueueTransition(other, MusicSync::NextBeat));
		Until(music, [&] { return !music.IsPlaying(); });
		assert(music.GetReference().empty());
		Tick();
		assert(Count("event:/MusicOther") == 0);
		music.SetTempoCallback({});

		// Script and per-instance timeline mailboxes coexist; neither drain consumes the other.
		auto independent = AudioEventInstance::Create(bed);
		assert(independent->SetCallbackHandle(991));
		assert(independent->EnableTimelineNotifications());
		assert(independent->Start());
		Tick();
		std::this_thread::sleep_for(std::chrono::milliseconds(350));
		Tick();
		auto scriptNotifications = AudioEventInstance::DrainNotifications();
		auto timeline = independent->DrainTimelineNotifications();
		assert(!timeline.empty());
		assert(std::any_of(timeline.begin(), timeline.end(), [](const auto& n) { return n.IsBeat && n.Tempo == 240.0f; }));
		assert(std::any_of(scriptNotifications.begin(), scriptNotifications.end(), [](const auto& n) { return n.Handle == 991 && !n.Marker.empty(); }));
		independent = nullptr;
		Tick();

		// Invalid old wrappers must not call a released Studio system; only the active bed resumes.
		assert(music.Play(bed));
		assert(music.QueueTransition(other, MusicSync::NextBeat));
		assert(music.PlayStinger("event:/MusicStinger"));
		Tick();
		AudioEngine::Shutdown();
		music.Update(false);
		AudioEngine::Init();
		Load();
		music.Update(false);
		Tick();
		assert(music.IsPlaying() && music.GetReference() == bed);
		Near(Parameter("event:/MusicBed", "Intensity"), 0.8f);
		assert(Count("event:/MusicOther") == 0 && Count("event:/MusicStinger") == 0);
		assert(music.PlayStinger("event:/MusicStinger"));
		music.Stop(true);
		Until(music, [&] { return !music.IsPlaying(); });
		music.Clear();
		Tick();
		assert(Count("event:/MusicBed") == 0 && Count("event:/MusicStinger") == 0);
		assert(music.Play(bed));
	}
	Tick();
	assert(Count("event:/MusicBed") == 0);
	AudioEngine::Shutdown();
	std::cout << "PASS: music states, intensity, layers, stingers, timeline callbacks, transitions, pause, reload and teardown\n";
}
