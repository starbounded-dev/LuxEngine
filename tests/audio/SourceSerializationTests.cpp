int main()
{
	const auto legacy = DeserializeSource(YAML::Load("AudioSourceComponent: { Audio: 42, PlayOnAwake: false }"));
	assert(legacy.Priority == 128 && !legacy.DistanceCulling && legacy.LegacyAudio == 42 && !legacy.Config.PlayOnAwake);
	FakeEntity entity;
	entity.Source.Priority = 0;
	entity.Source.DistanceCulling = true;
	entity.Source.ScriptPaused = true;
	entity.Source.Event = { "{12345678-1234-1234-1234-123456789abc}", "event:/Loop", "Main.bank" };
	entity.Source.ParameterOverrides = { { "Speed", 3.0f } };
	for (int priority : { 0, 128, 256 })
	{
		entity.Source.Priority = priority;
		const auto saved = DeserializeSource(YAML::Load(SerializeSource(entity)));
		assert(saved.Priority == priority && saved.DistanceCulling && !saved.ScriptPaused);
		assert(saved.Event.Guid == entity.Source.Event.Guid && saved.ParameterOverrides == entity.Source.ParameterOverrides);
	}
	for (const char* bad : { "Priority: -1", "Priority: 257", "Priority: garbage", "DistanceCulling: maybe" })
	{
		bool rejected = false;
		try { DeserializeSource(YAML::Load(std::string("AudioSourceComponent: { ") + bad + " }")); }
		catch (const std::exception&) { rejected = true; }
		assert(rejected);
	}
	std::cout << "PASS: production source YAML defaults, priority/culling roundtrip, invalid values and runtime-state exclusion\n";
}
