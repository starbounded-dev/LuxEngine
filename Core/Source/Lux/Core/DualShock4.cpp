// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "DualShock4.h"

#include "Lux/Core/HID.h"

namespace Lux::DualShock4 {

	namespace {

		// USB output report 0x05 (32 bytes): report ID, valid flags, reserved, then motors and
		// lightbar. Only the flags for the fields being changed are set; the pad keeps the rest.
		constexpr uint8_t kUSBReportID = 0x05;
		constexpr size_t kUSBReportSize = 32;
		constexpr size_t kValidFlags0 = 1;
		constexpr uint8_t kValidMotor = 0x01;
		constexpr uint8_t kValidLightbar = 0x02;
		constexpr size_t kRumbleHigh = 4; // Right (small) motor.
		constexpr size_t kRumbleLow = 5;  // Left (large) motor.
		constexpr size_t kLightbarRed = 6;

		constexpr std::array<uint8_t, 3> kDefaultLightColor = { 0x00, 0x00, 0x40 }; // Dim blue.

#ifdef LUX_PLATFORM_WINDOWS
		constexpr bool kRumbleOverHID = true;
#else
		constexpr bool kRumbleOverHID = false; // The kernel driver's evdev force feedback owns rumble.
#endif

		HID::DeviceGroup s_Devices("DualShock 4", 0x054C,
			{ 0x05C4 /* v1 */, 0x09CC /* v2 */, 0x0BA0 /* USB wireless adapter */ }, kUSBReportSize);
		uint8_t s_RumbleLow = 0;
		uint8_t s_RumbleHigh = 0;
		std::array<uint8_t, 3> s_LightColor = kDefaultLightColor;
		bool s_LightOverridden = false;
		bool s_LightPending = false;
		uint32_t s_ConnectedCount = 0;
		bool s_Dirty = false;

		bool AnyOutputActive()
		{
			return s_RumbleLow != 0 || s_RumbleHigh != 0 || s_LightOverridden;
		}

		void Flush()
		{
			std::array<uint8_t, kUSBReportSize> report{};
			report[0] = kUSBReportID;

			if constexpr (kRumbleOverHID)
			{
				report[kValidFlags0] |= kValidMotor;
				report[kRumbleLow] = s_RumbleLow;
				report[kRumbleHigh] = s_RumbleHigh;
			}

			if (s_LightPending)
			{
				report[kValidFlags0] |= kValidLightbar;
				std::copy(s_LightColor.begin(), s_LightColor.end(), report.begin() + kLightbarRed);
				s_LightPending = false;
			}

			if (report[kValidFlags0] != 0)
				s_Devices.Write(report.data(), report.size());
		}

		uint8_t ToByte(float value)
		{
			return (uint8_t)std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f);
		}

	}

	void SetRumble(float low, float high)
	{
		const uint8_t lowMotor = ToByte(low);
		const uint8_t highMotor = ToByte(high);
		if (lowMotor == s_RumbleLow && highMotor == s_RumbleHigh)
			return;

		s_RumbleLow = lowMotor;
		s_RumbleHigh = highMotor;
		s_Dirty = true;
	}

	void SetLightColor(float red, float green, float blue)
	{
		const std::array<uint8_t, 3> color = { ToByte(red), ToByte(green), ToByte(blue) };
		if (color == s_LightColor && s_LightOverridden)
			return;

		s_LightColor = color;
		s_LightOverridden = true;
		s_LightPending = true;
		s_Dirty = true;
	}

	void ResetLights()
	{
		if (!s_LightOverridden)
			return;

		s_LightColor = kDefaultLightColor;
		s_LightOverridden = false;
		s_LightPending = true;
		s_Dirty = true;
	}

	void Update(uint32_t connectedCount)
	{
		if (connectedCount != s_ConnectedCount)
		{
			s_ConnectedCount = connectedCount;
			s_Devices.Close();
			s_LightPending |= s_LightOverridden;
			// A newly connected pad starts with everything off; push any active output to it.
			s_Dirty |= AnyOutputActive();
		}

		if (!s_Dirty || connectedCount == 0)
			return;

		s_Dirty = false;
		Flush();
	}

	void Shutdown()
	{
		if (AnyOutputActive() && s_ConnectedCount > 0)
		{
			s_RumbleLow = s_RumbleHigh = 0;
			if (s_LightOverridden)
			{
				s_LightColor = kDefaultLightColor;
				s_LightOverridden = false;
				s_LightPending = true;
			}
			Flush();
		}

		s_Devices.Close();
		s_Dirty = false;
	}

}
