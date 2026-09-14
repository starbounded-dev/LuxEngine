#include "AudioTestHost.h"
#include "Lux/Audio/DialogueDirector.h"
#include "Lux/Asset/DialogueTableSerializer.h"
#include "Lux/Serialization/FileStream.h"
#include <thread>

int main(int argc, char** argv)
{
	assert(argc == 4);
	const std::filesystem::path output = argv[3];
	AudioEngine::Init();
	BankDirectory = argv[1];
	Load();
	auto table = Ref<DialogueTable>::Create();
	auto& greeting = table->Lines["greeting"];
	greeting.Event.Guid = AudioEngine::ResolveEventReference("event:/SurfaceHit");
	greeting.Translations["en"] = { "Hello", "Guard", "" };
	greeting.Translations["fr"] = { "Bonjour", "Garde", "" };
	table->Lines["urgent"] = greeting;
	table->Lines["urgent"].Priority = DialoguePriority::Critical;
	table->Lines["locked"] = greeting;
	table->Lines["locked"].Interruptible = false;
	assert(table->Validate());
	DialogueTable loaded;
	assert(loaded.FromYAML(table->ToYAML()));
	assert(loaded.Lines.at("greeting").Translations.at("fr").Text == "Bonjour");
	assert(!loaded.FromYAML("Version: 999\n"));
	assert(loaded.Lines.size() == 3);
	assert(!loaded.FromYAML("DefaultLanguage: invalid/code\n"));
	assert(!DialogueTable::ValidLanguage(std::string("en\0x", 4)));
	DialogueSettings settings;
	settings.Table = 123;
	settings.Language = "fr-CA";
	{
		FileStreamWriter writer(output / "dialogue-settings.bin");
		assert(settings.Serialize(writer));
	}
	{
		DialogueSettings read;
		FileStreamReader reader(output / "dialogue-settings.bin");
		assert(read.Deserialize(reader));
		assert(read.Table == settings.Table && read.Language == settings.Language);
	}
	{
		FileStreamWriter writer(output / "bad-dialogue-settings.bin");
		writer.WriteRaw<uint64_t>(0);
		writer.WriteRaw<uint32_t>(0xffffffff);
	}
	{
		FileStreamReader reader(output / "bad-dialogue-settings.bin");
		assert(!settings.Deserialize(reader));
		assert(settings.Table == 123 && settings.Language == "fr-CA");
	}
	{
		const auto yaml = table->ToYAML();
		FileStreamWriter writer(output / "dialogue-packed.bin");
		writer.WriteRaw<uint64_t>(yaml.size());
		writer.WriteData(yaml.data(), yaml.size());
	}
	{
		DialogueTableSerializer serializer;
		AssetPackFile::AssetInfo info{};
		info.PackedSize = std::filesystem::file_size(output / "dialogue-packed.bin");
		FileStreamReader reader(output / "dialogue-packed.bin");
		auto unpacked = serializer.DeserializeFromAssetPack(reader, info).As<DialogueTable>();
		assert(unpacked && unpacked->Lines.size() == 3);
		++info.PackedSize;
		assert(!serializer.DeserializeFromAssetPack(reader, info));
	}

	DialogueDirector director;
	bool speakerAlive = true;
	auto resolve = [&](UUID speaker)
	{
		DialogueSpeaker result;
		result.Valid = !speaker || speakerAlive;
		result.Name = "Entity";
		result.Position = { static_cast<float>(static_cast<uint64_t>(speaker)), 0, 0 };
		result.IsOffScreen = true;
		return result;
	};
	assert(director.Configure(table, "fr", resolve));
	table->Lines["greeting"].Translations["fr"].Text = "Edited while playing";
	std::vector<SubtitleEvent> events;
	director.SetSubtitleCallback([&](const SubtitleEvent& event) { events.push_back(event); });
	auto tick = [&]
	{
		Tick();
		director.Update(0.01f, false);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	};
	auto waitShown = [&]
	{
		for (int i = 0; i < 100 && (events.empty() || !events.back().Shown); ++i)
			tick();
		assert(!events.empty() && events.back().Shown);
	};
	const auto first = director.Speak("greeting", 1);
	assert(first && director.IsSpeaking(1));
	waitShown();
	assert(events.back().Text == "Bonjour" && events.back().SpeakerName == "Garde");
	assert(events.back().IsOffScreen && events.back().SpeakerPosition.x == 1);
	const size_t beforePause = events.size();
	director.Update(0.0f, true);
	Tick();
	const int pausedAt = Only("event:/SurfaceHit")->isValid() ? director.IsActive(first) : 0;
	assert(pausedAt);
	for (int i = 0; i < 5; ++i)
	{
		Tick();
		director.Update(1.0f, true);
	}
	assert(events.size() == beforePause && director.IsActive(first));
	director.Update(0.0f, false);
	const auto queued = director.Speak("greeting", 2);
	const auto urgent = director.Speak("urgent", 3);
	assert(queued && urgent && director.GetQueueLength() == 2);
	director.Stop(first, false);
	tick();
	assert(!director.IsActive(first) && director.IsSpeaking(3));
	assert(director.SetQueueMode(DialogueQueueMode::DropIfBusy));
	assert(!director.Speak("greeting", 4));
	assert(director.SetQueueMode(DialogueQueueMode::Interrupt));
	assert(!director.Speak("missing", 4) && director.IsActive(urgent));
	const auto replacement = director.Speak("urgent", 4);
	assert(replacement && !director.IsActive(urgent));
	director.StopAll();
	tick();
	const auto locked = director.Speak("locked", 1);
	assert(locked && director.Speak("urgent", 2) && director.IsActive(locked));
	assert(director.GetQueueLength() == 1);
	director.StopAll();
	assert(director.SetLanguage("de"));
	events.clear();
	assert(director.Speak("greeting", 1));
	waitShown();
	assert(events.back().Text == "Hello" && events.back().Language == "en");
	speakerAlive = false;
	tick();
	assert(!director.IsSpeaking(1) && !events.back().Shown);
	speakerAlive = true;
	assert(director.Bark("greeting", 1));
	assert(!director.Bark("greeting", 2));
	assert(director.Bark("greeting", 50));
	director.StopAll();
	tick();
	// A reentrant subtitle listener can cancel every voice without invalidating update iteration.
	director.SetSubtitleCallback([&](const SubtitleEvent& event)
	{
		events.push_back(event);
		if (event.Shown)
			director.StopAll();
	});
	const auto reentrant = director.Speak("greeting", 1);
	for (int i = 0; i < 100 && director.IsActive(reentrant); ++i)
		tick();
	assert(!director.IsActive(reentrant) && !events.back().Shown);
	// Pending and active voices are canceled when their bank/system generation disappears.
	director.SetSubtitleCallback([&](const SubtitleEvent& event) { events.push_back(event); });
	const auto beforeReload = director.Speak("greeting", 1);
	const auto pendingReload = director.Speak("greeting", 2);
	assert(beforeReload && pendingReload);
	AudioEngine::Shutdown();
	director.Update(0.0f, false);
	assert(!director.IsActive(beforeReload) && !director.IsActive(pendingReload));
	director.Clear();

	// Real SDK audio-table banks exercise programmer CREATE/DESTROY and source duration.
	AudioEngine::Init();
	const std::filesystem::path media = argv[2];
	for (const auto* name : { "Master.bank", "Master.strings.bank", "SFX.bank", "Dialogue_EN.bank" })
		assert(AudioEngine::LoadBank(media / name));
	auto programmer = Ref<DialogueTable>::Create();
	auto& spoken = programmer->Lines["welcome"];
	spoken.Event.Guid = AudioEngine::ResolveEventReference("event:/Character/Dialogue");
	spoken.Translations["en"] = { "Welcome", "Narrator", "welcome" };
	spoken.Translations["fr"] = { "Au revoir", "Narrateur", "goodbye" };
	programmer->Lines["missing-sound"] = spoken;
	programmer->Lines["missing-sound"].Translations["en"].AudioKey = "not-in-this-bank";
	bool oneShot = false;
	assert(Desc("event:/Character/Dialogue")->isOneshot(&oneShot) == FMOD_OK);
	std::cout << "Programmer event isOneShot: " << oneShot << std::endl;
	assert(director.Configure(programmer, "en", resolve));
	director.SetSubtitleCallback([&](const SubtitleEvent& event) { events.push_back(event); });
	events.clear();
	const auto speech = director.Speak("welcome");
	assert(speech);
	waitShown();
	assert(events.back().Duration > 0.0f && events.back().Text == "Welcome");
	assert(director.SetQueueMode(DialogueQueueMode::Interrupt));
	assert(!director.Speak("missing-sound") && director.IsActive(speech));
	for (int i = 0; i < 1000 && director.IsActive(speech); ++i)
		tick();
	assert(!director.IsActive(speech) && !events.back().Shown);
	assert(director.SetLanguage("fr"));
	events.clear();
	const auto localized = director.Speak("welcome");
	assert(localized);
	waitShown();
	assert(events.back().Text == "Au revoir" && events.back().Language == "fr" && events.back().Duration > 0.0f);
	director.Clear(); // Sound destruction may arrive after the callback mailbox is removed.
	for (int i = 0; i < 10; ++i)
		tick();
	AudioEventInstance::DrainNotifications();
	AudioEngine::Shutdown();
	std::cout << "Dialogue serialization, scheduling, subtitles and programmer sounds passed\n";
}
