#include "AudioTestHost.h"
#include "Lux/Audio/PhysicsAudioSystem.h"
#include "Lux/Asset/AudioSurfaceTableSerializer.h"
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
