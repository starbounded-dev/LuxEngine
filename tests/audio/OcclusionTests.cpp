#include "lpch.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cassert>
#include <iostream>
#include "Lux/Audio/AudioOcclusion.h"
#include "Lux/Serialization/FileStream.h"
#include "Lux/Serialization/Yaml.h"
#include <glm/gtc/matrix_transform.hpp>
#include <bit>
#include <soundwave/Solve.h>

namespace Lux
{
	std::shared_ptr<spdlog::logger> Log::s_CoreLogger = spdlog::stdout_color_mt("occlusion-test");
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger = Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_EditorConsoleLogger = Log::s_CoreLogger;
}
using namespace Lux;

namespace
{
	void Near(float actual, float expected)
	{
		if (std::abs(actual - expected) >= 0.0001f)
			std::cerr << "Expected " << expected << ", got " << actual << std::endl;
		assert(std::abs(actual - expected) < 0.0001f);
	}
	void SameBits(float actual, float expected)
	{
		assert(std::bit_cast<uint32_t>(actual) == std::bit_cast<uint32_t>(expected));
	}

	void CheckMeasurement(const AudioOcclusionPath& path)
	{
		const auto& measurement = path.Measurement;
		assert(measurement.Crossings.size() == path.Walls.size() && measurement.Bounces == 0);
		SameBits(measurement.Distance, glm::distance(path.From, path.To));
		SameBits(measurement.From.X, path.From.x);
		SameBits(measurement.From.Y, path.From.y);
		SameBits(measurement.From.Z, path.From.z);
		SameBits(measurement.To.X, path.To.x);
		SameBits(measurement.To.Y, path.To.y);
		SameBits(measurement.To.Z, path.To.z);
		float lossLF = 0.0f, lossHF = 0.0f;
		for (size_t i = 0; i < path.Walls.size(); ++i)
		{
			const auto& wall = path.Walls[i];
			const auto& crossing = measurement.Crossings[i];
			assert(crossing.Material == static_cast<Soundwave::MaterialTag>(wall.Material));
			assert(crossing.OneSided == wall.Flat && crossing.Portal == wall.Portal);
			SameBits(crossing.Start, wall.Start);
			SameBits(crossing.End, wall.End);
			SameBits(crossing.LossLF, wall.LossLF);
			SameBits(crossing.LossHF, wall.LossHF);
			lossLF += wall.LossLF;
			lossHF += wall.LossHF;
		}
		SameBits(measurement.LossLF, std::min(lossLF, 60.0f));
		SameBits(measurement.LossHF, std::min(lossHF, 60.0f));
		SameBits(path.LossLF, measurement.LossLF);
		SameBits(path.LossHF, measurement.LossHF);
		SameBits(measurement.GainLF(), std::pow(10.0f, -path.LossLF / 20.0f));
	}

	float SolidLoss(AcousticMaterial material, float thickness)
	{
		return 30.0f * thickness / GetDefaultAcousticMaterialProperties(material).TransmissionLF;
	}

	float FlatLoss(AcousticMaterial material)
	{
		return -10.0f * std::log10(1.0f - GetDefaultAcousticMaterialProperties(material).FlatTransmissionLF);
	}
	void CheckSoundwaveDirectOracle()
	{
		Soundwave::Grid grid;
		Soundwave::DirectScratch scratch;
		Soundwave::Path actual;
		Soundwave::Settings kernelSettings;
		kernelSettings.ReflectionDepth = 0;
		kernelSettings.OpeningGridSize = 1;
		kernelSettings.MaxBounces = 1;
		const auto materials = Soundwave::DefaultMaterials();
		AudioOcclusion oracle;
		AudioOcclusionPath expected;
		AcousticGeometry unitBox;
		AudioGeometrySystem::BuildPortal(unitBox);
		std::vector<Soundwave::Triangle> triangles;
		std::vector<AudioOcclusionHit> hits;
		std::vector<Soundwave::RayHit> rawHits;
		const auto position = [](const glm::vec3& p) { return Soundwave::Position{ p.x,p.y,p.z }; };
		const auto compare = [&](const glm::vec3& from, const glm::vec3& to, std::span<const Soundwave::PortalBox> portals)
		{
			assert(grid.Trace(position(from), position(to), 0, rawHits));
			hits.clear();
			for (const auto& hit : rawHits)
				hits.push_back({ grid.GetTriangles()[hit.TriangleIndex].Instance, hit.Distance, hit.Exit });
			oracle.Evaluate(from, to, hits, expected);
			assert(Soundwave::EvaluateDirect(grid, portals, materials, kernelSettings, position(from), position(to), 0, scratch, actual) ==
				Soundwave::EvaluationError::None);
			CheckMeasurement(expected);
			SameBits(actual.Distance, expected.Measurement.Distance);
			SameBits(actual.LossLF, expected.LossLF);
			SameBits(actual.LossHF, expected.LossHF);
			SameBits(actual.GainLF(), expected.Measurement.GainLF());
			assert(actual.Crossings.size() == expected.Walls.size());
			for (size_t i = 0; i < actual.Crossings.size(); ++i)
			{
				const auto& a = actual.Crossings[i];
				const auto& b = expected.Walls[i];
				SameBits(a.Start, b.Start);
				SameBits(a.End, b.End);
				SameBits(a.LossLF, b.LossLF);
				SameBits(a.LossHF, b.LossHF);
				assert(a.Material == static_cast<Soundwave::MaterialTag>(b.Material) && a.OneSided == b.Flat && a.Portal == b.Portal);
			}
		};
		AudioGeometryInput mesh;
		mesh.Entity = 10;
		for (size_t tag = 0; tag < AcousticMaterialCount; ++tag)
		{
			mesh.Material = static_cast<AcousticMaterial>(tag);
			triangles.clear();
			for (size_t i = 0; i < unitBox.Triangles.size(); i += 3)
			{
				const auto point = [&](size_t index) { return position(unitBox.Triangles[index] * glm::vec3(0.125f,1,1)); };
				triangles.push_back({ point(i), point(i + 1), point(i + 2), 10,0,static_cast<Soundwave::MaterialTag>(tag),false });
			}
			assert(grid.Build(triangles) == Soundwave::GeometryError::None);
			for (float strength : { 0.0f, 0.125f, 1.0f, 2.75f, 4.0f })
			{
				AudioOcclusionSettings settings;
				settings.Strength = kernelSettings.Strength = strength;
				oracle.Configure({}, settings);
				oracle.SetGeometry({ &mesh,1 }, {});
				compare({ -1,0,0 }, { 1,0,0 }, {});
				compare({ 1,0,0 }, { -1,0,0 }, {});
				compare({ 0,0,0 }, { 1,0,0 }, {});
			}
		}
		assert(grid.Build({}) == Soundwave::GeometryError::None);
		AudioGeometryInput portal;
		portal.Entity = 42;
		portal.Portal = true;
		portal.Mode = AcousticGeometryMode::Dynamic;
		portal.HalfExtents = { 0.75f,1.5f,0.125f };
		portal.Transform = glm::translate(glm::mat4(1), { 3.5f,-1.25f,2.0f }) *
			glm::rotate(glm::mat4(1), glm::radians(27.0f), glm::vec3(0,0,1));
		AudioOcclusionSettings settings;
		settings.Strength = kernelSettings.Strength = 0.5f;
		oracle.Configure({}, settings);
		std::vector<Soundwave::PortalBox> boxes;
		for (float extent : { 0.75f,0.315f })
		{
			portal.HalfExtents.x = extent;
			for (float open : { 0.0f,0.25f,0.5f,0.75f,1.0f })
			{
				portal.Open = open;
				oracle.SetGeometry({ &portal,1 }, [open](Lux::UUID) { return open; });
				boxes.clear();
				const glm::mat4 applied = AudioGeometrySystem::PortalTransform(portal);
				if (open < 1)
				{
					Soundwave::PortalBox box;
					box.Instance = 42;
					const glm::mat4 inverse = glm::inverse(applied);
					for (uint32_t c = 0; c < 4; ++c)
						for (uint32_t r = 0; r < 4; ++r)
							box.WorldToBox[c * 4 + r] = inverse[c][r];
					boxes.push_back(box);
				}
				compare(glm::vec3(portal.Transform * glm::vec4(-2,0,0,1)), glm::vec3(portal.Transform * glm::vec4(2,0,0,1)), boxes);
				compare(glm::vec3(applied * glm::vec4(-0.5f,0,0,1)), glm::vec3(applied * glm::vec4(0.5f,0,0,1)), boxes);
				compare(glm::vec3(applied[3]), glm::vec3(portal.Transform * glm::vec4(2,0,0,1)), boxes);
			}
		}
		std::cout << "PASS: CPU direct kernel matches production oracle bit-for-bit for 23 materials, strengths, reversed/inside solids and transformed portal opening states\n";
	}
}

void RunAudioAcousticsTests();

int main(int argc, char** argv)
{
	assert(argc == 2);
	const std::filesystem::path output = argv[1];

	// Settings: defaults, rejected values, YAML and bounded binary round trips.
	AudioOcclusionSettings settings;
	assert(settings.Validate() && settings.Source == AudioOcclusionSource::Engine && settings.CastBudget == 32);
	// A project saved before the setting has no Occlusion key: the lookup is undefined and loads defaults.
	assert(settings.DeserializeYAML(Yaml::Load("{ Other: 1 }")["Occlusion"]) && settings.Source == AudioOcclusionSource::Engine);
	for (const char* yaml : { "Source: Muffled", "UpdateRateHz: 0", "UpdateRateHz: 500", "CastBudget: 0", "CastBudget: 5000",
		"Strength: -1", "Strength: .nan", "Strength: 9", "[1, 2]" })
		assert(!settings.DeserializeYAML(Yaml::Load(yaml)) && settings.UpdateRateHz == 20.0f);
	assert(settings.DeserializeYAML(Yaml::Load("{ Source: Raytraced, UpdateRateHz: 10, CastBudget: 4, Strength: 0.5 }")));
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


	// The existing binary format is a length-delimited YAML block. Old blocks without
	// Kernel still load; versioned rows round-trip through both project and export paths.
	const std::string oldBlock = "{ Source: Engine, UpdateRateHz: 12, CastBudget: 7, Strength: 0.25 }";
	{
		FileStreamWriter writer(file);
		writer.WriteRaw<uint32_t>(static_cast<uint32_t>(oldBlock.size()));
		assert(writer.WriteData(oldBlock.data(), oldBlock.size()));
	}
	{
		FileStreamReader reader(file);
		assert(copied.Deserialize(reader) && copied.UpdateRateHz == 12 && copied.CastBudget == 7 &&
			copied.ReflectionDepth == 2 && copied.OpeningGridSize == 8 && copied.MaxBounces == 2);
	}
	AudioOcclusionSettings authored;
	assert(authored.DeserializeYAML(Yaml::Load(
		"{ Strength: 0.75, Kernel: { Version: 1, ReflectionDepth: 3, OpeningGridSize: 12, MaxBounces: 4, "
		"CandidateBudget: 1234, PathBudget: 20, OpeningNodeBudget: 48, Absorption: 0.35, CompareRaycast: true } }")));
	const auto checkAuthored = [&](const AudioOcclusionSettings& actual)
	{
		assert(actual.ReflectionDepth == 3 && actual.OpeningGridSize == 12 && actual.MaxBounces == 4 &&
			actual.CandidateBudget == 1234 && actual.PathBudget == 20 && actual.OpeningNodeBudget == 48 && actual.CompareRaycast);
		SameBits(actual.Absorption, 0.35f);
		const auto kernel = actual.GetKernelSettings();
		assert(kernel.Validate() && kernel.ReflectionDepth == 3 && kernel.OpeningGridSize == 12 && kernel.MaxBounces == 4 &&
			kernel.CandidateBudget == 1234 && kernel.PathBudget == 20 && kernel.OpeningNodeBudget == 48);
		SameBits(kernel.Absorption, actual.Absorption);
		SameBits(kernel.Strength, 0.75f);
	};
	checkAuthored(authored);
	Yaml::Writer settingsYaml;
	authored.SerializeYAML(settingsYaml);
	assert(copied.DeserializeYAML(Yaml::Load(settingsYaml.c_str())));
	checkAuthored(copied);
	{
		FileStreamWriter writer(file);
		assert(authored.Serialize(writer));
	}
	{
		FileStreamReader reader(file);
		assert(copied.Deserialize(reader));
	}
	checkAuthored(copied);
	for (const char* kernel : { "~", "[1,2]", "{ Version: 2 }", "{ Version: text }", "{ MaxBounces: -1 }",
		"{ MaxBounces: 17 }", "{ ReflectionDepth: 3, MaxBounces: 2 }", "{ OpeningGridSize: 0 }",
		"{ OpeningGridSize: 33 }", "{ CandidateBudget: 0 }", "{ PathBudget: 4097 }", "{ OpeningNodeBudget: 2049 }",
		"{ Absorption: .nan }", "{ Absorption: -0.1 }", "{ Absorption: 1.1 }", "{ Absorption: text }",
		"{ CompareRaycast: maybe }", "{ ReflectionDepth: ~ }", "{ PathBudget: [1,2] }",
		"{ MaxBounce: 2 }", "{ MaxBounces: 2, MaxBounces: 3 }" })
	{
		const auto text = std::string("Kernel: ") + kernel;
		assert(!copied.DeserializeYAML(Yaml::Load(text)));
		checkAuthored(copied); // rejection is transactional
		{
			FileStreamWriter writer(file);
			writer.WriteRaw<uint32_t>(static_cast<uint32_t>(text.size()));
			assert(writer.WriteData(text.data(), text.size()));
		}
		{
			FileStreamReader reader(file);
			assert(!copied.Deserialize(reader));
		}
		checkAuthored(copied);
	}
	assert(copied.DeserializeYAML(Yaml::Load("Kernel: {}")) && copied.ReflectionDepth == 2 && !copied.CompareRaycast);
	assert(copied.DeserializeYAML(Yaml::Load("Kernel: { ReflectionDepth: 0, OpeningGridSize: 1, MaxBounces: 1 }")) &&
		copied.GetKernelSettings().Validate() && copied.ReflectionDepth == 0);
	std::cout << "PASS: versioned kernel settings, old project/export defaults, strict validation and transactional round trips\n";

	CheckSoundwaveDirectOracle();
	RunAudioAcousticsTests();

	// The old engine accessor is an independent baseline until its Phase 7 removal.
	for (size_t i = 0; i < AcousticMaterialCount; ++i)
	{
		const auto expected = GetDefaultAcousticMaterialProperties(static_cast<AcousticMaterial>(i));
		const auto& actual = Soundwave::GetDefaultMaterialProperties(static_cast<Soundwave::MaterialTag>(i));
		SameBits(actual.AbsorptionLF, expected.AbsorptionLF);
		SameBits(actual.AbsorptionHF, expected.AbsorptionHF);
		SameBits(actual.Scattering, expected.Scattering);
		SameBits(actual.TransmissionLF, expected.TransmissionLF);
		SameBits(actual.TransmissionHF, expected.TransmissionHF);
		SameBits(actual.FlatTransmissionLF, expected.FlatTransmissionLF);
		SameBits(actual.FlatTransmissionHF, expected.FlatTransmissionHF);
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
	CheckMeasurement(path);
	assert(path.Walls.empty() && path.LossLF == 0.0f);

	// Unsorted input, a duplicate entry/exit from a shared edge, and a non-acoustic entity.
	hits = { { 10, 4.3f, true }, { 99, 2.0f, false }, { 10, 4.0f, false }, { 10, 4.0005f, false }, { 10, 4.3005f, true } };
	occlusion.Evaluate(from, to, hits, path);
	CheckMeasurement(path);
	assert(path.Walls.size() == 1 && !path.Walls[0].Flat && !path.Walls[0].Portal);
	assert(path.Walls[0].Entity == 10 && path.Walls[0].Material == AcousticMaterial::Brick);
	Near(path.Walls[0].Start, 4.0f);
	Near(path.Walls[0].End, 4.3f);
	Near(path.LossLF, SolidLoss(AcousticMaterial::Brick, 0.3f));
	assert(path.LossHF > path.LossLF);
	const float thinLoss = path.LossLF;

	hits = { { 10, 4.0f, false }, { 10, 4.6f, true } };
	occlusion.Evaluate(from, to, hits, path);
	CheckMeasurement(path);
	assert(path.LossLF > thinLoss);

	// A single crossing is a one-sided surface, from either side.
	for (bool exit : { false, true })
	{
		hits = { { 20, 5.0f, exit } };
		occlusion.Evaluate(from, to, hits, path);
		CheckMeasurement(path);
		assert(path.Walls.size() == 1 && path.Walls[0].Flat);
		Near(path.LossLF, FlatLoss(AcousticMaterial::Plaster));
	}

	// Walls add up; crossings outside the segment do not count.
	hits = { { 10, 2.0f, false }, { 10, 2.3f, true }, { 20, 6.0f, false }, { 10, 12.0f, false }, { 10, 12.3f, true } };
	occlusion.Evaluate(from, to, hits, path);
	CheckMeasurement(path);
	assert(path.Walls.size() == 2 && path.Walls[0].Start < path.Walls[1].Start);
	Near(path.LossLF, SolidLoss(AcousticMaterial::Brick, 0.3f) + FlatLoss(AcousticMaterial::Plaster));

	hits.clear();
	for (int i = 0; i < 20; ++i)
	{
		hits.push_back({ 10, 0.4f * i + 0.1f, false });
		hits.push_back({ 10, 0.4f * i + 0.35f, true });
	}
	occlusion.Evaluate(from, to, hits, path);
	CheckMeasurement(path);
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
	occlusion.SetGeometry(std::span(&portal, 1), [&](Lux::UUID entity) { assert(entity == 30); return open; });
	hits.clear();
	occlusion.Evaluate(offsetFrom, offsetTo, hits, path);
	CheckMeasurement(path);
	assert(path.Walls.size() == 1 && path.Walls[0].Portal && path.Walls[0].Material == AcousticMaterial::Wood);
	Near(path.Walls[0].End - path.Walls[0].Start, 0.1f);
	Near(path.Walls[0].Start, 6.95f);
	for (float value : { 0.5f, 1.0f })
	{
		open = value;
		occlusion.SetGeometry(std::span(&portal, 1), [&](Lux::UUID) { return open; });
		occlusion.Evaluate(offsetFrom, offsetTo, hits, path);
		CheckMeasurement(path);
		assert(path.Walls.empty());
	}

	// Scheduling: a 32-cast budget, round-robin, snap on first result, then smoothing and pruning.
	occlusion.SetGeometry(geometry, {});
	std::vector<AudioOcclusion::Source> sources;
	for (int i = 0; i < 40; ++i)
		sources.push_back({ static_cast<Lux::UUID>(1000 + i), glm::vec3(10.0f, 0.0f, 0.0f) });
	int casts = 0;
	bool wall = true;
	auto cast = [&](Lux::UUID, const glm::vec3&, const glm::vec3&, std::vector<AudioOcclusionHit>& out)
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
	CheckMeasurement(path);
	Near(path.LossLF, 0.5f * (SolidLoss(AcousticMaterial::Brick, 0.3f) + FlatLoss(AcousticMaterial::Plaster)));
	casts = 0;
	occlusion.Update(0.016f, from, sources, cast);
	assert(casts == 4);

	std::cout << "PASS: occlusion settings YAML/binary/limits, solid/flat walls, duplicate crossings, range, loss cap, portal shutters, strength, cast budget, smoothing and pruning\n";
}
