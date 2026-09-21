// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Input.h"

namespace Lux::DualSense {

	// DualSense adaptive triggers through the USB output report, since GLFW is input-only. USB only
	// (see HID::DeviceGroup). GLFW cannot say which HID device backs which joystick slot, so
	// effects go to every connected DualSense. Main thread only; driven by Input::Update.

	void SetTriggerEffect(GamepadTrigger trigger, const TriggerEffect& effect);
	void ResetTriggerEffects();

	// connectedCount = DualSenses GLFW currently sees; a change triggers re-enumeration.
	void Update(uint32_t connectedCount);
	// Clears trigger effects on the hardware and closes devices. Call before exit, or a trigger
	// stays stiff after the application quits.
	void Shutdown();

}
