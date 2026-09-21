// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "HID.h"

namespace Lux::HID {

	DeviceGroup::DeviceGroup(std::string name, uint16_t vendorID, std::vector<uint16_t> productIDs, size_t usbReportSize)
		: m_Name(std::move(name)), m_VendorID(vendorID), m_ProductIDs(std::move(productIDs)), m_USBReportSize(usbReportSize)
	{
	}

	void DeviceGroup::Close()
	{
		for (DeviceHandle device : m_Devices)
			HID::Close(device);

		m_Devices.clear();
		m_Enumerated = false;
	}

	void DeviceGroup::Enumerate()
	{
		Close();
		m_Enumerated = true;

		for (const DeviceInfo& info : HID::Enumerate(m_VendorID))
		{
			if (std::find(m_ProductIDs.begin(), m_ProductIDs.end(), info.ProductID) == m_ProductIDs.end())
				continue;

			if (info.Bluetooth || (info.OutputReportSize != 0 && info.OutputReportSize != m_USBReportSize))
			{
				if (!m_WarnedBluetooth)
					LUX_CORE_WARN("{} connected over Bluetooth: output effects need a USB cable (a Bluetooth output report would stop the controller's input from reaching the engine until it reconnects).", m_Name);
				m_WarnedBluetooth = true;
				continue;
			}

			const DeviceHandle device = HID::Open(info.Path);
			if (device == InvalidDevice)
			{
				if (!m_WarnedOpen)
				{
#ifdef LUX_PLATFORM_LINUX
					LUX_CORE_WARN("Could not open {} '{}' for output; write access to hidraw needs a udev rule.", m_Name, info.Path);
#else
					LUX_CORE_WARN("Could not open {} '{}' for output.", m_Name, info.Path);
#endif
				}
				m_WarnedOpen = true;
				continue;
			}

			m_Devices.push_back(device);
		}
	}

	void DeviceGroup::Write(const uint8_t* report, size_t size)
	{
		if (!m_Enumerated)
			Enumerate();

		std::erase_if(m_Devices, [report, size](DeviceHandle device)
		{
			if (HID::Write(device, report, size))
				return false;

			HID::Close(device);
			return true;
		});
	}

}
