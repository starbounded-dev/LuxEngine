// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "DualShock4.h"

#include "Lux/Core/HID.h"

namespace Lux::DualShock4 {

	namespace {

		// USB output report 0x05 (32 bytes): report ID, valid flags, reserved, then motors and
		// lightbar. Only the motor flag is set, so the lightbar keeps whatever the system chose.
		constexpr uint8_t kUSBReportID = 0x05;
		constexpr size_t kUSBReportSize = 32;
		constexpr size_t kValidFlags0 = 1;
		constexpr uint8_t kValidMotor = 0x01;
		constexpr size_t kRumbleHigh = 4; // Right (small) motor.
		constexpr size_t kRumbleLow = 5;  // Left (large) motor.

		HID::DeviceGroup s_Devices("DualShock 4", 0x054C,
			{ 0x05C4 /* v1 */, 0x09CC /* v2 */, 0x0BA0 /* USB wireless adapter */ }, kUSBReportSize);
		uint8_t s_RumbleLow = 0;
		uint8_t s_RumbleHigh = 0;
		uint32_t s_ConnectedCount = 0;
		bool s_Dirty = false;

		void Flush()
		{
			std::array<uint8_t, kUSBReportSize> report{};
			report[0] = kUSBReportID;
			report[kValidFlags0] = kValidMotor;
			report[kRumbleLow] = s_RumbleLow;
			report[kRumbleHigh] = s_RumbleHigh;
			s_Devices.Write(report.data(), report.size());
		}

		uint8_t ToMotor(float strength)
		{
			return (uint8_t)std::lround(std::clamp(strength, 0.0f, 1.0f) * 255.0f);
		}

	}

	void SetRumble(float low, float high)
	{
		const uint8_t lowMotor = ToMotor(low);
		const uint8_t highMotor = ToMotor(high);
		if (lowMotor == s_RumbleLow && highMotor == s_RumbleHigh)
			return;

		s_RumbleLow = lowMotor;
		s_RumbleHigh = highMotor;
		s_Dirty = true;
	}

	void Update(uint32_t connectedCount)
	{
		if (connectedCount != s_ConnectedCount)
		{
			s_ConnectedCount = connectedCount;
			s_Devices.Close();
			s_Dirty |= s_RumbleLow != 0 || s_RumbleHigh != 0;
		}

		if (!s_Dirty || connectedCount == 0)
			return;

		s_Dirty = false;
		Flush();
	}

	void Shutdown()
	{
		if ((s_RumbleLow != 0 || s_RumbleHigh != 0) && s_ConnectedCount > 0)
		{
			s_RumbleLow = s_RumbleHigh = 0;
			Flush();
		}

		s_Devices.Close();
		s_Dirty = false;
	}

}
