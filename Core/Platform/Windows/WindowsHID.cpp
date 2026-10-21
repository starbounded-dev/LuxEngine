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

		// The HID class driver's minimum input queue (its default is 32 reports).
		constexpr ULONG kMinInputBuffers = 2;

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

	struct InputDevice::Impl
	{
		HANDLE Handle = INVALID_HANDLE_VALUE;
		HANDLE ReadEvent = nullptr;
		OVERLAPPED Overlapped{};
		// ReadFile must be given room for a whole report (InputReportByteLength), and a read that
		// timed out stays pending into this buffer, so it belongs to the device, not the caller.
		std::vector<uint8_t> Report;
		bool ReadPending = false;
	};

	InputDevice::InputDevice()
		: m_Impl(CreateScope<Impl>())
	{
	}

	InputDevice::~InputDevice()
	{
		Close();
	}

	bool InputDevice::Open(const std::string& path)
	{
		Close();

		HANDLE handle = CreateFileW(ToWide(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
		if (handle == INVALID_HANDLE_VALUE)
			return false;

		size_t reportSize = 0;
		PHIDP_PREPARSED_DATA preparsed = nullptr;
		if (HidD_GetPreparsedData(handle, &preparsed))
		{
			HIDP_CAPS caps{};
			if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS)
				reportSize = caps.InputReportByteLength;
			HidD_FreePreparsedData(preparsed);
		}

		HANDLE readEvent = reportSize > 0 ? CreateEventW(nullptr, TRUE, FALSE, nullptr) : nullptr;
		if (!readEvent)
		{
			// No input reports on this collection, or no event to wait on.
			CloseHandle(handle);
			return false;
		}

		HidD_SetNumInputBuffers(handle, kMinInputBuffers);
		HidD_FlushQueue(handle);

		m_Impl->Handle = handle;
		m_Impl->ReadEvent = readEvent;
		m_Impl->Report.assign(reportSize, 0);
		m_Impl->ReadPending = false;
		return true;
	}

	void InputDevice::Close()
	{
		if (m_Impl->Handle == INVALID_HANDLE_VALUE)
			return;

		if (m_Impl->ReadPending)
		{
			// The pending read still writes into Report and Overlapped: wait until it is cancelled.
			CancelIoEx(m_Impl->Handle, &m_Impl->Overlapped);
			DWORD ignored = 0;
			GetOverlappedResult(m_Impl->Handle, &m_Impl->Overlapped, &ignored, TRUE);
			m_Impl->ReadPending = false;
		}

		CloseHandle(m_Impl->ReadEvent);
		CloseHandle(m_Impl->Handle);
		m_Impl->ReadEvent = nullptr;
		m_Impl->Handle = INVALID_HANDLE_VALUE;
	}

	bool InputDevice::IsOpen() const
	{
		return m_Impl->Handle != INVALID_HANDLE_VALUE;
	}

	int InputDevice::Read(uint8_t* buffer, size_t size, uint32_t timeoutMs)
	{
		if (!IsOpen())
			return -1;

		if (!m_Impl->ReadPending)
		{
			m_Impl->Overlapped = {};
			m_Impl->Overlapped.hEvent = m_Impl->ReadEvent;
			ResetEvent(m_Impl->ReadEvent);
			if (!ReadFile(m_Impl->Handle, m_Impl->Report.data(), static_cast<DWORD>(m_Impl->Report.size()), nullptr, &m_Impl->Overlapped)
				&& GetLastError() != ERROR_IO_PENDING)
				return -1;

			m_Impl->ReadPending = true;
		}

		const DWORD wait = WaitForSingleObject(m_Impl->ReadEvent, timeoutMs);
		if (wait == WAIT_TIMEOUT)
			return 0;
		if (wait != WAIT_OBJECT_0)
			return -1;   // the read stays pending; Close cancels it

		m_Impl->ReadPending = false;
		DWORD bytesRead = 0;
		if (!GetOverlappedResult(m_Impl->Handle, &m_Impl->Overlapped, &bytesRead, FALSE))
			return -1;   // unplugged

		const size_t copied = std::min(static_cast<size_t>(bytesRead), size);
		std::memcpy(buffer, m_Impl->Report.data(), copied);
		return static_cast<int>(copied);
	}

}
