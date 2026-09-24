// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Lux::HID {

	// Minimal raw HID access for controller features GLFW cannot reach (e.g. DualSense adaptive
	// triggers). Implemented per platform in Core/Platform/<OS>/<OS>HID.cpp with system APIs only
	// (Windows SetupAPI/HID, Linux hidraw). Main thread only.

	struct DeviceInfo
	{
		std::string Path;
		uint16_t VendorID = 0;
		uint16_t ProductID = 0;
		bool Bluetooth = false;
		size_t OutputReportSize = 0; // Largest output report incl. report ID; 0 if unknown.
	};

	using DeviceHandle = intptr_t;
	constexpr DeviceHandle InvalidDevice = -1;

	std::vector<DeviceInfo> Enumerate(uint16_t vendorID);

	DeviceHandle Open(const std::string& path);
	// Writes one output report; data[0] is the report ID. Returns false if the device is gone.
	bool Write(DeviceHandle device, const uint8_t* data, size_t size);
	void Close(DeviceHandle device);

	// Every USB-connected device of one controller model, opened for output reports (HID.cpp).
	// Bluetooth devices are skipped with a one-time warning: on PlayStation pads, any Bluetooth
	// output report switches the pad to its enhanced input mode, which DirectInput (and so GLFW)
	// cannot read until it reconnects.
	class DeviceGroup
	{
	public:
		DeviceGroup(std::string name, uint16_t vendorID, std::vector<uint16_t> productIDs, size_t usbReportSize);
		~DeviceGroup() { Close(); }

		// Closes the devices; the next Write re-enumerates. Call when the connected set changes.
		void Close();
		// Sends one report to every device; unplugged devices are dropped.
		void Write(const uint8_t* report, size_t size);
	private:
		void Enumerate();
	private:
		std::string m_Name;
		uint16_t m_VendorID;
		std::vector<uint16_t> m_ProductIDs;
		size_t m_USBReportSize;

		std::vector<DeviceHandle> m_Devices;
		bool m_Enumerated = false;
		bool m_WarnedBluetooth = false;
		bool m_WarnedOpen = false;
	};

}
