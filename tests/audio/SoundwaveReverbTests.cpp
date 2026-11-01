// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include <soundwave/Reverb.h>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
	using namespace Lux::Soundwave;
	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			std::exit(EXIT_FAILURE);
		}
	}
	void Box(std::vector<Triangle>& triangles, Position low, Position high, MaterialTag material, bool openEnds = false)
	{
		constexpr int indices[] = { 0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,4,6, 0,6,2,
			1,3,7, 1,7,5, 0,1,5, 0,5,4, 2,6,7, 2,7,3 };
		const auto point = [&](int i) { return Position{ i & 1 ? high.X : low.X, i & 2 ? high.Y : low.Y, i & 4 ? high.Z : low.Z }; };
		for (int i = openEnds ? 6 : 0; i < 36; i += 3) // the first two faces are the Z ends
			triangles.push_back({ point(indices[i]),point(indices[i + 1]),point(indices[i + 2]),1,0,material,false });
	}
	ReverbResult Measure(const std::vector<Triangle>& triangles, Position listener)
	{
		Grid grid;
		Check(grid.Build(triangles) == GeometryError::None, "test geometry invalid");
		std::vector<RayHit> hits;
		ReverbResult result;
		Check(EvaluateReverb(GeometryView(grid), DefaultMaterials(), Settings{}, listener, hits, result) == EvaluationError::None,
			"reverb evaluation failed");
		std::cout << "decay=" << result.DecaySeconds << " send=" << result.Send << " escaped=" << result.EscapedFraction
			<< " mfp=" << result.MeanFreePath << '\n';
		return result;
	}
	constexpr MaterialTag kCarpet = 2, kMarble = 10;
}

int main()
{
	std::vector<Triangle> geometry;
	Box(geometry, {-5,0,-5}, {5,4,5}, kMarble);
	const auto stone = Measure(geometry, {0,1.7f,0});
	const auto again = Measure(geometry, {0,1.7f,0});
	Check(stone.DecaySeconds == again.DecaySeconds && stone.Send == again.Send && stone.Traces == again.Traces,
		"reverb is not deterministic");
	Check(stone.EscapedFraction == 0, "rays escaped a closed room");
	// Sabine: T60 = 13.8 * (4V/S) / (c * mean absorption). Specular rays in a box sit a little above it.
	const float sabine = 13.815511f * (4.0f * 400.0f / 360.0f) / (343.0f * 0.09f);
	Check(stone.DecaySeconds > 0.9f * sabine && stone.DecaySeconds < 1.4f * sabine, "stone room decay far from Sabine");

	geometry.clear();
	Box(geometry, {-1,0,-10}, {1,3,10}, kMarble);
	const auto corridor = Measure(geometry, {0,1.7f,0});
	Check(corridor.DecaySeconds < 0.75f * stone.DecaySeconds && corridor.Send < stone.Send - 0.1f,
		"a corridor and a stone room of the same material sound alike");

	geometry.clear();
	Box(geometry, {-1,0,-10}, {1,3,10}, kMarble, true);
	const auto openCorridor = Measure(geometry, {0,1.7f,0});
	Check(openCorridor.EscapedFraction > 0 && openCorridor.DecaySeconds < corridor.DecaySeconds,
		"open corridor ends did not shorten the decay");

	geometry.clear();
	Box(geometry, {-5,0,-5}, {5,4,5}, kCarpet);
	const auto carpet = Measure(geometry, {0,1.7f,0});
	Check(carpet.DecaySeconds < 0.25f * stone.DecaySeconds, "absorbent room rang like stone");

	geometry.clear();
	geometry.push_back({ {-50,0,-50},{50,0,50},{50,0,-50},1,0,kMarble,false });
	geometry.push_back({ {-50,0,-50},{-50,0,50},{50,0,50},1,0,kMarble,false });
	const auto field = Measure(geometry, {0,1.7f,0});
	Check(field.EscapedFraction == 1 && field.Send < 0.05f, "open ground reverberates");

	Grid empty;
	Check(empty.Build({}) == GeometryError::None, "empty geometry invalid");
	std::vector<RayHit> hits;
	ReverbResult none;
	Check(EvaluateReverb(GeometryView(empty), DefaultMaterials(), Settings{}, {}, hits, none) == EvaluationError::None &&
		none.Send == 0 && none.DecaySeconds == 0, "empty world is not dry");
	Settings invalid;
	invalid.ReverbRays = 0;
	Check(EvaluateReverb(GeometryView(empty), DefaultMaterials(), invalid, {}, hits, none) == EvaluationError::InvalidSettings,
		"invalid reverb settings accepted");
	std::cout << "Soundwave reverb checks passed\n";
}
