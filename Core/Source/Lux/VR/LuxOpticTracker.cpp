// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "LuxOpticTracker.h"

#include "Lux/Core/HID.h"
#include "Lux/Debug/Profiler.h"
#include "Lux/VR/StereoRig.h"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>

namespace Lux {

	namespace {

		constexpr uint8_t kOrientationReportID = 1;
		constexpr size_t kOrientationReportSize = 1 + 4 * sizeof(float);   // report ID, then w, x, y, z
		constexpr size_t kReportBufferSize = 65;                           // report ID + 64-byte payload
		// Bounds how long stopping waits for a silent device.
		constexpr uint32_t kReadTimeoutMs = 100;
		constexpr std::chrono::milliseconds kRetryInterval{ 500 };
		constexpr std::chrono::seconds kRateWindow{ 1 };

		static_assert(std::endian::native == std::endian::little, "The tracker sends little-endian floats.");

		bool ParseOrientationReport(const uint8_t* report, size_t size, glm::quat& orientation)
		{
			if (size < kOrientationReportSize || report[0] != kOrientationReportID)
				return false;

			float components[4];   // w, x, y, z
			std::memcpy(components, report + 1, sizeof(components));
			const glm::quat rotation(components[0], components[1], components[2], components[3]);

			const float lengthSquared = glm::dot(rotation, rotation);
			if (!std::isfinite(lengthSquared) || lengthSquared < 1e-6f)
				return false;

			orientation = rotation / std::sqrt(lengthSquared);
			return true;
		}

	}

	LuxOpticTracker::LuxOpticTracker()
		: m_Thread("Head Tracker")
	{
		m_Thread.Dispatch([this]() { Run(); });
	}

	LuxOpticTracker::~LuxOpticTracker()
	{
		{
			std::scoped_lock lock(m_StopMutex);
			m_Running = false;
		}
		m_StopCondition.notify_all();
		m_Thread.Join();
	}

	glm::quat LuxOpticTracker::GetOrientation() const
	{
		std::scoped_lock lock(m_PoseMutex);
		return m_Reference * m_RawOrientation;
	}

	void LuxOpticTracker::Recenter()
	{
		std::scoped_lock lock(m_PoseMutex);
		m_Reference = glm::inverse(StereoRig::ExtractYaw(m_RawOrientation));
	}

	void LuxOpticTracker::Run()
	{
		LUX_PROFILE_THREAD("Head Tracker");

		HID::InputDevice device;
		uint8_t report[kReportBufferSize];
		uint8_t newerReport[kReportBufferSize];
		bool reportedMissing = false;
		uint32_t reportsInWindow = 0;
		auto windowStart = std::chrono::steady_clock::now();

		while (m_Running)
		{
			if (!device.IsOpen())
			{
				if (!TryOpen(device))
				{
					if (!reportedMissing)
						LUX_CORE_INFO_TAG("VR", "LUX OPTIC tracker (USB {:04X}:{:04X}) not found; retrying in the background.", VendorID, ProductID);
					reportedMissing = true;

					if (!WaitBeforeRetry())
						break;
					continue;
				}

				LUX_CORE_INFO_TAG("VR", "LUX OPTIC tracker connected.");
				reportedMissing = false;
				m_Connected = true;
				reportsInWindow = 0;
				windowStart = std::chrono::steady_clock::now();
			}

			int size = device.Read(report, sizeof(report), kReadTimeoutMs);
			if (size > 0)
			{
				reportsInWindow++;

				// The newest report wins: drain whatever queued behind this one. A failure here shows
				// up again on the next blocking read.
				int newerSize = 0;
				while ((newerSize = device.Read(newerReport, sizeof(newerReport), 0)) > 0)
				{
					std::memcpy(report, newerReport, static_cast<size_t>(newerSize));
					size = newerSize;
					reportsInWindow++;
				}

				glm::quat orientation;
				if (ParseOrientationReport(report, static_cast<size_t>(size), orientation))
				{
					std::scoped_lock lock(m_PoseMutex);
					m_RawOrientation = orientation;
				}
			}
			else if (size < 0)
			{
				device.Close();
				m_Connected = false;
				m_ReportsPerSecond = 0;
				LUX_CORE_WARN_TAG("VR", "LUX OPTIC tracker disconnected; reconnecting in the background.");
				reportedMissing = true;   // this already said it is gone
				continue;
			}

			const auto now = std::chrono::steady_clock::now();
			const std::chrono::duration<float> windowLength = now - windowStart;
			if (windowLength >= kRateWindow)
			{
				m_ReportsPerSecond = static_cast<uint32_t>(std::lround(reportsInWindow / windowLength.count()));
				reportsInWindow = 0;
				windowStart = now;
			}
		}

		device.Close();
		m_Connected = false;
	}

	bool LuxOpticTracker::TryOpen(HID::InputDevice& device)
	{
		bool found = false;
		for (const HID::DeviceInfo& info : HID::Enumerate(VendorID))
		{
			if (info.ProductID != ProductID)
				continue;

			found = true;
			if (device.Open(info.Path))
				return true;
		}

		if (found && !m_WarnedOpen)
		{
			LUX_CORE_WARN_TAG("VR", "Found the LUX OPTIC tracker but could not open it for reading. On Linux, read access to "
				"/dev/hidraw* needs a udev rule (TAG+=\"uaccess\" for {:04x}:{:04x}); on Windows, close any program holding it "
				"exclusively (SteamVR normally shares it).", VendorID, ProductID);
			m_WarnedOpen = true;
		}
		return false;
	}

	bool LuxOpticTracker::WaitBeforeRetry()
	{
		std::unique_lock lock(m_StopMutex);
		return !m_StopCondition.wait_for(lock, kRetryInterval, [this]() { return !m_Running; });
	}

}
