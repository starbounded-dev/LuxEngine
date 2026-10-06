// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Aftermath.h"

#include "Lux/Core/SharedLibrary.h"
#include "Lux/Utilities/StringUtils.h"

// The decoding header declares the SPIR-V entry points only after <vulkan/vulkan.h> (Aftermath.h).
#include <GFSDK_Aftermath.h>
#include <GFSDK_Aftermath_GpuCrashDump.h>
#include <GFSDK_Aftermath_GpuCrashDumpDecoding.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>

namespace Lux::Aftermath {

	namespace {

#ifdef LUX_PLATFORM_WINDOWS
		constexpr const char* k_LibraryName = "GFSDK_Aftermath_Lib.x64.dll";
#else
		constexpr const char* k_LibraryName = "libGFSDK_Aftermath_Lib.x64.so";
#endif

		// About 20 frames of render-pass markers; a slot is reused after that.
		constexpr uint32_t k_CheckpointCount = 1024;
		constexpr size_t k_CheckpointLabelSize = 64;
		constexpr auto k_CrashDumpTimeout = std::chrono::seconds(10);
		constexpr auto k_CrashDumpPollInterval = std::chrono::milliseconds(50);

		// Entry points of the runtime-loaded library, typed from the SDK's declarations (which are
		// never called directly, so nothing links against the import library).
		struct API
		{
			decltype(&GFSDK_Aftermath_EnableGpuCrashDumps) EnableGpuCrashDumps = nullptr;
			decltype(&GFSDK_Aftermath_DisableGpuCrashDumps) DisableGpuCrashDumps = nullptr;
			decltype(&GFSDK_Aftermath_GetShaderDebugInfoIdentifier) GetShaderDebugInfoIdentifier = nullptr;
			decltype(&GFSDK_Aftermath_GetShaderHashSpirv) GetShaderHashSpirv = nullptr;
			decltype(&GFSDK_Aftermath_GpuCrashDump_CreateDecoder) CreateDecoder = nullptr;
			decltype(&GFSDK_Aftermath_GpuCrashDump_DestroyDecoder) DestroyDecoder = nullptr;
			decltype(&GFSDK_Aftermath_GpuCrashDump_GetEventMarkersInfoCount) GetEventMarkersInfoCount = nullptr;
			decltype(&GFSDK_Aftermath_GpuCrashDump_GetEventMarkersInfo) GetEventMarkersInfo = nullptr;
			decltype(&GFSDK_Aftermath_GpuCrashDump_GenerateJSON) GenerateJSON = nullptr;
			decltype(&GFSDK_Aftermath_GpuCrashDump_GetJSON) GetJSON = nullptr;
		};

		struct Checkpoint
		{
			char Label[k_CheckpointLabelSize];
		};

		struct AftermathData
		{
			void* Library = nullptr;
			API Api;
			VkDeviceDiagnosticsConfigFlagsNV DiagnosticsFlags = 0;
			std::filesystem::path DumpDirectory;

			// Written on the render thread; vkCmdSetCheckpointNV records the slot's address, which
			// the crash dump hands back so OnCrashDump can name it.
			PFN_vkCmdSetCheckpointNV CmdSetCheckpoint = nullptr;
			std::array<Checkpoint, k_CheckpointCount> Checkpoints = {};
			std::atomic<uint32_t> NextCheckpoint = 0;

			// Guards everything below. OnCrashDump holds it while the decoder calls the lookups.
			std::mutex Mutex;
			std::map<uint64_t, std::vector<uint32_t>> Shaders; // by GFSDK_Aftermath_ShaderHash
			std::map<std::pair<uint64_t, uint64_t>, std::vector<uint8_t>> ShaderDebugInfo; // by identifier
			std::filesystem::path DumpPath;
			std::atomic<bool> DumpFinished = false;
		};

		AftermathData* s_Data = nullptr;

		template<typename T>
		bool LoadSymbol(void* library, const char* name, T& function)
		{
			function = reinterpret_cast<T>(SharedLibrary::GetSymbol(library, name));
			return function != nullptr;
		}

		bool LoadAPI(void* library, API& api)
		{
			return LoadSymbol(library, "GFSDK_Aftermath_EnableGpuCrashDumps", api.EnableGpuCrashDumps)
				&& LoadSymbol(library, "GFSDK_Aftermath_DisableGpuCrashDumps", api.DisableGpuCrashDumps)
				&& LoadSymbol(library, "GFSDK_Aftermath_GetShaderDebugInfoIdentifier", api.GetShaderDebugInfoIdentifier)
				&& LoadSymbol(library, "GFSDK_Aftermath_GetShaderHashSpirv", api.GetShaderHashSpirv)
				&& LoadSymbol(library, "GFSDK_Aftermath_GpuCrashDump_CreateDecoder", api.CreateDecoder)
				&& LoadSymbol(library, "GFSDK_Aftermath_GpuCrashDump_DestroyDecoder", api.DestroyDecoder)
				&& LoadSymbol(library, "GFSDK_Aftermath_GpuCrashDump_GetEventMarkersInfoCount", api.GetEventMarkersInfoCount)
				&& LoadSymbol(library, "GFSDK_Aftermath_GpuCrashDump_GetEventMarkersInfo", api.GetEventMarkersInfo)
				&& LoadSymbol(library, "GFSDK_Aftermath_GpuCrashDump_GenerateJSON", api.GenerateJSON)
				&& LoadSymbol(library, "GFSDK_Aftermath_GpuCrashDump_GetJSON", api.GetJSON);
		}

		void WriteFile(const std::filesystem::path& path, const void* data, size_t size)
		{
			std::ofstream file(path, std::ios::out | std::ios::binary);
			if (file)
				file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
			else
				LUX_CORE_ERROR_TAG("Renderer", "Aftermath: could not write {}", path.string());
		}

		// A marker is the address of a checkpoint slot (vkCmdSetCheckpointNV stores only the pointer).
		std::string ResolveMarker(const GFSDK_Aftermath_GpuCrashDump_EventMarkerInfo& marker)
		{
			if (marker.markerDataSize > 0)
				return std::string(static_cast<const char*>(marker.markerData), marker.markerDataSize);

			const uintptr_t address = reinterpret_cast<uintptr_t>(marker.markerData);
			const uintptr_t begin = reinterpret_cast<uintptr_t>(s_Data->Checkpoints.data());
			const uintptr_t end = begin + sizeof(s_Data->Checkpoints);
			if (address < begin || address >= end || (address - begin) % sizeof(Checkpoint) != 0)
				return std::format("<unknown marker 0x{:X}>", address);

			const Checkpoint& checkpoint = *reinterpret_cast<const Checkpoint*>(address);
			return std::string(checkpoint.Label, strnlen(checkpoint.Label, k_CheckpointLabelSize));
		}

		// Called by the decoder inside OnCrashDump, which already holds s_Data->Mutex.
		void GFSDK_AFTERMATH_CALL OnShaderDebugInfoLookup(const GFSDK_Aftermath_ShaderDebugInfoIdentifier* identifier, PFN_GFSDK_Aftermath_SetData setShaderDebugInfo, void* userData)
		{
			auto it = s_Data->ShaderDebugInfo.find({ identifier->id[0], identifier->id[1] });
			if (it != s_Data->ShaderDebugInfo.end())
				setShaderDebugInfo(it->second.data(), static_cast<uint32_t>(it->second.size()));
		}

		// Called by the decoder inside OnCrashDump, which already holds s_Data->Mutex.
		void GFSDK_AFTERMATH_CALL OnShaderLookup(const GFSDK_Aftermath_ShaderHash* shaderHash, PFN_GFSDK_Aftermath_SetData setShaderBinary, void* userData)
		{
			auto it = s_Data->Shaders.find(shaderHash->hash);
			if (it != s_Data->Shaders.end())
				setShaderBinary(it->second.data(), static_cast<uint32_t>(it->second.size() * sizeof(uint32_t)));
		}

		// Aftermath thread. With DeferDebugInfoCallbacks this runs once per shader, right before OnCrashDump.
		void GFSDK_AFTERMATH_CALL OnShaderDebugInfo(const void* shaderDebugInfo, const uint32_t shaderDebugInfoSize, void* userData)
		{
			GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier = {};
			if (!GFSDK_Aftermath_SUCCEED(s_Data->Api.GetShaderDebugInfoIdentifier(GFSDK_Aftermath_Version_API, shaderDebugInfo, shaderDebugInfoSize, &identifier)))
				return;

			std::lock_guard lock(s_Data->Mutex);
			const uint8_t* bytes = static_cast<const uint8_t*>(shaderDebugInfo);
			s_Data->ShaderDebugInfo[{ identifier.id[0], identifier.id[1] }].assign(bytes, bytes + shaderDebugInfoSize);

			// Nsight Graphics reads these beside the dump for shader address mapping.
			std::error_code error;
			std::filesystem::create_directories(s_Data->DumpDirectory, error);
			WriteFile(s_Data->DumpDirectory / std::format("shader-{:016x}-{:016x}.nvdbg", identifier.id[0], identifier.id[1]), shaderDebugInfo, shaderDebugInfoSize);
		}

		void GFSDK_AFTERMATH_CALL OnDescription(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription, void* userData)
		{
			addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName, "LuxEngine");
			addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationVersion, LUX_VERSION_LONG);
		}

		// Aftermath thread, after the device was lost: writes the dump, its JSON decoding and the
		// named checkpoints next to the logs, then releases WaitForCrashDump.
		void GFSDK_AFTERMATH_CALL OnCrashDump(const void* gpuCrashDump, const uint32_t gpuCrashDumpSize, void* userData)
		{
			std::lock_guard lock(s_Data->Mutex);
			const API& api = s_Data->Api;

			std::error_code error;
			std::filesystem::create_directories(s_Data->DumpDirectory, error);
			const std::filesystem::path dumpPath = s_Data->DumpDirectory / std::format("Lux_{}.nv-gpudmp", Utils::String::GetCurrentTimeString(true, true));
			WriteFile(dumpPath, gpuCrashDump, gpuCrashDumpSize);

			GFSDK_Aftermath_GpuCrashDump_Decoder decoder = {};
			if (GFSDK_Aftermath_SUCCEED(api.CreateDecoder(GFSDK_Aftermath_Version_API, gpuCrashDump, gpuCrashDumpSize, &decoder)))
			{
				uint32_t jsonSize = 0;
				if (GFSDK_Aftermath_SUCCEED(api.GenerateJSON(decoder, GFSDK_Aftermath_GpuCrashDumpDecoderFlags_ALL_INFO, GFSDK_Aftermath_GpuCrashDumpFormatterFlags_NONE,
					OnShaderDebugInfoLookup, OnShaderLookup, nullptr, nullptr, nullptr, &jsonSize)))
				{
					std::vector<char> json(jsonSize);
					if (GFSDK_Aftermath_SUCCEED(api.GetJSON(decoder, jsonSize, json.data())))
						WriteFile(std::filesystem::path(dumpPath).concat(".json"), json.data(), strnlen(json.data(), json.size()));
				}

				uint32_t markerCount = 0;
				std::vector<GFSDK_Aftermath_GpuCrashDump_EventMarkerInfo> markers;
				if (GFSDK_Aftermath_SUCCEED(api.GetEventMarkersInfoCount(decoder, &markerCount)) && markerCount > 0)
				{
					markers.resize(markerCount);
					if (!GFSDK_Aftermath_SUCCEED(api.GetEventMarkersInfo(decoder, markerCount, markers.data())))
						markers.clear();
				}

				std::string markerText;
				for (const GFSDK_Aftermath_GpuCrashDump_EventMarkerInfo& marker : markers)
				{
					const std::string line = std::format("context 0x{:X} (status {}): {}", marker.contextId, static_cast<uint32_t>(marker.contextStatus), ResolveMarker(marker));
					LUX_CORE_ERROR_TAG("Renderer", "Aftermath last checkpoint: {}", line);
					markerText += line + "\n";
				}
				WriteFile(std::filesystem::path(dumpPath).concat(".markers.txt"), markerText.data(), markerText.size());

				api.DestroyDecoder(decoder);
			}

			s_Data->DumpPath = dumpPath;
			s_Data->DumpFinished.store(true, std::memory_order_release);
		}

	}

	bool Initialize()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		LUX_CORE_VERIFY(!s_Data, "Aftermath is already initialized");

		void* library = SharedLibrary::Load(k_LibraryName);
		if (!library)
		{
			LUX_CORE_INFO_TAG("Renderer", "Aftermath unavailable ({} not found)", k_LibraryName);
			return false;
		}

		API api;
		if (!LoadAPI(library, api))
		{
			LUX_CORE_WARN_TAG("Renderer", "Aftermath unavailable ({} is missing an entry point)", k_LibraryName);
			SharedLibrary::Unload(library);
			return false;
		}

		s_Data = lnew AftermathData();
		s_Data->Library = library;
		s_Data->Api = api;
		s_Data->DumpDirectory = Log::GetLogDirectory() / "GPUCrashDumps";

		// Shader debug info is cached by Aftermath and only handed over (OnShaderDebugInfo) on a crash.
		const GFSDK_Aftermath_Result result = api.EnableGpuCrashDumps(GFSDK_Aftermath_Version_API, GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan,
			GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks, OnCrashDump, OnShaderDebugInfo, OnDescription, nullptr);
		if (!GFSDK_Aftermath_SUCCEED(result))
		{
			const bool oldDriver = result == GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported;
			LUX_CORE_WARN_TAG("Renderer", "Aftermath unavailable ({})", oldDriver ? "the NVIDIA driver is too old" : std::format("error 0x{:X}", static_cast<uint32_t>(result)));
			ldelete s_Data;
			s_Data = nullptr;
			SharedLibrary::Unload(library);
			return false;
		}

		s_Data->DiagnosticsFlags = VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV | VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV;
#ifdef LUX_DEBUG
		// A checkpoint per draw and dispatch: precise, but it costs GPU time, so Debug only.
		s_Data->DiagnosticsFlags |= VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV;
#endif

		LUX_CORE_INFO_TAG("Renderer", "Aftermath GPU crash dumps enabled (written to {})", s_Data->DumpDirectory.string());
		return true;
	}

	void OnDeviceCreated(VkDevice device, PFN_vkGetDeviceProcAddr getDeviceProcAddr)
	{
		if (s_Data)
			s_Data->CmdSetCheckpoint = reinterpret_cast<PFN_vkCmdSetCheckpointNV>(getDeviceProcAddr(device, "vkCmdSetCheckpointNV"));
	}

	void Shutdown()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!s_Data)
			return;

		// The library stays loaded until the process exits; Aftermath may still have threads running.
		s_Data->Api.DisableGpuCrashDumps();
		ldelete s_Data;
		s_Data = nullptr;
	}

	bool IsEnabled()
	{
		return s_Data != nullptr;
	}

	VkDeviceDiagnosticsConfigFlagsNV GetDeviceDiagnosticsFlags()
	{
		return s_Data ? s_Data->DiagnosticsFlags : 0;
	}

	void SetCheckpoint(VkCommandBuffer commandBuffer, std::string_view label)
	{
		if (!s_Data || !s_Data->CmdSetCheckpoint || !commandBuffer)
			return;

		Checkpoint& checkpoint = s_Data->Checkpoints[s_Data->NextCheckpoint.fetch_add(1, std::memory_order_relaxed) % k_CheckpointCount];
		const size_t length = std::min(label.size(), k_CheckpointLabelSize - 1);
		std::memcpy(checkpoint.Label, label.data(), length);
		checkpoint.Label[length] = '\0';
		s_Data->CmdSetCheckpoint(commandBuffer, &checkpoint);
	}

	void AddShaderBinary(const uint32_t* spirv, size_t wordCount)
	{
		if (!s_Data || !spirv || wordCount == 0)
			return;

		GFSDK_Aftermath_SpirvCode code = {};
		code.pData = const_cast<uint32_t*>(spirv);
		code.size = static_cast<uint32_t>(wordCount * sizeof(uint32_t));

		GFSDK_Aftermath_ShaderHash hash = {};
		if (!GFSDK_Aftermath_SUCCEED(s_Data->Api.GetShaderHashSpirv(GFSDK_Aftermath_Version_API, &code, &hash)))
			return;

		std::lock_guard lock(s_Data->Mutex);
		s_Data->Shaders.try_emplace(hash.hash, spirv, spirv + wordCount);
	}

	std::filesystem::path WaitForCrashDump()
	{
		if (!s_Data)
			return {};

		const auto deadline = std::chrono::steady_clock::now() + k_CrashDumpTimeout;
		while (!s_Data->DumpFinished.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(k_CrashDumpPollInterval);

		if (!s_Data->DumpFinished.load(std::memory_order_acquire))
			return {};

		std::lock_guard lock(s_Data->Mutex);
		return s_Data->DumpPath;
	}

}
