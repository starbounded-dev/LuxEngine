#include "AudioTestHost.h"
#include "Lux/Audio/AudioAccessibilityMixer.h"
#include "Lux/Serialization/FileStream.h"
#include <yaml-cpp/yaml.h>
#include <thread>
#include "Lux/ImGui/AudioAccessibilityWidgets.h"
#include <imgui.h>

FMOD::DSP* FindDSP(FMOD::ChannelGroup* group, FMOD_DSP_TYPE type)
{
	int count = 0;
	assert(group->getNumDSPs(&count) == FMOD_OK);
	// Accessibility processing is appended at the tail, after authored FMOD DSPs.
	for (int i = count - 1; i >= 0; --i)
	{
		FMOD::DSP* dsp = nullptr;
		FMOD_DSP_TYPE found;
		assert(group->getDSP(i, &dsp) == FMOD_OK && dsp->getType(&found) == FMOD_OK);
		if (found == type)
			return dsp;
	}
	return nullptr;
}

int main(int argc, char** argv)
{
	assert(argc == 3);
	BankDirectory = argv[1];
	const std::filesystem::path directory = argv[2];
	AudioEngine::Init();
	Load();
	AudioAccessibilityConfig config;
	config.BusPaths[1] = "bus:/Music";
	config.BusPaths[2] = "bus:/SFX";
	config.BusPaths[3] = "bus:/Dialogue";
	config.Defaults.Captions = true;
	config.Defaults.VisualCues = true;
	const auto hit = AudioEngine::ResolveEventReference("event:/SurfaceHit");
	config.Events[hit].Captions["en"] = "[impact]";
	config.Events[hit].Captions["fr"] = "[choc]";
	config.Events[hit].VisualCue = true;
	config.Events[hit].MaxDistance = 10;
	config.SpeakerColors["Guard"] = { 0.2f, 0.4f, 0.6f, 1.0f };
	assert(config.Validate());
	YAML::Emitter yaml;
	config.SerializeYAML(yaml);
	AudioAccessibilityConfig restored;
	assert(restored.DeserializeYAML(YAML::Load(yaml.c_str())));
	assert(restored.Events.at(hit).Captions.at("fr") == "[choc]");
	assert(restored.Defaults.Captions && restored.Defaults.MaxLines == 3);
	assert(!restored.DeserializeYAML(YAML::Load("Defaults: {MaxLines: 0}")));
	assert(restored.Events.contains(hit));
	assert(!restored.DeserializeYAML(YAML::Load("DescriptionDuck: .nan")));
	assert(!restored.DeserializeYAML(YAML::Load("Buses: {Master: 'bus:/', Dialogue: 'bus:/'}")));
	assert(!restored.DeserializeYAML(YAML::Load("Buses: {Music: 'bus:/Music', SFX: 'bus:/Music/SFX'}")));
	AudioAccessibilityConfig defaults;
	assert(defaults.DeserializeYAML(YAML::Node()));
	assert(!defaults.Defaults.Mono && defaults.Defaults.Subtitles && defaults.Events.empty());
	{
		FileStreamWriter writer(directory / "accessibility.bin");
		assert(config.Serialize(writer));
		writer.WriteRaw<uint32_t>(0x12345678);
	}
	{
		FileStreamReader reader(directory / "accessibility.bin");
		AudioAccessibilityConfig read;
		assert(read.Deserialize(reader));
		uint32_t sentinel = 0;
		reader.ReadRaw(sentinel);
		assert(sentinel == 0x12345678 && read.Events.at(hit).VisualCue);
	}
	{
		FileStreamWriter writer(directory / "bad-accessibility.bin");
		writer.WriteRaw<uint32_t>(0xffffffff);
	}
	{
		FileStreamReader reader(directory / "bad-accessibility.bin");
		assert(!restored.Deserialize(reader));
	}

	FMOD::ChannelGroup* master = nullptr;
	assert(AudioEngine::GetEngine()->getMasterChannelGroup(&master) == FMOD_OK);
	int originalDSPs = 0;
	assert(master->getNumDSPs(&originalDSPs) == FMOD_OK);
	int owner = 0;
	DialogueDirector dialogue;
	std::vector<SubtitleEvent> captions;
	dialogue.SetSubtitleCallback([&](const auto& event) { captions.push_back(event); });
	assert(AudioAccessibility::BeginScene(&owner, config, directory / "player-audio.yaml",
		[&](const SubtitleEvent& event) { dialogue.PublishCaption(event); }));
	assert(AudioAccessibility::HasBus(AudioCategory::Master) && AudioAccessibility::HasBus(AudioCategory::Dialogue));
	auto preferences = AudioAccessibility::GetPreferences();
	preferences.Mono = true;
	preferences.DynamicRange = AudioDynamicRange::Night;
	assert(AudioAccessibility::ApplyPreferences(preferences));
	auto* mono = FindDSP(master, FMOD_DSP_TYPE_CHANNELMIX);
	auto* compressor = FindDSP(master, FMOD_DSP_TYPE_COMPRESSOR);
	assert(mono && compressor);
	bool bypass = true;
	int grouping = -1;
	assert(mono->getBypass(&bypass) == FMOD_OK && !bypass);
	assert(mono->getParameterInt(FMOD_DSP_CHANNELMIX_OUTPUTGROUPING, &grouping, nullptr, 0) == FMOD_OK);
	assert(grouping == FMOD_DSP_CHANNELMIX_OUTPUT_ALLMONO);
	float threshold = 0;
	assert(compressor->getParameterFloat(FMOD_DSP_COMPRESSOR_THRESHOLD, &threshold, nullptr, 0) == FMOD_OK);
	Near(threshold, -24);
	FMOD::Studio::Bus* musicBus = nullptr;
	FMOD::Studio::Bus* dialogueBus = nullptr;
	assert(AudioEngine::GetStudioSystem()->getBus("bus:/Music", &musicBus) == FMOD_OK);
	assert(AudioEngine::GetStudioSystem()->getBus("bus:/Dialogue", &dialogueBus) == FMOD_OK);
	FMOD::ChannelGroup* musicGroup = nullptr;
	FMOD::ChannelGroup* dialogueGroup = nullptr;
	assert(musicBus->getChannelGroup(&musicGroup) == FMOD_OK && dialogueBus->getChannelGroup(&dialogueGroup) == FMOD_OK);
	auto* musicGain = FindDSP(musicGroup, FMOD_DSP_TYPE_FADER);
	auto* dialogueGain = FindDSP(dialogueGroup, FMOD_DSP_TYPE_FADER);
	assert(musicGain && dialogueGain);
	assert(musicBus->setVolume(0.7f) == FMOD_OK);
	preferences.Volumes[1] = 0.5f;
	preferences.DialogueBoost = 2.0f;
	assert(AudioAccessibility::ApplyPreferences(preferences));
	float authoredVolume = 0, gainDB = 0;
	assert(musicBus->getVolume(&authoredVolume) == FMOD_OK);
	Near(authoredVolume, 0.7f);
	assert(dialogueGain->getParameterFloat(FMOD_DSP_FADER_GAIN, &gainDB, nullptr, 0) == FMOD_OK);
	Near(gainDB, 20.0f * std::log10(2.0f));
	AudioAccessibility::SetDescribing(true);
	assert(musicGain->getParameterFloat(FMOD_DSP_FADER_GAIN, &gainDB, nullptr, 0) == FMOD_OK);
	Near(gainDB, 20.0f * std::log10(0.5f * config.DescriptionDuck));
	AudioAccessibility::SetDescribing(false);
	assert(musicGain->getParameterFloat(FMOD_DSP_FADER_GAIN, &gainDB, nullptr, 0) == FMOD_OK);
	Near(gainDB, 20.0f * std::log10(0.5f));

	preferences.Volumes[1] = 0;
	assert(AudioAccessibility::ApplyPreferences(preferences));
	float preWet = 0, postWet = 0, dry = 0;
	assert(musicGain->getWetDryMix(&preWet, &postWet, &dry) == FMOD_OK);
	Near(postWet, 0);
	Near(dry, 0);
	preferences.Volumes[1] = 0.5f;
	assert(AudioAccessibility::ApplyPreferences(preferences));
	assert(AudioAccessibility::SavePreferences());
	preferences.TextSize = 36;
	assert(AudioAccessibility::ApplyPreferences(preferences));
	assert(AudioAccessibility::SavePreferences()); // Existing destination must be replaced too.
	preferences.Mono = false;
	assert(AudioAccessibility::ApplyPreferences(preferences));
	assert(AudioAccessibility::LoadPreferences());
	assert(mono->getBypass(&bypass) == FMOD_OK && !bypass);
	Near(AudioAccessibility::GetPreferences().TextSize, 36);
	std::vector<SoundEvent> cues;
	AudioAccessibility::SetSoundCallback([&](const auto& event) { cues.push_back(event); });
	AudioAccessibilityView view;
	view.HasCamera = true;
	auto update = [&](bool paused = false)
	{
		Tick();
		AudioAccessibility::Update(&owner, 0.01f, paused, "fr", view, dialogue.IsDescribing());
		dialogue.Update(0.01f, paused);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	};
	auto event = AudioEventInstance::Create(hit);
	event->Set3DAttributes({ 2, 0, 0 }, {}, { 0, 0, -1 }, { 0, 1, 0 });
	assert(event->Start());
	for (int i = 0; i < 100 && captions.empty(); ++i)
		update();
	assert(captions.size() == 1 && captions.back().Text == "[choc]" && captions.back().IsCaption && captions.back().Shown);
	assert(cues.size() == 1 && cues.back().Active && cues.back().Direction.x > 0.9f);
	assert(AudioAccessibility::GetSubtitles().size() == 1);
	Near(AudioAccessibility::GetSoundCues()[0].Intensity, 0.8f);
	event->Set3DAttributes({ -2, 0, 0 }, {}, { 0, 0, -1 }, { 0, 1, 0 });
	update();
	assert(AudioAccessibility::GetSoundCues()[0].Direction.x < -0.9f);
	assert(AudioAccessibility::GetSubtitles()[0].Event.SpeakerPosition.x == -2);
	event = nullptr; // Weak tracking must neither extend the voice nor dereference its destroyed wrapper.
	update();
	update();
	assert(!captions.back().Shown && !cues.back().Active && AudioAccessibility::GetSoundCues().empty());
	assert(AudioAccessibility::GetSubtitles().empty());
	// Description lines share the foreground queue and become opt-in.
	auto table = Ref<DialogueTable>::Create();
	table->Lines["description"].Event.Guid = AudioEngine::ResolveEventReference("event:/MusicStinger");
	table->Lines["description"].Translations["en"].Text = "The door opens.";
	assert(dialogue.Configure(table, "en", [](UUID) { DialogueSpeaker speaker; speaker.Valid = true; return speaker; }));
	assert(!dialogue.Describe("description"));
	preferences.AudioDescriptions = true;
	assert(AudioAccessibility::ApplyPreferences(preferences));
	const auto description = dialogue.Describe("description");
	assert(description && dialogue.IsDescribing());
	for (int i = 0; i < 100 && (captions.empty() || !captions.back().Shown); ++i)
		update();
	assert(captions.back().IsDescription);
	preferences.AudioDescriptions = false;
	assert(AudioAccessibility::ApplyPreferences(preferences));
	update();
	assert(!dialogue.IsActive(description) && !dialogue.IsDescribing());
	// Draw actual production widgets without a renderer. Explicit newlines obey MaxLines.
	preferences.MaxLines = 1;
	assert(AudioAccessibility::ApplyPreferences(preferences));
	SubtitleEvent sample;
	sample.Handle = 123;
	sample.Shown = true;
	sample.SpeakerName = "Guard";
	sample.Text = "First line\nSecond line";
	AudioAccessibility::OnSubtitle(sample);
	Near(AudioAccessibility::GetSubtitles().back().SpeakerColor.r, 0.2f);
	const float subtitleAge = AudioAccessibility::GetSubtitles().back().Age;
	AudioAccessibility::Update(&owner, 10.0f, true, "fr", view, false);
	Near(AudioAccessibility::GetSubtitles().back().Age, subtitleAge);
	ImGui::CreateContext();
	auto& io = ImGui::GetIO();
	io.DisplaySize = { 800, 600 };
	io.DeltaTime = 1.0f / 60.0f;
	io.IniFilename = nullptr;
	io.Fonts->AddFontDefault();
	unsigned char* pixels = nullptr;
	int width = 0, height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	ImGui::NewFrame();
	ImGuiEx::AudioAccessibilityOverlay({ 40, 30 }, { 760, 570 });
	ImGui::Render();
	assert(ImGui::GetDrawData()->TotalVtxCount > 0);
	for (auto* list : ImGui::GetDrawData()->CmdLists)
	{
		for (const auto& command : list->CmdBuffer)
		{
			assert(command.ClipRect.x >= 40 && command.ClipRect.y >= 30);
			assert(command.ClipRect.z <= 760 && command.ClipRect.w <= 570);
		}
		for (const auto& vertex : list->VtxBuffer)
			assert(vertex.pos.y < 539); // A second rendered text line would extend below this bound.
	}
	ImGui::NewFrame();
	bool menu = true;
	ImGuiEx::AudioAccessibilityMenu(menu);
	ImGui::Render();
	ImGui::DestroyContext();
	AudioAccessibility::EndScene(&owner);
	dialogue.Clear();
	int remaining = 0;
	assert(master->getNumDSPs(&remaining) == FMOD_OK && remaining == originalDSPs);
	Tick();
	// An active service survives a bank/system generation change without retaining stale DSPs.
	assert(AudioAccessibility::BeginScene(&owner, config, directory / "player-audio.yaml",
		[&](const SubtitleEvent& notification) { dialogue.PublishCaption(notification); }));
	auto oldEvent = AudioEventInstance::Create(hit);
	assert(oldEvent->Start());
	update();
	AudioEngine::Shutdown();
	AudioEngine::Init();
	Load();
	update();
	assert(!oldEvent->IsValid());
	assert(AudioAccessibility::HasBus(AudioCategory::Music));
	assert(AudioAccessibility::GetSoundCues().empty());
	AudioAccessibility::EndScene(&owner);
	dialogue.Clear();
	oldEvent = nullptr;
	AudioEngine::Shutdown();
	std::cout << "PASS: accessibility serialization, preferences, mono/compression DSPs, captions, visual cues, descriptions and teardown\n";
}
