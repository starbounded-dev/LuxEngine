// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>

namespace Lux {

	enum class LuxOpticLayout : uint32_t
	{
		SideBySide = 0,   // left eye on the left half of the display
		TopBottom = 1     // left eye on the top half
	};

	// Measurements and calibration of the LUX OPTIC headset (docs/VR_PLAN.md). They describe the
	// hardware, not a game, so there is one file per machine in the user data folder, shared by the
	// editor and every exported game. Every value is configurable because the display board and
	// lenses had not been measured when this was written. Main thread.
	struct LuxOpticConfig
	{
		float IPD = 0.062f;                           // metres
		float FOV = 90.0f;                            // degrees per eye; horizontal = vertical (square panels)
		glm::uvec2 EyeResolution = { 1440, 1440 };
		float RenderScale = 1.0f;                     // eye render size = round(EyeResolution * RenderScale)

		glm::uvec2 DisplayMatchResolution = { 2880, 1440 };
		int32_t DisplayIndex = -1;                    // -1: the monitor whose mode is DisplayMatchResolution
		uint32_t DisplayRefreshRate = 120;

		LuxOpticLayout Layout = LuxOpticLayout::SideBySide;
		uint32_t EyeRotation = 0;                     // degrees: 0, 90, 180 or 270 (panels mounted rotated)
		int32_t EyeGapOffset = 0;                     // pixels; positive moves each eye's image outward

		float DistortionK1 = 0.22f;
		float DistortionK2 = 0.24f;
		float DistortionZoom = 1.0f;

		bool ChromaticEnabled = false;
		float ChromaticRedScale = 0.996f;
		float ChromaticBlueScale = 1.014f;

		float Near = 0.05f;                           // metres
		float Far = 1000.0f;

		// <user data folder>/LuxOptic.yaml.
		static std::filesystem::path GetDefaultPath();

		// Reads `path` over the current values: a missing key keeps its value, and an unreadable or
		// out-of-range value is kept or clamped, and logged. False with the values unchanged when
		// the file does not exist (not logged: the caller decides whether that is expected) or is
		// malformed (logged).
		bool Load(const std::filesystem::path& path);
		// False, and logged, when the file cannot be written.
		bool Save(const std::filesystem::path& path) const;
	};

}
