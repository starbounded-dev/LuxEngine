// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Core/HID.h"

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

namespace Lux::HID {

	namespace {

		constexpr uint32_t kBusUSB = 0x03;
		constexpr uint32_t kBusBluetooth = 0x05;

	}

	std::vector<DeviceInfo> Enumerate(uint16_t vendorID)
	{
		std::vector<DeviceInfo> devices;

		std::error_code error;
		for (const auto& entry : std::filesystem::directory_iterator("/sys/class/hidraw", error))
		{
			// device/uevent carries "HID_ID=<bus>:<vendor>:<product>" in hex, e.g. 0003:0000054C:00000CE6.
			std::ifstream uevent(entry.path() / "device" / "uevent");
			std::string line;
			while (std::getline(uevent, line))
			{
				uint32_t bus = 0, vendor = 0, product = 0;
				if (std::sscanf(line.c_str(), "HID_ID=%x:%x:%x", &bus, &vendor, &product) != 3)
					continue;

				if (vendor == vendorID && (bus == kBusUSB || bus == kBusBluetooth))
				{
					DeviceInfo& info = devices.emplace_back();
					info.Path = "/dev/" + entry.path().filename().string();
					info.VendorID = (uint16_t)vendor;
					info.ProductID = (uint16_t)product;
					info.Bluetooth = bus == kBusBluetooth;
				}
				break;
			}
		}

		return devices;
	}

	DeviceHandle Open(const std::string& path)
	{
		// Needs write access to /dev/hidrawN, which distributions usually grant only through a
		// udev rule (e.g. the one Steam installs for PlayStation controllers).
		const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
		return fd < 0 ? InvalidDevice : (DeviceHandle)fd;
	}

	bool Write(DeviceHandle device, const uint8_t* data, size_t size)
	{
		if (device == InvalidDevice)
			return false;

		return ::write((int)device, data, size) == (ssize_t)size;
	}

	void Close(DeviceHandle device)
	{
		if (device != InvalidDevice)
			::close((int)device);
	}

}
