// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// The build without --raytraced-audio: the stub backend is inert, and the engine-owned material
// defaults derive from their published inputs. Needs neither FMOD nor Asterwave.

#include "AudioTestLog.h"
#include "Lux/Audio/AcousticMaterial.h"
#include "Lux/Audio/RaytracedAudioScene.h"

#include <cmath>

namespace
{
	float MassLawDb(float density, float thickness, float hz)
	{
		return 20.0f * std::log10(density * thickness * hz) - 47.0f;
	}

	void StubBackendIsInert()
	{
		assert(!RaytracedAudioScene::IsAvailable());
		RaytracedAudioScene scene(nullptr);
		scene.Start();
		assert(!scene.IsRunning());
		scene.WaitForResults();
		scene.SetListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
		scene.CreateEmitter(7);
		scene.SetEmitterPosition(7, glm::vec3(1.0f));
		scene.OnUpdate(0.016f);
		assert(!scene.GetResult(7).Valid && !scene.GetAmbience().Valid);
		AcousticGeometry geometry;
		geometry.Triangles = { glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f) };
		glm::mat4 transform(1.0f);
		assert(!scene.SetGeometry(1, geometry, transform, false) && !scene.GetGeometryTransform(1, transform));
		scene.SetVisualisationEnabled(true, 8, 2, 100);
		RaytracedAudioVisualisation visualisation;
		scene.GetVisualisation(visualisation);
		assert(visualisation.Bounces.empty());
		const RaytracedAudioStats stats = scene.GetStats();
		assert(!stats.Available && !stats.Running && scene.GetRaytracingTimeMilliseconds() == 0.0);
		scene.DestroyEmitter(7);
		scene.Stop();
	}

	void DefaultsComeFromThePublishedTable()
	{
		for (size_t i = 0; i < AcousticMaterialCount; ++i)
		{
			const auto properties = GetDefaultAcousticMaterialProperties(static_cast<AcousticMaterial>(i));
			assert(properties.IsValid());
			// Transmission loss rises with frequency for every row, so HF loses more per metre.
			assert(properties.TransmissionHF <= properties.TransmissionLF);
			// Heavy walls lose all but ~1e-8 of the energy; float may round that to a complete 1.
			assert(properties.FlatTransmissionLF <= 1.0f && properties.FlatTransmissionHF <= 1.0f);
			assert(properties.FlatTransmissionLF <= properties.FlatTransmissionHF);
		}

		const auto defaults = GetDefaultAcousticMaterialProperties(AcousticMaterial::Default);
		const auto concrete = GetDefaultAcousticMaterialProperties(AcousticMaterial::Concrete);
		Near(defaults.AbsorptionLF, concrete.AbsorptionLF);
		Near(defaults.TransmissionLF, concrete.TransmissionLF);

		// Unglazed brick: 0.03 at 125 and 250 Hz, 0.05 and 0.07 at 2 and 4 kHz.
		const auto brick = GetDefaultAcousticMaterialProperties(AcousticMaterial::Brick);
		Near(brick.AbsorptionLF, 0.03f);
		Near(brick.AbsorptionHF, 0.06f);
		// 300 mm of brick at 1800 kg/m^3 under the mass law, and still under the engine's 60 dB cap.
		Near(brick.TransmissionLF, 30.0f * 0.3f / MassLawDb(1800.0f, 0.3f, 125.0f));
		assert(30.0f * 0.3f / brick.TransmissionLF < 60.0f);

		// 6 mm window glass: about 18.5 dB at 125 Hz, the usual measured figure.
		const auto glass = GetDefaultAcousticMaterialProperties(AcousticMaterial::Glass);
		const float glassLoss = MassLawDb(2500.0f, 0.006f, 125.0f);
		assert(glassLoss > 18.0f && glassLoss < 19.0f);
		Near(glass.TransmissionLF, 30.0f * 0.006f / glassLoss);
		Near(glass.FlatTransmissionLF, 1.0f - std::pow(10.0f, -glassLoss / 10.0f));

		// A curtain is below the mass law's range at 125 Hz; the minimum 1 dB loss applies.
		Near(GetDefaultAcousticMaterialProperties(AcousticMaterial::Cloth).TransmissionLF, 30.0f * 0.001f / 1.0f);

		// Foliage uses ISO 9613-2 per-metre attenuation: 0.03 dB/m at 125 Hz, 0.08 dB/m at 2 kHz.
		const auto foliage = GetDefaultAcousticMaterialProperties(AcousticMaterial::Foliage);
		Near(foliage.TransmissionLF, 1000.0f);
		Near(foliage.TransmissionHF, 375.0f);
	}
}

int main()
{
	StubBackendIsInert();
	DefaultsComeFromThePublishedTable();
	std::cout << "PASS: stub backend inert; material defaults derive from the published table\n";
	return 0;
}
