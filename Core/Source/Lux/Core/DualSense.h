// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Input.h"

namespace Lux::DualSense {

	// DualSense output (adaptive triggers, and rumble on Windows) through the USB output report,
	// since GLFW is input-only. USB only (see HID::DeviceGroup). GLFW cannot say which HID device
	// backs which joystick slot, so output goes to every connected DualSense. On Linux, rumble uses
	// evdev force feedback instead (GamepadRumble.h) and this report carries triggers only.
	// Main thread only; driven by Input::Update.

	void SetTriggerEffect(GamepadTrigger trigger, const TriggerEffect& effect);
	void ResetTriggerEffects();
	// Motor strengths 0..1: low = large (low-frequency) motor, high = small (high-frequency) motor.
	void SetRumble(float low, float high);

	// connectedCount = DualSenses GLFW currently sees; a change triggers re-enumeration.
	void Update(uint32_t connectedCount);
	// Clears triggers and rumble on the hardware and closes devices. Call before exit, or a
	// trigger stays stiff after the application quits.
	void Shutdown();

}
