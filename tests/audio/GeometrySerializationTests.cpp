// The runner inserts production SceneSerializer portal/mesh blocks above this test.
int main()
{
	FakeEntity original;
	original.Portal.Enabled = false;
	original.Portal.ZoneA = 123;
	original.Portal.ZoneB = 456;
	original.Portal.HalfExtents = { 2, 3, 0.1f };
	original.Portal.Open = 0.75f;
	original.Portal.BlendDistance = 7;
	original.Portal.Material = AcousticMaterial::Metal;
	const auto text = SerializePortal(original);
	const auto copied = DeserializeGeometry(YAML::Load(text));
	assert(SerializePortal(copied) == text);
	assert(copied.Portal.ZoneA == 123 && copied.Portal.ZoneB == 456);
	const auto defaults = DeserializeGeometry(YAML::Load("AudioPortalComponent: {}\nMeshColliderComponent: {}"));
	assert(defaults.Portal.Enabled && defaults.Portal.Open == 0 && defaults.Portal.Material == AcousticMaterial::Wood);
	assert(defaults.Mesh.AcousticMotion == AcousticGeometryMode::Static);
	for (int mode = 0; mode < 3; ++mode)
	{
		const auto entity = DeserializeGeometry(YAML::Load("MeshColliderComponent: { AcousticMotion: " + std::to_string(mode) + " }"));
		assert(static_cast<int>(entity.Mesh.AcousticMotion) == mode);
	}
	for (const char* malformed : { "AudioPortalComponent: { Open: .nan }", "AudioPortalComponent: { Open: 1.1 }",
		"AudioPortalComponent: { HalfExtents: [1, 0, 1] }", "AudioPortalComponent: { HalfExtents: [1, 2] }",
		"AudioPortalComponent: { BlendDistance: .inf }", "AudioPortalComponent: { Material: Nonsense }",
		"MeshColliderComponent: { AcousticMotion: 3 }", "MeshColliderComponent: { AcousticMotion: -1 }" })
	{
		bool rejected = false;
		try { DeserializeGeometry(YAML::Load(malformed)); }
		catch (const std::exception&) { rejected = true; }
		if (!rejected)
			std::cerr << "Accepted invalid YAML: " << malformed << "\n";
		assert(rejected);
	}
	std::cout << "PASS: production portal YAML round-trip, room references, legacy acoustic mode and invalid data\n";
}
