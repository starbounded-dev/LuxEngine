// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <cstdint>

namespace Lux::DualShock4 {

	// DualShock 4 rumble through the USB output report (Windows; on Linux the kernel driver's evdev
	// force feedback is used instead, see GamepadRumble.h). USB only, and output goes to every
	// connected DualShock 4, for the same reasons as DualSense.h. Main thread only.

	// Motor strengths 0..1: low = large (low-frequency) motor, high = small (high-frequency) motor.
	void SetRumble(float low, float high);

	// connectedCount = DualShock 4s GLFW currently sees; a change triggers re-enumeration.
	void Update(uint32_t connectedCount);
	void Shutdown();

}
