// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include <soundwave/AcousticPath.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			std::exit(EXIT_FAILURE);
		}
	}

	void SameBits(float actual, float expected, const char* message)
	{
		Check(std::bit_cast<std::uint32_t>(actual) == std::bit_cast<std::uint32_t>(expected), message);
	}
}

int main()
{
	using namespace Lux::Soundwave;

	// Frozen scalar expressions from the pre-Soundwave engine, independent of the library helpers.
	// Exercise material-specific thresholds and the full authored Strength range.
	for (std::size_t tag = 0; tag < kMaterialTagCount; ++tag)
	{
		const auto& material = GetDefaultMaterialProperties(static_cast<MaterialTag>(tag));
		Check(IsValidMaterialProperties(material), "invalid authored material row");
		for (float transmission : { material.TransmissionLF, material.TransmissionHF, 0.00001f })
		{
			for (float strength : { 0.0f, 0.125f, 0.5f, 1.0f, 2.75f, 4.0f })
			{
				for (float thickness : { 0.0f, 0.001f, 0.1f, 0.3f, 1.0f, 100.0f })
				{
					const float lossPerMetre = strength * 30.0f;
					const float expected = lossPerMetre * thickness / std::max(transmission, 0.001f);
					SameBits(SolidLossDb(thickness, transmission, strength), expected, "solid loss changed bits");
				}
			}
		}
		for (float fraction : { material.FlatTransmissionLF, material.FlatTransmissionHF, 0.0f, 0.999999f, 1.0f, 2.0f })
		{
			const float expected = -10.0f * std::log10(std::max(1.0f - fraction, 1e-6f));
			SameBits(FlatLossDb(fraction), expected, "one-sided loss changed bits");
		}
	}
	for (float loss : { 0.0f, 0.001f, 1.0f, 6.0f, 30.0f, 60.0f })
		SameBits(GainFromLossDb(loss), std::pow(10.0f, -loss / 20.0f), "linear gain changed bits");

	Path path;
	path.Reset({ 1.0f, 2.0f, 3.0f }, { 5.0f, 6.0f, 7.0f }, 6.928203f);
	Check(path.GainLF() == 1.0f && path.GainHF() == 1.0f, "empty path attenuates sound");
	// Deliberately unsorted equal-distance crossings expose any extra sort or changed association.
	path.AddCrossing({ 1, 3.0f, 3.5f, 32.0f, 0.25f, false, false });
	path.AddCrossing({ 2, 1.0f, 1.0f, 0.000001f, 0.5f, true, false });
	path.AddCrossing({ 3, 1.0f, 1.5f, 0.000001f, 0.75f, false, true });
	path.Finalize();
	SameBits(path.LossLF, 32.0f, "fixed-order reduction changed bits");
	SameBits(path.LossHF, 1.5f, "HF reduction incorrect");
	Check(path.Crossings[0].Material == 1 && path.Crossings[1].OneSided && path.Crossings[2].Portal,
		"path reordered crossings or discarded flags");
	SameBits(path.Distance, 6.928203f, "caller distance was recomputed");
	SameBits(path.From.Y, 2.0f, "listener position lost");
	SameBits(path.To.Z, 7.0f, "source position lost");
	path.Finalize();
	SameBits(path.LossHF, 1.5f, "finalize accumulated the previous total");
	path.AddCrossing({ 0, 9.0f, 10.0f, 100.0f, 100.0f, false, false });
	path.Finalize();
	Check(path.LossLF == 60.0f && path.LossHF == 60.0f, "loss cap missing");
	SameBits(path.GainLF(), GainFromLossDb(60.0f), "LF cap not reflected in gain");
	const auto capacity = path.Crossings.capacity();
	path.Bounces = 4;
	path.Reset({}, {}, 0.0f);
	Check(path.Crossings.empty() && path.Crossings.capacity() == capacity, "reset failed to reuse storage");
	Check(path.LossLF == 0.0f && path.LossHF == 0.0f && path.Bounces == 0 && path.Distance == 0.0f,
		"reset retained the previous path");

	const auto& defaultMaterial = GetDefaultMaterialProperties(0);
	Check(&GetDefaultMaterialProperties(255) == &defaultMaterial, "invalid tag did not use Default");
	using Property = float MaterialProperties::*;
	for (Property field : { &MaterialProperties::AbsorptionLF, &MaterialProperties::AbsorptionHF,
		&MaterialProperties::Scattering, &MaterialProperties::TransmissionLF, &MaterialProperties::TransmissionHF,
		&MaterialProperties::FlatTransmissionLF, &MaterialProperties::FlatTransmissionHF })
	{
		for (float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.0f })
		{
			auto candidate = defaultMaterial;
			candidate.*field = bad;
			Check(!IsValidMaterialProperties(candidate), "invalid coefficient accepted");
		}
	}
	for (Property field : { &MaterialProperties::AbsorptionLF, &MaterialProperties::AbsorptionHF,
		&MaterialProperties::Scattering })
	{
		auto candidate = defaultMaterial;
		candidate.*field = 1.01f;
		Check(!IsValidMaterialProperties(candidate), "out-of-range fraction accepted");
	}
	for (Property field : { &MaterialProperties::TransmissionLF, &MaterialProperties::TransmissionHF })
	{
		auto candidate = defaultMaterial;
		candidate.*field = 0.0f;
		Check(!IsValidMaterialProperties(candidate), "zero transmission distance accepted");
	}

	std::cout << "PASS: Soundwave scalar compatibility, ordered paths, caps, reset/reuse, 23 material rows and validation\n";
}
