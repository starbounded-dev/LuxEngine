// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

// Nsight Aftermath GPU crash dumps (NVIDIA only). Off in Dist and with "--no-aftermath", where
// Core/premake5.lua also drops Debug/**.cpp; the inline stubs below keep callers free of #if.
#if !defined(LUX_DIST) && !defined(LUX_DISABLE_AFTERMATH)
	#define LUX_HAS_AFTERMATH 1
#else
	#define LUX_HAS_AFTERMATH 0
#endif

// The Aftermath library is loaded at runtime, so a machine without it (or without an NVIDIA
// driver) runs normally, without crash dumps. Dumps are written to <log directory>/GPUCrashDumps.
namespace Lux::Aftermath {

	// VulkanDeviceManager enables these, plus VkDeviceDiagnosticsConfigCreateInfoNV, when
	// Initialize succeeds.
	inline constexpr const char* k_DeviceExtensions[] = {
		VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME,
		VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME,
	};

#if LUX_HAS_AFTERMATH
	// Main thread, before vkCreateDevice. Loads the library and enables crash dumps; false (and
	// logged) when it cannot, after which every other function does nothing.
	bool Initialize();
	// Main thread, after vkCreateDevice with k_DeviceExtensions and GetDeviceDiagnosticsFlags().
	void OnDeviceCreated(VkDevice device, PFN_vkGetDeviceProcAddr getDeviceProcAddr);
	// Main thread, after vkDestroyDevice.
	void Shutdown();

	bool IsEnabled();
	VkDeviceDiagnosticsConfigFlagsNV GetDeviceDiagnosticsFlags();

	// The thread recording `commandBuffer` (the render thread). A crash dump names the last
	// checkpoint each command buffer reached. Labels are truncated to 63 characters.
	void SetCheckpoint(VkCommandBuffer commandBuffer, std::string_view label);
	// Any thread. Registers SPIR-V handed to the driver so a dump can map shader addresses.
	void AddShaderBinary(const uint32_t* spirv, size_t wordCount);
	// After VK_ERROR_DEVICE_LOST, on the thread that saw it. Waits (bounded) for Aftermath to
	// write the dump and returns its path; empty when Aftermath is off or wrote nothing.
	std::filesystem::path WaitForCrashDump();
#else
	inline bool Initialize() { return false; }
	inline void OnDeviceCreated(VkDevice, PFN_vkGetDeviceProcAddr) {}
	inline void Shutdown() {}

	inline bool IsEnabled() { return false; }
	inline VkDeviceDiagnosticsConfigFlagsNV GetDeviceDiagnosticsFlags() { return 0; }

	inline void SetCheckpoint(VkCommandBuffer, std::string_view) {}
	inline void AddShaderBinary(const uint32_t*, size_t) {}
	inline std::filesystem::path WaitForCrashDump() { return {}; }
#endif

}
