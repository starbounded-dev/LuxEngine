// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Input.h"

namespace Lux::PlatformRumble {

	// Rumble through the OS gamepad API, implemented per platform in
	// Core/Platform/<OS>/<OS>GamepadRumble.cpp:
	//   Windows: XInput, for Xbox controllers (PlayStation pads go through DualSense/DualShock4 HID).
	//   Linux:   evdev force feedback (FF_RUMBLE), for every pad whose kernel driver supports it.
	// Main thread only; driven by Input::Update, which only calls Set when strengths change.

	// The set of connected controllers changed; drop cached device handles.
	void OnControllersChanged();
	bool Supports(const Controller& controller);
	// Motor strengths 0..1: low = large (low-frequency) motor, high = small (high-frequency) motor.
	void Set(const Controller& controller, float low, float high);
	// Stops every motor this backend started and releases devices.
	void Shutdown();

}
