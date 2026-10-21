// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Thread.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace Lux {

	namespace HID { class InputDevice; }

	// Reads the LUX OPTIC head tracker (ESP32-S3 + IMU, USB HID, ~1000 reports per second) on a
	// thread of its own and reconnects by itself, so the pose it hands out is always the newest one.
	// A port of vr/LuxTracker.h onto Lux::HID, so no hidapi. Input report 1 carries the head rotation
	// relative to the world as four little-endian floats w, x, y, z, in the engine's own axes (+X
	// right, +Y up, -Z forward, right-handed): no conversion is needed.
	//
	// The reader thread touches only the device and the pose; the public functions are for the main
	// thread and are safe from any.
	class LuxOpticTracker
	{
	public:
		static constexpr uint16_t VendorID = 0x303A;   // Espressif's defaults, used by the tracker firmware
		static constexpr uint16_t ProductID = 0x1001;

		LuxOpticTracker();    // starts the reader thread
		~LuxOpticTracker();   // stops and joins it

		bool IsConnected() const { return m_Connected; }
		uint32_t GetReportsPerSecond() const { return m_ReportsPerSecond; }

		// The newest head rotation with the recenter applied; identity until the first report.
		glm::quat GetOrientation() const;
		// Makes the direction the head faces now forward. Yaw only, so up stays up. (The firmware's
		// own recenter button does the same at the source.)
		void Recenter();

	private:
		void Run();
		bool TryOpen(HID::InputDevice& device);
		// Waits before the next connection attempt; false when the tracker is stopping.
		bool WaitBeforeRetry();

	private:
		std::atomic<bool> m_Running = true;
		std::atomic<bool> m_Connected = false;
		std::atomic<uint32_t> m_ReportsPerSecond = 0;
		bool m_WarnedOpen = false;   // reader thread only

		mutable std::mutex m_PoseMutex;
		glm::quat m_RawOrientation = { 1.0f, 0.0f, 0.0f, 0.0f };   // w, x, y, z
		glm::quat m_Reference = { 1.0f, 0.0f, 0.0f, 0.0f };

		std::mutex m_StopMutex;
		std::condition_variable m_StopCondition;

		Thread m_Thread;
	};

}
