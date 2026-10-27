// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/RaytracedAudioScene.h"
#include "Lux/Serialization/Yaml.h"
#include <asterwave/simulation.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cstdlib>
#include <cmath>
#include <iostream>

// Only logging is supplied by this host. All scene, material and native simulation code is real.
namespace Lux
{
	std::shared_ptr<spdlog::logger> Log::s_CoreLogger = spdlog::stdout_color_mt("asterwave-test");
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger = Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_EditorConsoleLogger = Log::s_CoreLogger;
}

namespace
{
	using namespace Lux;

	void Check(bool condition, const char* expression, int line)
	{
		if (!condition)
		{
			std::cerr << "FAIL at line " << line << ": " << expression << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

	void Near(float actual, float expected, float tolerance = 0.0001f)
	{
		if (!std::isfinite(actual) || !std::isfinite(expected) || std::abs(actual - expected) > tolerance)
		{
			std::cerr << "Expected " << expected << ", got " << actual << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

	AcousticGeometry Room()
	{
		constexpr int indices[] = { 0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,4,6, 0,6,2,
			1,3,7, 1,7,5, 0,1,5, 0,5,4, 2,6,7, 2,7,3 };
		AcousticGeometry geometry;
		geometry.Triangles.reserve(72);
		for (size_t i = 0; i < std::size(indices); i += 3)
		{
			for (const int index : { indices[i], indices[i + 1], indices[i + 2], indices[i], indices[i + 2], indices[i + 1] })
				geometry.Triangles.emplace_back(index & 1 ? 1 : -1, index & 2 ? 1 : -1, index & 4 ? 1 : -1);
		}
		return geometry;
	}

	void Tick(RaytracedAudioScene& scene)
	{
		scene.OnUpdate(0.016f);
		scene.WaitForResults();
		CHECK(scene.IsRunning());
		CHECK(!scene.GetStats().ThreadsRunning);
	}

	void Materials()
	{
		AcousticMaterialSettings settings;
		for (size_t i = 0; i < AcousticMaterialCount; ++i)
		{
			const auto material = static_cast<AcousticMaterial>(i);
			CHECK(AcousticMaterialID(material) == 1000 + static_cast<int>(i));
			CHECK(GetDefaultAcousticMaterialProperties(material).IsValid());
		}
		Near(GetDefaultAcousticMaterialProperties(AcousticMaterial::Default).AbsorptionLF,
			GetDefaultAcousticMaterialProperties(AcousticMaterial::Concrete).AbsorptionLF);
		auto& carpet = settings.Overrides[static_cast<size_t>(AcousticMaterial::Carpet)];
		carpet.Enabled = true;
		carpet.Properties = GetDefaultAcousticMaterialProperties(AcousticMaterial::Carpet);
		carpet.Properties.AbsorptionLF = 0.37f;
		carpet.Properties.FlatTransmissionHF = 1.25f;
		carpet.Properties.TransmissionLF = std::numeric_limits<float>::denorm_min();
		asterwave::World world;
		CHECK(ConfigureAcousticMaterials(world, settings));
		Near(world.material(AcousticMaterialID(AcousticMaterial::Carpet)).AbsorptionLF(), 0.37f);
		Near(world.material(AcousticMaterialID(AcousticMaterial::Carpet)).FlatTransmissionHF(), 1.0f);
		const float transmission = world.material(AcousticMaterialID(AcousticMaterial::Carpet)).TransmissionLF();
		CHECK(transmission > 0 && std::isfinite(30.0f / transmission));
		Near(world.material(AcousticMaterialID(AcousticMaterial::Cloth)).AbsorptionLF(),
			GetDefaultAcousticMaterialProperties(AcousticMaterial::Cloth).AbsorptionLF);
		Yaml::Writer text;
		settings.SerializeYAML(text);
		AcousticMaterialSettings loaded;
		CHECK(loaded.DeserializeYAML(Yaml::Load(text.c_str())));
		CHECK(loaded.Overrides[static_cast<size_t>(AcousticMaterial::Carpet)].Enabled);
		Near(loaded.Overrides[static_cast<size_t>(AcousticMaterial::Carpet)].Properties.AbsorptionLF, 0.37f);
		Near(loaded.Overrides[static_cast<size_t>(AcousticMaterial::Carpet)].Properties.FlatTransmissionHF, 1.25f);
		CHECK(loaded.Overrides[static_cast<size_t>(AcousticMaterial::Carpet)].Properties.TransmissionLF == std::numeric_limits<float>::denorm_min());
		CHECK(!loaded.Overrides[static_cast<size_t>(AcousticMaterial::Cloth)].Enabled);
		AcousticMaterialSettings invalid = settings;
		invalid.Overrides[static_cast<size_t>(AcousticMaterial::Carpet)].Properties.TransmissionLF = 0;
		CHECK(!ConfigureAcousticMaterials(world, invalid));
		Near(world.material(AcousticMaterialID(AcousticMaterial::Carpet)).AbsorptionLF(), 0.37f);
	}

	void SceneLifecycle()
	{
		RaytracedAudioScene scene(nullptr);
		CHECK(RaytracedAudioScene::IsAvailable());
		CHECK(!scene.IsRunning());
		CHECK(!scene.GetResult(1).Valid && !scene.GetAmbience().Valid);
		scene.Start();
		scene.Start();
		CHECK(scene.IsRunning() && scene.GetStats().EmitterCount == 1);
		scene.CreateEmitter(1);
		scene.CreateEmitter(1);
		scene.SetEmitterPosition(1, { 6, 0, 0 });
		scene.SetEmitterMaxVolume(1, 0.5f);
		// Authored playback boosts are valid (the inspector permits 0..2). The native energy
		// fraction remains bounded, and invalid values must not poison the next batch.
		scene.SetEmitterMaxVolume(1, 2.0f);
		scene.SetEmitterMaxVolume(1, std::numeric_limits<float>::quiet_NaN());
		scene.SetEmitterMaxVolume(1, std::numeric_limits<float>::infinity());
		scene.SetEmitterMaxVolume(1, -1.0f);
		CHECK(scene.GetStats().SourceEmitterCount == 1);
		CHECK(!scene.GetResult(1).Valid);
		scene.SetVisualisationEnabled(true, 32, 3, 1);
		Tick(scene);
		const auto open = scene.GetResult(1);
		CHECK(open.Valid && open.OcclusionGainLF > 0 && open.OcclusionGainLF <= 1);
		CHECK(open.OcclusionGainHF > 0 && open.OcclusionGainHF <= 1);
		CHECK(scene.GetAmbience().Valid && scene.GetAmbience().Reverb.Valid);
		RaytracedAudioVisualisation rays;
		scene.GetVisualisation(rays);
		CHECK(rays.RayCount == 32 && rays.BounceCount == 3 && rays.Bounces.size() == 96);
		CHECK(std::none_of(rays.Bounces.begin(), rays.Bounces.end(), [](const auto& bounce) { return bounce.Hit; }));
		const auto stats = scene.GetStats();
		CHECK(stats.VersionMajor == 0 && stats.VersionMinor == 1 && stats.VersionPatch == 0);
		CHECK(stats.RaysCastThisFrame > 0 && stats.WorkItemCount > 0 && stats.MaximumConcurrencyLevel > 0);
		CHECK(stats.RaytracingTimeMs >= 0 && stats.MainThreadTimeMs >= 0 && stats.LatencyMs >= 0);

		scene.OnUpdate(0.016f);
		// Read-only editor consumers see the last published values while a new batch runs.
		Near(scene.GetResult(1).OcclusionGainLF, open.OcclusionGainLF);
		scene.SetVisualisationEnabled(false, 0, 0, 0);
		scene.GetVisualisation(rays);
		CHECK(rays.Bounces.empty() && rays.RayCount == 0);
		scene.WaitForResults();
		scene.GetVisualisation(rays);
		CHECK(rays.Bounces.empty());

		// Both creation and destruction may occur in post-update script queues.
		scene.OnUpdate(0.016f);
		scene.CreateEmitter(2);
		CHECK(!scene.GetResult(2).Valid);
		scene.DestroyEmitter(1);
		CHECK(!scene.GetResult(1).Valid && scene.GetStats().SourceEmitterCount == 1);
		scene.SetEmitterPosition(2, { 2, 0, 0 });
		Tick(scene);
		CHECK(scene.GetResult(2).Valid);
		scene.OnUpdate(0.016f);
		scene.Stop();
		scene.Stop();
		CHECK(!scene.IsRunning() && !scene.GetResult(2).Valid && !scene.GetAmbience().Valid);
		CHECK(scene.GetStats().SourceEmitterCount == 0 && scene.GetStats().RaytracingTimeMs == 0);
		scene.GetVisualisation(rays);
		CHECK(rays.Bounces.empty());
		scene.Start();
		scene.CreateEmitter(2);
		CHECK(!scene.GetResult(2).Valid);
		Tick(scene);
		scene.Stop();
	}

	void GeometryAndReverb()
	{
		RaytracedAudioScene scene(nullptr);
		scene.Start();
		const auto room = Room();
		const glm::vec3 center{ 1200, 3, -2 };
		const auto transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1), center),
			0.7f, glm::vec3(0, 1, 0)), glm::vec3(-2, 3, 1));
		CHECK(scene.SetGeometry(100, room, transform, true));
		glm::mat4 actual;
		CHECK(scene.GetGeometryTransform(100, actual) && actual == transform);
		CHECK(scene.GetStats().DynamicPrimitiveCount == 1 && scene.GetStats().DynamicTriangleCount == 24);
		CHECK(scene.GetStats().WorldMin.x + scene.GetStats().WorldSize.x > center.x);
		scene.SetListener(center, { 0, 0, -1 });
		scene.CreateEmitter(7);
		scene.SetEmitterPosition(7, center + glm::vec3(0.5f, 0, 0));
		scene.SetVisualisationEnabled(true, 32, 2, 1);
		Tick(scene);
		CHECK(scene.GetResult(7).Valid);
		const auto ambience = scene.GetAmbience();
		CHECK(ambience.Valid && ambience.Reverb.Valid);
		CHECK(ambience.ReturnedPercent > 0 && std::isfinite(ambience.MaterialAbsorptionLF));
		RaytracedAudioVisualisation rays;
		scene.GetVisualisation(rays);
		CHECK(rays.RayCount == 32 && rays.BounceCount == 2);
		Near(rays.Origin.x, center.x);
		const auto inverse = glm::inverse(transform);
		size_t hits = 0;
		for (int ray = 0; ray < rays.RayCount; ++ray)
		{
			const auto& bounce = rays.Bounces[static_cast<size_t>(ray * rays.BounceCount)];
			if (!bounce.Hit)
				continue;
			++hits;
			const auto local = glm::vec3(inverse * glm::vec4(bounce.Position, 1));
			Near(std::max({ std::abs(local.x), std::abs(local.y), std::abs(local.z) }), 1.0f, 0.002f);
			CHECK(std::isfinite(bounce.Normal.x) && std::isfinite(bounce.Normal.y) && std::isfinite(bounce.Normal.z));
		}
		CHECK(hits == static_cast<size_t>(rays.RayCount));

		// Failed replacements and overflowing transforms preserve the working node.
		auto broken = room;
		broken.Triangles[0].x = std::numeric_limits<float>::quiet_NaN();
		CHECK(!scene.SetGeometry(100, broken, glm::mat4(1), false));
		CHECK(scene.GetGeometryTransform(100, actual) && actual == transform);
		auto invalid = glm::mat4(1);
		invalid[0][0] = invalid[3][0] = std::numeric_limits<float>::max();
		CHECK(!scene.UpdateGeometry(100, invalid, AcousticMaterial::Glass, false));
		CHECK(scene.GetGeometryTransform(100, actual) && actual == transform);
		CHECK(scene.GetStats().DynamicTriangleCount == 24);
		const auto underflow = glm::scale(glm::mat4(1), glm::vec3(1.0e-16f));
		CHECK(!scene.SetGeometry(100, room, underflow, false));
		CHECK(!scene.UpdateGeometry(100, underflow, AcousticMaterial::Glass, false));
		CHECK(scene.GetGeometryTransform(100, actual) && actual == transform);
		const auto inverseOverflow = glm::scale(glm::mat4(1), glm::vec3(1.0e-13f));
		CHECK(!scene.UpdateGeometry(100, inverseOverflow, AcousticMaterial::Glass, false));
		CHECK(scene.GetGeometryTransform(100, actual) && actual == transform);
		Tick(scene);
		CHECK(scene.GetResult(7).Valid);
		CHECK(scene.UpdateGeometry(100, glm::mat4(1), AcousticMaterial::Glass, false));
		CHECK(scene.GetStats().DynamicPrimitiveCount == 0 && scene.GetStats().StaticTriangleCount == 24);
		scene.SetListener({ 0, 0, 0 }, { 0, 0, -1 });
		Tick(scene);
		Near(scene.GetAmbience().MaterialAbsorptionLF, GetDefaultAcousticMaterialProperties(AcousticMaterial::Glass).AbsorptionLF, 0.002f);
		CHECK(scene.SetStaticGeometry({ room }));
		CHECK(scene.GetStats().StaticTriangleCount == 48);
		CHECK(!scene.SetStaticGeometry({ broken }));
		CHECK(scene.GetStats().StaticTriangleCount == 48);
		scene.OnUpdate(0.016f);
		CHECK(scene.RemoveGeometry(100));
		CHECK(scene.RemoveGeometry(100));
		CHECK(scene.GetStats().StaticTriangleCount == 24);
		CHECK(scene.SetStaticGeometry({}));
		CHECK(scene.GetStats().StaticPrimitiveCount == 0);
		Tick(scene);
		Near(scene.GetAmbience().ReturnedPercent, 0);
		scene.Stop();
	}
}

int main()
{
	Lux::Log::EnabledTags()[""] = {};
	try
	{
		Materials();
		SceneLifecycle();
		GeometryAndReverb();
		std::cout << "PASS: Asterwave scene lifecycle, native results, materials/YAML, transformed geometry, reverb and visualisation\n";
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAIL: unexpected exception: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
