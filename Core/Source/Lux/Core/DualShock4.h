// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <cstdint>

namespace Lux::DualShock4 {

	// DualShock 4 output through the USB output report: lightbar everywhere, and rumble on Windows
	// (on Linux the kernel driver's evdev force feedback owns rumble, see GamepadRumble.h). USB
	// only, and output goes to every connected DualShock 4, for the same reasons as DualSense.h.
	// Main thread only.

	// Motor strengths 0..1: low = large (low-frequency) motor, high = small (high-frequency) motor.
	void SetRumble(float low, float high);
	// Lightbar colour, 0..1 RGB; ResetLights restores the default blue (only sent if changed).
	void SetLightColor(float red, float green, float blue);
	void ResetLights();

	// connectedCount = DualShock 4s GLFW currently sees; a change triggers re-enumeration.
	void Update(uint32_t connectedCount);
	void Shutdown();

}
