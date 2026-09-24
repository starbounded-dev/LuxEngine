// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Core/HID.h"

#include <Windows.h>
#include <SetupAPI.h>
#include <hidsdi.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace Lux::HID {

	namespace {

		std::string ToUTF8(const wchar_t* wide)
		{
			const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
			if (size <= 1)
				return {};

			std::string result((size_t)size - 1, '\0');
			WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), size, nullptr, nullptr);
			return result;
		}

		std::wstring ToWide(const std::string& utf8)
		{
			const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
			if (size <= 1)
				return {};

			std::wstring result((size_t)size - 1, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, result.data(), size);
			return result;
		}

	}

	std::vector<DeviceInfo> Enumerate(uint16_t vendorID)
	{
		std::vector<DeviceInfo> devices;

		GUID hidGuid;
		HidD_GetHidGuid(&hidGuid);

		HDEVINFO deviceSet = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (deviceSet == INVALID_HANDLE_VALUE)
			return devices;

		SP_DEVICE_INTERFACE_DATA interfaceData{ sizeof(SP_DEVICE_INTERFACE_DATA) };
		for (DWORD index = 0; SetupDiEnumDeviceInterfaces(deviceSet, nullptr, &hidGuid, index, &interfaceData); index++)
		{
			DWORD detailSize = 0;
			SetupDiGetDeviceInterfaceDetailW(deviceSet, &interfaceData, nullptr, 0, &detailSize, nullptr);
			if (detailSize == 0)
				continue;

			std::vector<uint8_t> detailBuffer(detailSize);
			auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuffer.data());
			detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
			if (!SetupDiGetDeviceInterfaceDetailW(deviceSet, &interfaceData, detail, detailSize, nullptr, nullptr))
				continue;

			// Zero access is enough to query attributes, and works even while another process
			// (Steam, a game) has the device open.
			HANDLE handle = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
				continue;

			HIDD_ATTRIBUTES attributes{ sizeof(HIDD_ATTRIBUTES) };
			if (HidD_GetAttributes(handle, &attributes) && attributes.VendorID == vendorID)
			{
				DeviceInfo& info = devices.emplace_back();
				info.Path = ToUTF8(detail->DevicePath);
				info.VendorID = attributes.VendorID;
				info.ProductID = attributes.ProductID;

				// Bluetooth HID interfaces are enumerated under the HID-over-BT service GUID (classic)
				// or BTHLEDevice (LE).
				std::wstring path = detail->DevicePath;
				std::transform(path.begin(), path.end(), path.begin(), ::towlower);
				info.Bluetooth = path.find(L"{00001124-0000-1000-8000-00805f9b34fb}") != std::wstring::npos
					|| path.find(L"bthledevice") != std::wstring::npos;

				PHIDP_PREPARSED_DATA preparsed = nullptr;
				if (HidD_GetPreparsedData(handle, &preparsed))
				{
					HIDP_CAPS caps{};
					if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS)
						info.OutputReportSize = caps.OutputReportByteLength;
					HidD_FreePreparsedData(preparsed);
				}
			}

			CloseHandle(handle);
		}

		SetupDiDestroyDeviceInfoList(deviceSet);
		return devices;
	}

	DeviceHandle Open(const std::string& path)
	{
		HANDLE handle = CreateFileW(ToWide(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		return handle == INVALID_HANDLE_VALUE ? InvalidDevice : reinterpret_cast<DeviceHandle>(handle);
	}

	bool Write(DeviceHandle device, const uint8_t* data, size_t size)
	{
		if (device == InvalidDevice)
			return false;

		DWORD written = 0;
		return WriteFile(reinterpret_cast<HANDLE>(device), data, (DWORD)size, &written, nullptr) && written == size;
	}

	void Close(DeviceHandle device)
	{
		if (device != InvalidDevice)
			CloseHandle(reinterpret_cast<HANDLE>(device));
	}

}
