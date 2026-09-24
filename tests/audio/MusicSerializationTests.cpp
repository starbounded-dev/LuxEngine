// The runner inserts the production SceneSerializer music blocks above this test body.
int main()
{
	FakeEntity original;
	original.Music.Event = { "{12345678-1234-1234-1234-123456789abc}", "event:/Music/Score", "Music.bank" };
	original.Music.PlayOnAwake = false;
	original.Music.InitialState = "Combat";
	original.Music.Intensity = 0.7f;
	const auto copied = DeserializeMusic(YAML::Load(SerializeMusic(original)));
	assert(copied.Event.Guid == original.Music.Event.Guid);
	assert(copied.Event.Path == original.Music.Event.Path);
	assert(copied.Event.BankName == original.Music.Event.BankName);
	assert(copied.InitialState == "Combat" && !copied.PlayOnAwake && copied.Intensity == 0.7f);
	const auto defaults = DeserializeMusic(YAML::Load("MusicDirectorComponent: {}"));
	assert(defaults.PlayOnAwake && defaults.Intensity == 0.0f && defaults.InitialState.empty() && !defaults.Event.IsValid());
	for (const char* value : { "-0.1", "1.1", ".nan", ".inf" })
	{
		bool rejected = false;
		try { DeserializeMusic(YAML::Load(std::string("MusicDirectorComponent: { Intensity: ") + value + " }")); }
		catch (const std::runtime_error&) { rejected = true; }
		assert(rejected);
	}
	std::cout << "PASS: production music YAML round-trip, legacy defaults and malformed intensity\n";
}
