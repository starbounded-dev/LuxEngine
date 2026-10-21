// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Lux::HID {

	// Minimal raw HID access for devices GLFW cannot reach: controller output (e.g. DualSense
	// adaptive triggers) and input reports from custom devices (the LUX OPTIC head tracker).
	// Implemented per platform in Core/Platform/<OS>/<OS>HID.cpp with system APIs only (Windows
	// SetupAPI/HID, Linux hidraw). Enumerate may run on any thread; a device handle or an
	// InputDevice is used by one thread at a time; DeviceGroup is main-thread only.

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

	// One device opened for input reports. Reads wait with a timeout, so the reading thread can
	// check for shutdown. Callers want the newest state and every queued report only adds latency,
	// so on Windows the OS input queue is cut to its minimum and flushed on open (hidraw has no such
	// control, and its queue starts empty at open; readers drain it themselves).
	class InputDevice
	{
	public:
		InputDevice();
		~InputDevice();
		InputDevice(const InputDevice&) = delete;
		InputDevice& operator=(const InputDevice&) = delete;

		bool Open(const std::string& path);
		void Close();
		bool IsOpen() const;

		// Reads one input report into `buffer`; byte 0 is the report ID when the device numbers its
		// reports. Returns the report size (truncated to `size`), 0 when none arrived within
		// `timeoutMs` (0 = poll), or -1 when the device is gone.
		int Read(uint8_t* buffer, size_t size, uint32_t timeoutMs);
	private:
		struct Impl;
		Scope<Impl> m_Impl;
	};

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
