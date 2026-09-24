// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Core/GamepadRumble.h"

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <filesystem>

namespace Lux::PlatformRumble {

	namespace {

		// An evdev node that supports FF_RUMBLE. GLFW names joysticks with EVIOCGNAME too, so
		// controllers are matched by name (identical models all receive the effect).
		struct RumbleDevice
		{
			std::string Name;
			int FD = -1;
			int16_t EffectID = -1;
			bool Playing = false;
		};

		std::vector<RumbleDevice> s_Devices;
		bool s_Scanned = false;

		bool TestBit(const unsigned long* bits, int bit)
		{
			constexpr int kBitsPerLong = (int)(sizeof(unsigned long) * 8);
			return (bits[bit / kBitsPerLong] >> (bit % kBitsPerLong)) & 1;
		}

		void Stop(RumbleDevice& device)
		{
			if (!device.Playing)
				return;

			input_event stop{};
			stop.type = EV_FF;
			stop.code = (uint16_t)device.EffectID;
			stop.value = 0;
			(void)::write(device.FD, &stop, sizeof(stop));
			device.Playing = false;
		}

		void CloseAll()
		{
			for (RumbleDevice& device : s_Devices)
			{
				Stop(device);
				if (device.EffectID >= 0)
					::ioctl(device.FD, EVIOCRMFF, device.EffectID);
				::close(device.FD);
			}
			s_Devices.clear();
			s_Scanned = false;
		}

		void Scan()
		{
			CloseAll();
			s_Scanned = true;

			std::error_code error;
			for (const auto& entry : std::filesystem::directory_iterator("/dev/input", error))
			{
				const std::string filename = entry.path().filename().string();
				if (!filename.starts_with("event"))
					continue;

				// Joystick event nodes are normally granted to the logged-in user (uaccess);
				// keyboards and the like simply fail to open and are skipped.
				const int fd = ::open(entry.path().c_str(), O_RDWR | O_CLOEXEC | O_NONBLOCK);
				if (fd < 0)
					continue;

				unsigned long ffBits[FF_MAX / (sizeof(unsigned long) * 8) + 1]{};
				if (::ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ffBits)), ffBits) < 0 || !TestBit(ffBits, FF_RUMBLE))
				{
					::close(fd);
					continue;
				}

				char name[256]{};
				::ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);

				RumbleDevice& device = s_Devices.emplace_back();
				device.Name = name;
				device.FD = fd;
			}
		}

		void EnsureScanned()
		{
			if (!s_Scanned)
				Scan();
		}

		void Play(RumbleDevice& device, float low, float high)
		{
			ff_effect effect{};
			effect.type = FF_RUMBLE;
			effect.id = device.EffectID; // -1 uploads a new effect; otherwise updates it in place.
			effect.u.rumble.strong_magnitude = (uint16_t)std::lround(std::clamp(low, 0.0f, 1.0f) * 65535.0f);
			effect.u.rumble.weak_magnitude = (uint16_t)std::lround(std::clamp(high, 0.0f, 1.0f) * 65535.0f);
			effect.replay.length = 0; // Until stopped; Input handles durations.

			if (::ioctl(device.FD, EVIOCSFF, &effect) < 0)
				return;

			device.EffectID = effect.id;

			input_event play{};
			play.type = EV_FF;
			play.code = (uint16_t)effect.id;
			play.value = 1;
			device.Playing = ::write(device.FD, &play, sizeof(play)) == (ssize_t)sizeof(play);
		}

	}

	void OnControllersChanged()
	{
		CloseAll();
	}

	bool Supports(const Controller& controller)
	{
		EnsureScanned();
		return std::any_of(s_Devices.begin(), s_Devices.end(), [&controller](const RumbleDevice& device) { return device.Name == controller.Name; });
	}

	void Set(const Controller& controller, float low, float high)
	{
		EnsureScanned();
		for (RumbleDevice& device : s_Devices)
		{
			if (device.Name != controller.Name)
				continue;

			if (low <= 0.0f && high <= 0.0f)
				Stop(device);
			else
				Play(device, low, high);
		}
	}

	void Shutdown()
	{
		CloseAll();
	}

}
