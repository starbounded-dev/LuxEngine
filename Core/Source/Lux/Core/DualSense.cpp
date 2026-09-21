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
		constexpr size_t kRumbleHigh = 2; // Right (small) motor.
		constexpr size_t kRumbleLow = 3;  // Left (large) motor.
		constexpr size_t kRightTriggerEffect = 10;
		constexpr size_t kLeftTriggerEffect = 21;
		constexpr size_t kTriggerEffectSize = 11;

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
		uint32_t s_ConnectedCount = 0;
		bool s_Dirty = false;

		bool AnyOutputActive()
		{
			return s_Effects[0].Type != TriggerEffectType::Off || s_Effects[1].Type != TriggerEffectType::Off
				|| s_RumbleLow != 0 || s_RumbleHigh != 0;
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

	void Update(uint32_t connectedCount)
	{
		if (connectedCount != s_ConnectedCount)
		{
			s_ConnectedCount = connectedCount;
			s_Devices.Close();
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
			Flush();
		}

		s_Devices.Close();
		s_Dirty = false;
	}

}
