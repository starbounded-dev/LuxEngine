#include "AudioTestHost.h"
#include "Lux/Audio/AudioOcclusion.h"
#include "Lux/Serialization/FileStream.h"
#include <yaml-cpp/yaml.h>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
	float SolidLoss(AcousticMaterial material, float thickness)
	{
		return 30.0f * thickness / GetDefaultAcousticMaterialProperties(material).TransmissionLF;
	}

	float FlatLoss(AcousticMaterial material)
	{
		return -10.0f * std::log10(1.0f - GetDefaultAcousticMaterialProperties(material).FlatTransmissionLF);
	}
}

int main(int argc, char** argv)
{
	assert(argc == 2);
	const std::filesystem::path output = argv[1];

	// Settings: defaults, rejected values, YAML and bounded binary round trips.
	AudioOcclusionSettings settings;
	assert(settings.Validate() && settings.Source == AudioOcclusionSource::Engine && settings.CastBudget == 32);
	// A project saved before the setting has no Occlusion key: the lookup is undefined and loads defaults.
	assert(settings.DeserializeYAML(YAML::Load("{ Other: 1 }")["Occlusion"]) && settings.Source == AudioOcclusionSource::Engine);
	for (const char* yaml : { "Source: Muffled", "UpdateRateHz: 0", "UpdateRateHz: 500", "CastBudget: 0", "CastBudget: 5000",
		"Strength: -1", "Strength: .nan", "Strength: 9", "[1, 2]" })
		assert(!settings.DeserializeYAML(YAML::Load(yaml)) && settings.UpdateRateHz == 20.0f);
	assert(settings.DeserializeYAML(YAML::Load("{ Source: Raytraced, UpdateRateHz: 10, CastBudget: 4, Strength: 0.5 }")));
	assert(settings.Source == AudioOcclusionSource::Raytraced && settings.UpdateRateHz == 10.0f && settings.CastBudget == 4 && settings.Strength == 0.5f);
	const auto file = output / "occlusion-settings.bin";
	{
		FileStreamWriter writer(file);
		assert(settings.Serialize(writer));
	}
	AudioOcclusionSettings copied;
	{
		FileStreamReader reader(file);
		assert(copied.Deserialize(reader));
	}
	assert(copied.Source == settings.Source && copied.UpdateRateHz == 10.0f && copied.CastBudget == 4 && copied.Strength == 0.5f);
	std::filesystem::resize_file(file, std::filesystem::file_size(file) - 1);
	{
		FileStreamReader reader(file);
		assert(!copied.Deserialize(reader));
	}

	AudioOcclusion occlusion;
	occlusion.Configure({});
	std::vector<AudioGeometryInput> geometry(2);
	geometry[0].Entity = 10;
	geometry[0].Material = AcousticMaterial::Brick;
	geometry[1].Entity = 20;
	geometry[1].Material = AcousticMaterial::Plaster;
	occlusion.SetGeometry(geometry, {});

	const glm::vec3 from(0.0f);
	const glm::vec3 to(10.0f, 0.0f, 0.0f);
	AudioOcclusionPath path;
	std::vector<AudioOcclusionHit> hits;
	occlusion.Evaluate(from, to, hits, path);
	assert(path.Walls.empty() && path.LossLF == 0.0f);

	// Unsorted input, a duplicate entry/exit from a shared edge, and a non-acoustic entity.
	hits = { { 10, 4.3f, true }, { 99, 2.0f, false }, { 10, 4.0f, false }, { 10, 4.0005f, false }, { 10, 4.3005f, true } };
	occlusion.Evaluate(from, to, hits, path);
	assert(path.Walls.size() == 1 && !path.Walls[0].Flat && !path.Walls[0].Portal);
	assert(path.Walls[0].Entity == 10 && path.Walls[0].Material == AcousticMaterial::Brick);
	Near(path.Walls[0].Start, 4.0f);
	Near(path.Walls[0].End, 4.3f);
	Near(path.LossLF, SolidLoss(AcousticMaterial::Brick, 0.3f));
	assert(path.LossHF > path.LossLF);
	const float thinLoss = path.LossLF;

	hits = { { 10, 4.0f, false }, { 10, 4.6f, true } };
	occlusion.Evaluate(from, to, hits, path);
	assert(path.LossLF > thinLoss);

	// A single crossing is a one-sided surface, from either side.
	for (bool exit : { false, true })
	{
		hits = { { 20, 5.0f, exit } };
		occlusion.Evaluate(from, to, hits, path);
		assert(path.Walls.size() == 1 && path.Walls[0].Flat);
		Near(path.LossLF, FlatLoss(AcousticMaterial::Plaster));
	}

	// Walls add up; crossings outside the segment do not count.
	hits = { { 10, 2.0f, false }, { 10, 2.3f, true }, { 20, 6.0f, false }, { 10, 12.0f, false }, { 10, 12.3f, true } };
	occlusion.Evaluate(from, to, hits, path);
	assert(path.Walls.size() == 2 && path.Walls[0].Start < path.Walls[1].Start);
	Near(path.LossLF, SolidLoss(AcousticMaterial::Brick, 0.3f) + FlatLoss(AcousticMaterial::Plaster));

	hits.clear();
	for (int i = 0; i < 20; ++i)
	{
		hits.push_back({ 10, 0.4f * i + 0.1f, false });
		hits.push_back({ 10, 0.4f * i + 0.35f, true });
	}
	occlusion.Evaluate(from, to, hits, path);
	assert(path.LossLF == 60.0f);

	// A portal shutter turned so its 0.1 m thickness faces the path, which passes 0.3 m off centre.
	AudioGeometryInput portal;
	portal.Entity = 30;
	portal.Portal = true;
	portal.Mode = AcousticGeometryMode::Dynamic;
	portal.Material = AcousticMaterial::Wood;
	portal.Transform = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(7.0f, 0.0f, 0.0f)), glm::half_pi<float>(), glm::vec3(0, 1, 0));
	const glm::vec3 offsetFrom(0.0f, 0.0f, -0.3f);
	const glm::vec3 offsetTo(10.0f, 0.0f, -0.3f);
	float open = 0.0f;
	occlusion.SetGeometry(std::span(&portal, 1), [&](UUID entity) { assert(entity == 30); return open; });
	hits.clear();
	occlusion.Evaluate(offsetFrom, offsetTo, hits, path);
	assert(path.Walls.size() == 1 && path.Walls[0].Portal && path.Walls[0].Material == AcousticMaterial::Wood);
	Near(path.Walls[0].End - path.Walls[0].Start, 0.1f);
	Near(path.Walls[0].Start, 6.95f);
	for (float value : { 0.5f, 1.0f })
	{
		open = value;
		occlusion.SetGeometry(std::span(&portal, 1), [&](UUID) { return open; });
		occlusion.Evaluate(offsetFrom, offsetTo, hits, path);
		assert(path.Walls.empty());
	}

	// Scheduling: a 32-cast budget, round-robin, snap on first result, then smoothing and pruning.
	occlusion.SetGeometry(geometry, {});
	std::vector<AudioOcclusion::Source> sources;
	for (int i = 0; i < 40; ++i)
		sources.push_back({ static_cast<UUID>(1000 + i), glm::vec3(10.0f, 0.0f, 0.0f) });
	int casts = 0;
	bool wall = true;
	auto cast = [&](UUID, const glm::vec3&, const glm::vec3&, std::vector<AudioOcclusionHit>& out)
		{
			++casts;
			if (wall)
			{
				out.push_back({ 10, 4.0f, false });
				out.push_back({ 10, 4.3f, true });
			}
		};
	occlusion.Update(0.016f, from, sources, cast);
	assert(casts == 32);
	assert(occlusion.GetPath(1000) && occlusion.GetGainLF(1039) == 1.0f && !occlusion.GetPath(1039));
	const float blocked = std::pow(10.0f, -SolidLoss(AcousticMaterial::Brick, 0.3f) / 20.0f);
	Near(occlusion.GetGainLF(1000), blocked);
	occlusion.Update(0.016f, from, sources, cast);
	assert(casts == 40 && occlusion.GetPath(1039));

	wall = false;
	casts = 0;
	occlusion.Update(0.05f, from, sources, cast);
	assert(casts == 32);
	const float easing = occlusion.GetGainLF(1000);
	assert(easing > blocked && easing < 1.0f);
	for (int i = 0; i < 40; ++i)
		occlusion.Update(0.05f, from, sources, cast);
	assert(occlusion.GetGainLF(1000) > 0.999f);

	sources.resize(10);
	occlusion.Update(0.016f, from, sources, cast);
	assert(!occlusion.GetPath(1020) && occlusion.GetGainLF(1020) == 1.0f && occlusion.GetPath(1005));
	occlusion.Clear();
	assert(!occlusion.GetPath(1005));

	// Strength scales every wall and the cast budget comes from the settings.
	AudioOcclusionSettings tuned;
	tuned.Strength = 0.5f;
	tuned.CastBudget = 4;
	occlusion.Configure({}, tuned);
	occlusion.SetGeometry(geometry, {});
	hits = { { 10, 4.0f, false }, { 10, 4.3f, true }, { 20, 6.0f, false } };
	occlusion.Evaluate(from, to, hits, path);
	Near(path.LossLF, 0.5f * (SolidLoss(AcousticMaterial::Brick, 0.3f) + FlatLoss(AcousticMaterial::Plaster)));
	casts = 0;
	occlusion.Update(0.016f, from, sources, cast);
	assert(casts == 4);

	std::cout << "PASS: occlusion settings YAML/binary/limits, solid/flat walls, duplicate crossings, range, loss cap, portal shutters, strength, cast budget, smoothing and pruning\n";
}
