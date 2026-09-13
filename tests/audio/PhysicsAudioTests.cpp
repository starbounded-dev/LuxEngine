#include "lpch.h"
#include "Lux/Audio/PhysicsAudioSystem.h"
#include "Lux/Audio/AudioEngine.h"
#include "Lux/Asset/AudioSurfaceTableSerializer.h"
#include <fmod.hpp>
#include <fmod_studio.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cassert>
#include <iostream>
// Only host setup is replaced: production surface/event code drives the real FMOD Studio SDK.
namespace Lux
{
	std::shared_ptr<spdlog::logger> Log::s_CoreLogger = spdlog::stdout_color_mt("test");
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger = Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_EditorConsoleLogger = Log::s_CoreLogger;
	FMOD::System* AudioEngine::s_Engine = nullptr;
	FMOD::Studio::System* AudioEngine::s_StudioSystem = nullptr;
	void AudioEngine::Init()
	{
		assert(FMOD::Studio::System::create(&s_StudioSystem) == FMOD_OK);
		assert(s_StudioSystem->getCoreSystem(&s_Engine) == FMOD_OK);
		assert(s_Engine->setOutput(FMOD_OUTPUTTYPE_NOSOUND) == FMOD_OK);
		assert(s_StudioSystem->initialize(128, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, nullptr) == FMOD_OK);
		s_HasInitializedAudioEngine = true;
		++s_EventGeneration;
		++s_BankRevision;
	}
	void AudioEngine::Shutdown()
	{
		s_HasInitializedAudioEngine = false;
		++s_EventGeneration;
		++s_BankRevision;
		assert(s_StudioSystem->release() == FMOD_OK);
		s_StudioSystem = nullptr;
		s_Engine = nullptr;
	}
	bool AudioEngine::LoadBank(const std::filesystem::path& p)
	{
		FMOD::Studio::Bank* b = nullptr;
		auto r = s_StudioSystem->loadBankFile(p.c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &b);
		++s_BankRevision;
		return r == FMOD_OK;
	}
	std::string AudioEngine::ResolveEventReference(const std::string& ref)
	{
		FMOD_GUID g{};
		if (ref.front() == '{')
		{
			if (FMOD::Studio::parseID(ref.c_str(), &g) != FMOD_OK)
				return {};
		}
		else if (s_StudioSystem->lookupID(ref.c_str(), &g) != FMOD_OK)
			return {};
		char text[64];
		std::snprintf(text, sizeof(text), "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g.Data1, g.Data2,
					  g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6],
					  g.Data4[7]);
		return text;
	}
} // namespace Lux
using namespace Lux;
void Near(float a, float b)
{
	assert(std::abs(a - b) < 0.0001f);
}
void Tick()
{
	auto* s = AudioEngine::GetStudioSystem();
	assert(s->update() == FMOD_OK);
	assert(s->flushCommands() == FMOD_OK);
}
FMOD::Studio::EventDescription* Desc(const char* path)
{
	FMOD::Studio::EventDescription* d = nullptr;
	assert(AudioEngine::GetStudioSystem()->getEvent(path, &d) == FMOD_OK);
	return d;
}
FMOD::Studio::EventInstance* Only(const char* path)
{
	FMOD::Studio::EventInstance* i = nullptr;
	int n = 0;
	assert(Desc(path)->getInstanceList(&i, 1, &n) == FMOD_OK && n == 1);
	return i;
}
std::filesystem::path BankDirectory;
void Load()
{
	auto dir = BankDirectory;
	assert(AudioEngine::LoadBank(dir / "Master.strings.bank"));
	assert(AudioEngine::LoadBank(dir / "Master.bank"));
}

int Count(const char* path)
{
	int n = 0;
	assert(Desc(path)->getInstanceCount(&n) == FMOD_OK);
	return n;
}
void TestPackedTable(const std::filesystem::path& directory)
{
	AudioSurfaceTable table;
	table.Surfaces[2].Footstep = { "{12345678-1234-1234-1234-123456789abc}", "event:/Foot", "Master.bank" };
	const auto text = table.ToYAML();
	const auto path = directory / "surface-pack.bin";
	{
		FileStreamWriter writer(path);
		assert(writer.WriteData("pad", 3));
		writer.WriteRaw<uint64_t>(text.size());
		assert(writer.WriteData(text.data(), text.size()));
		assert(writer.Flush());
	}
	AudioSurfaceTableSerializer serializer;
	AssetPackFile::AssetInfo info{};
	info.PackedOffset = 3;
	info.PackedSize = sizeof(uint64_t) + text.size();
	FileStreamReader reader(path);
	auto loaded = serializer.DeserializeFromAssetPack(reader, info).As<AudioSurfaceTable>();
	assert(loaded && loaded->Surfaces[2].Footstep.Guid == table.Surfaces[2].Footstep.Guid);
	info.PackedSize = 4;
	assert(!serializer.DeserializeFromAssetPack(reader, info));
	info.PackedSize = sizeof(uint64_t) + text.size();
	{
		FileStreamWriter writer(directory / "cut-surface.bin");
		assert(writer.WriteData("pad", 3));
		writer.WriteRaw<uint64_t>(text.size());
		assert(writer.WriteData(text.data(), 4));
	}
	FileStreamReader cut(directory / "cut-surface.bin");
	assert(!serializer.DeserializeFromAssetPack(cut, info));
}

int main(int argc, char** argv)
{
	assert(argc == 3);
	TestPackedTable(argv[2]);
	BankDirectory = argv[1];
	AudioSurfaceTable table;
	table.Surfaces[2].Impact = {"guid", "event:/test", "Master.bank"};
	table.ImpactCooldown = 0.3f;
	AudioSurfaceTable roundtrip;
	assert(roundtrip.FromYAML(table.ToYAML()));
	assert(roundtrip.Surfaces[2].Impact.Guid == "guid" && roundtrip.ImpactCooldown == 0.3f);
	assert(roundtrip.FromYAML("AudioSurfaceTable: {}"));
	assert(roundtrip.ImpactCooldown == 0.15f && !roundtrip.Surfaces[2].Impact.IsValid());
	assert(!roundtrip.FromYAML("AudioSurfaceTable: {MinimumImpulse: -.inf}"));
	assert(!roundtrip.FromYAML("AudioSurfaceTable: {Surfaces: {Bad: {}}}"));
	AudioEngine::Init();
	Load();
	auto hit = AudioEngine::ResolveEventReference("event:/SurfaceHit");
	auto motion = AudioEngine::ResolveEventReference("event:/SurfaceMotion");
	auto one = AudioEventInstance::Create(hit), loop = AudioEventInstance::Create(motion);
	assert(one && one->IsOneShot() && loop && !loop->IsOneShot());
	one = nullptr;
	loop = nullptr;
	Tick();
	PhysicsAudioSystem system;
	PhysicsAudioContact c;
	c.Key = {1, 2, 3, 4};
	c.Owner = 10;
	c.Other = 20;
	c.Impulse = 2;
	c.SlipSpeed = 3;
	c.Material = AcousticMaterial::Metal;
	c.Sounds.Impact.Guid = hit;
	c.Sounds.Scrape.Guid = motion;
	system.Contact(c, table);
	Tick();
	assert(Count("event:/SurfaceHit") == 1);
	float f;
	assert(Only("event:/SurfaceHit")->getParameterByName("Impulse", &f) == FMOD_OK);
	Near(f, 2);
	system.Contact(c, table);
	Tick();
	assert(Count("event:/SurfaceHit") == 1);
	c.Key[2] = 5;
	system.Contact(c, table);
	system.Update(0, false, table);
	Tick();
	assert(Count("event:/SurfaceHit") == 1 && Count("event:/SurfaceMotion") == 1 && system.GetContactCount() == 2);
	assert(Only("event:/SurfaceMotion")->getParameterByName("Speed", &f) == FMOD_OK);
	Near(f, 3);
	system.Update(1, true, table);
	Tick();
	bool paused = false;
	Only("event:/SurfaceMotion")->getPaused(&paused);
	assert(paused);
	c.Key[2] = 6;
	system.Contact(c, table);
	Tick();
	assert(Count("event:/SurfaceHit") == 1); // pause did not advance cooldown
	system.Update(0.4f, false, table);
	Tick();
	c.Key[2] = 7;
	system.Contact(c, table);
	Tick();
	assert(Count("event:/SurfaceHit") == 2);
	// End of one subshape must retain the other manifolds' one shared loop.
	c.End = true;
	system.Contact(c, table);
	system.Update(0, false, table);
	Tick();
	assert(Count("event:/SurfaceMotion") == 1);
	system.Remove(20);
	Tick();
	assert(system.GetContactCount() == 0 && Count("event:/SurfaceMotion") == 0 && Count("event:/SurfaceHit") == 0);
	assert(system.Footstep(10, {hit}, AcousticMaterial::Grass, {1, 2, 3}, 4, 80));
	Tick();
	Only("event:/SurfaceHit")->getParameterByName("Weight", &f);
	Near(f, 80);
	Only("event:/SurfaceHit")->getParameterByName("Speed", &f);
	Near(f, 4);
	Only("event:/SurfaceHit")->getParameterByName("Surface", &f);
	Near(f, 8);
	assert(!system.Footstep(10, {motion}, AcousticMaterial::Grass, {}, 4, 80));
	assert(!system.Footstep(10, {hit}, AcousticMaterial::Grass, {}, NAN, 80));
	system.Clear();
	Tick();
	c.End = false;
	c.Key = {1, 2, 3, 4};
	c.Sounds.Impact = {};
	system.Contact(c, table);
	system.Update(0, false, table);
	Tick();
	assert(Count("event:/SurfaceMotion") == 1);
	AudioEngine::Shutdown();
	AudioEngine::Init();
	Load();
	system.Update(0, false, table);
	Tick();
	assert(Count("event:/SurfaceMotion") == 1);
	c.End = true;
	system.Contact(c, table);
	system.Update(0, false, table);
	Tick();
	for (int i = 0; i < 100 && Count("event:/SurfaceMotion") > 0; ++i)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		system.Update(0, false, table);
		Tick();
	}
	assert(Count("event:/SurfaceMotion") == 0);
	system.Clear();
	AudioEngine::Shutdown();
	std::cout << "PASS: surface table YAML, impact thresholds/cooldown, compound dedup, pause, footsteps, bank reload, "
				 "contact end and destruction\n";
}
