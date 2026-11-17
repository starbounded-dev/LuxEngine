// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include <soundwave/Echogram.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace Lux::Soundwave;

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
	bool Near(float a, float b) { return std::abs(a - b) < 1e-5f; }
}

int main()
{
	SolveResult solve;
	solve.Direct.Reset({}, {}, 34.3f); // 100 ms direct arrival
	solve.Direct.Finalize();
	PropagationPath reflection;
	reflection.Kind = PathKind::Reflection;
	reflection.Distance = 37.8f; // just over 10 ms after direct
	reflection.GainLF = 0.5f;
	reflection.GainHF = 0.25f;
	solve.Indirect.push_back(reflection);
	EchogramSettings settings;
	settings.WindowSeconds = 2.56f; // 10 ms bins
	Echogram output;
	Check(BuildEchogram(solve, settings, output) == EvaluationError::None, "valid paths rejected");
	Check(Near(output.DirectArrivalSeconds, 0.1f) && Near(output.BinSeconds, 0.01f), "time shift wrong");
	Check(Near(output.Bins[0].EnergyLF, 1.0f) && Near(output.Bins[1].EnergyLF, 0.25f),
		"direct and reflected LF energy landed in wrong bins");
	Check(Near(output.Bins[1].EnergyHF, 0.0625f) && Near(output.TotalEnergyHF, 1.0625f),
		"HF energy was not squared or totalled");
	Check(output.PathCount == 2 && !output.WindowLimited, "path accounting wrong");

	settings.SmoothingSeconds = 0.02f;
	Check(BuildEchogram(solve, settings, output) == EvaluationError::None, "smoothing rejected");
	float reflectedLF = 0.0f;
	for (const auto& bin : output.Bins)
		reflectedLF += bin.EnergyLF;
	Check(Near(reflectedLF, 1.25f) && Near(output.TotalEnergyLF, 1.25f),
		"smoothing changed total energy");

	const Echogram preserved = output;
	solve.Indirect[0].Distance = std::numeric_limits<float>::quiet_NaN();
	Check(BuildEchogram(solve, settings, output) == EvaluationError::NumericalFailure &&
		Near(output.TotalEnergyLF, preserved.TotalEnergyLF) && output.PathCount == preserved.PathCount,
		"invalid path replaced the last valid histogram");
	solve.Indirect[0].Distance = 1000.0f;
	Check(BuildEchogram(solve, settings, output) == EvaluationError::None && output.WindowLimited &&
		output.PathCount == 2 && Near(output.TotalEnergyLF, 1.0f), "late path was folded into the window");
	std::cout << "Soundwave echogram checks passed\n";
}
