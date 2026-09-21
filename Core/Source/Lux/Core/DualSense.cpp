// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "DualSense.h"

#include "Lux/Core/HID.h"

namespace Lux::DualSense {

	namespace {

		// USB output report 0x02: report ID followed by a 47-byte effects block.
		constexpr uint8_t kUSBReportID = 0x02;
		constexpr size_t kUSBReportSize = 48;

		// Offsets inside the effects block (report byte 1 onward).
		constexpr size_t kEnableBits1 = 0;
		constexpr uint8_t kEnableRumbleEmulation = 0x01;
		constexpr uint8_t kDisableAudioHaptics = 0x02;
		constexpr uint8_t kEnableRightTriggerEffect = 0x04;
		constexpr uint8_t kEnableLeftTriggerEffect = 0x08;
		constexpr size_t kEnableBits2 = 1;
		constexpr uint8_t kEnableLightbar = 0x04;
		constexpr uint8_t kEnablePlayerLights = 0x10;
		constexpr size_t kRumbleHigh = 2; // Right (small) motor.
		constexpr size_t kRumbleLow = 3;  // Left (large) motor.
		constexpr size_t kRightTriggerEffect = 10;
		constexpr size_t kLeftTriggerEffect = 21;
		constexpr size_t kTriggerEffectSize = 11;
		constexpr size_t kEnableBits3 = 38;
		constexpr uint8_t kEnableLightbarSetup = 0x02;
		constexpr size_t kLightbarSetup = 41;
		constexpr uint8_t kLightbarSetupLightOut = 0x02; // Ends the firmware's own lightbar animation.
		constexpr size_t kPlayerLights = 43;
		constexpr size_t kLightbarRed = 44;

		// Player LED patterns across the 5 LEDs, as the console shows players 1-4.
		constexpr std::array<uint8_t, 5> kPlayerLightPatterns = { 0x00, 0x04, 0x0A, 0x15, 0x1B };
		constexpr std::array<uint8_t, 3> kDefaultLightColor = { 0x00, 0x00, 0x40 }; // Dim blue.

		// Trigger effect modes (first byte of an 11-byte trigger block).
		constexpr uint8_t kModeOff = 0x05;
		constexpr uint8_t kModeFeedback = 0x21;
		constexpr uint8_t kModeWeapon = 0x25;
		constexpr uint8_t kModeVibration = 0x26;

#ifdef LUX_PLATFORM_WINDOWS
		constexpr bool kRumbleOverHID = true;
#else
		constexpr bool kRumbleOverHID = false; // The kernel driver's evdev force feedback owns rumble.
#endif

		HID::DeviceGroup s_Devices("DualSense", 0x054C, { 0x0CE6 /* DualSense */, 0x0DF2 /* DualSense Edge */ }, kUSBReportSize);
		std::array<TriggerEffect, 2> s_Effects{};
		uint8_t s_RumbleLow = 0;
		uint8_t s_RumbleHigh = 0;
		std::array<uint8_t, 3> s_LightColor = kDefaultLightColor;
		uint8_t s_PlayerLights = 0;
		bool s_LightsOverridden = false; // Colour or player LEDs differ from the defaults.
		bool s_LightsPending = false;    // Include the light fields in the next report.
		bool s_LightbarSetupSent = false;
		uint32_t s_ConnectedCount = 0;
		bool s_Dirty = false;

		bool AnyOutputActive()
		{
			return s_Effects[0].Type != TriggerEffectType::Off || s_Effects[1].Type != TriggerEffectType::Off
				|| s_RumbleLow != 0 || s_RumbleHigh != 0 || s_LightsOverridden;
		}

		void RestoreDefaultLights()
		{
			s_LightColor = kDefaultLightColor;
			s_PlayerLights = 0;
			s_LightsOverridden = false;
			s_LightsPending = true;
		}

		// Sets 'value' (3 bits) in zones [start, 9] and marks those zones active.
		void FillZones(uint8_t* out, int32_t start, uint8_t value)
		{
			uint16_t activeZones = 0;
			uint32_t zoneValues = 0;
			for (int32_t zone = start; zone < 10; zone++)
			{
				activeZones |= (uint16_t)(1u << zone);
				zoneValues |= (uint32_t)(value & 0x07) << (3 * zone);
			}

			out[1] = (uint8_t)(activeZones & 0xFF);
			out[2] = (uint8_t)(activeZones >> 8);
			out[3] = (uint8_t)(zoneValues & 0xFF);
			out[4] = (uint8_t)((zoneValues >> 8) & 0xFF);
			out[5] = (uint8_t)((zoneValues >> 16) & 0xFF);
			out[6] = (uint8_t)((zoneValues >> 24) & 0xFF);
		}

		void EncodeTriggerEffect(const TriggerEffect& effect, uint8_t* out)
		{
			std::fill_n(out, kTriggerEffectSize, (uint8_t)0);
			out[0] = kModeOff;

			const int32_t strength = std::clamp(effect.Strength, 0, 8);
			if (strength == 0)
				return;

			switch (effect.Type)
			{
				case TriggerEffectType::Resistance:
				{
					out[0] = kModeFeedback;
					FillZones(out, std::clamp(effect.Start, 0, 9), (uint8_t)(strength - 1));
					break;
				}
				case TriggerEffectType::Weapon:
				{
					const int32_t start = std::clamp(effect.Start, 2, 7);
					const int32_t end = std::clamp(effect.End, start + 1, 8);
					const uint16_t zones = (uint16_t)((1u << start) | (1u << end));
					out[0] = kModeWeapon;
					out[1] = (uint8_t)(zones & 0xFF);
					out[2] = (uint8_t)(zones >> 8);
					out[3] = (uint8_t)(strength - 1);
					break;
				}
				case TriggerEffectType::Vibration:
				{
					const int32_t frequency = std::clamp(effect.Frequency, 0, 255);
					if (frequency == 0)
						return;

					out[0] = kModeVibration;
					FillZones(out, std::clamp(effect.Start, 0, 9), (uint8_t)(strength - 1));
					out[9] = (uint8_t)frequency;
					break;
				}
				case TriggerEffectType::Off:
					break;
			}
		}

		void Flush()
		{
			std::array<uint8_t, kUSBReportSize> report{};
			report[0] = kUSBReportID;
			uint8_t* effects = report.data() + 1;

			effects[kEnableBits1] = kEnableRightTriggerEffect | kEnableLeftTriggerEffect;
			EncodeTriggerEffect(s_Effects[(size_t)GamepadTrigger::Right], effects + kRightTriggerEffect);
			EncodeTriggerEffect(s_Effects[(size_t)GamepadTrigger::Left], effects + kLeftTriggerEffect);

			if constexpr (kRumbleOverHID)
			{
				effects[kEnableBits1] |= kEnableRumbleEmulation | kDisableAudioHaptics;
				effects[kRumbleLow] = s_RumbleLow;
				effects[kRumbleHigh] = s_RumbleHigh;
			}

			// Lights are only sent when changed; leaving the enable bits clear keeps the current state.
			if (s_LightsPending)
			{
				if (!s_LightbarSetupSent)
				{
					effects[kEnableBits3] |= kEnableLightbarSetup;
					effects[kLightbarSetup] = kLightbarSetupLightOut;
					s_LightbarSetupSent = true;
				}

				effects[kEnableBits2] |= kEnableLightbar | kEnablePlayerLights;
				effects[kPlayerLights] = s_PlayerLights;
				std::copy(s_LightColor.begin(), s_LightColor.end(), effects + kLightbarRed);
				s_LightsPending = false;
			}

			s_Devices.Write(report.data(), report.size());
		}

		uint8_t ToMotor(float strength)
		{
			return (uint8_t)std::lround(std::clamp(strength, 0.0f, 1.0f) * 255.0f);
		}

	}

	void SetTriggerEffect(GamepadTrigger trigger, const TriggerEffect& effect)
	{
		const size_t index = trigger == GamepadTrigger::Left ? 0 : 1;
		if (s_Effects[index] == effect)
			return;

		s_Effects[index] = effect;
		s_Dirty = true;
	}

	void ResetTriggerEffects()
	{
		if (s_Effects[0].Type == TriggerEffectType::Off && s_Effects[1].Type == TriggerEffectType::Off)
			return;

		s_Effects = {};
		s_Dirty = true;
	}

	void SetRumble(float low, float high)
	{
		if constexpr (!kRumbleOverHID)
			return;

		const uint8_t lowMotor = ToMotor(low);
		const uint8_t highMotor = ToMotor(high);
		if (lowMotor == s_RumbleLow && highMotor == s_RumbleHigh)
			return;

		s_RumbleLow = lowMotor;
		s_RumbleHigh = highMotor;
		s_Dirty = true;
	}

	void SetLightColor(float red, float green, float blue)
	{
		const std::array<uint8_t, 3> color = { ToMotor(red), ToMotor(green), ToMotor(blue) };
		if (color == s_LightColor && s_LightsOverridden)
			return;

		s_LightColor = color;
		s_LightsOverridden = true;
		s_LightsPending = true;
		s_Dirty = true;
	}

	void SetPlayerLights(int player)
	{
		const uint8_t pattern = kPlayerLightPatterns[(size_t)std::clamp(player, 0, (int)kPlayerLightPatterns.size() - 1)];
		if (pattern == s_PlayerLights && s_LightsOverridden)
			return;

		s_PlayerLights = pattern;
		s_LightsOverridden = true;
		s_LightsPending = true;
		s_Dirty = true;
	}

	void ResetLights()
	{
		if (!s_LightsOverridden)
			return;

		RestoreDefaultLights();
		s_Dirty = true;
	}

	void Update(uint32_t connectedCount)
	{
		if (connectedCount != s_ConnectedCount)
		{
			s_ConnectedCount = connectedCount;
			s_Devices.Close();
			s_LightbarSetupSent = false;
			s_LightsPending |= s_LightsOverridden;
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
			s_Effects = {};
			s_RumbleLow = s_RumbleHigh = 0;
			if (s_LightsOverridden)
				RestoreDefaultLights();
			Flush();
		}

		s_Devices.Close();
		s_Dirty = false;
	}

}
