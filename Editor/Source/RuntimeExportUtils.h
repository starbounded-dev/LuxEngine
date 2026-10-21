// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Thread.h"
#include "Lux/Project/Project.h"
#include "Lux/Scripting/ScriptBuilder.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>

namespace Lux::RuntimeExport {

	inline constexpr const char* RuntimeProjectFile = "Project.luxruntime";
	inline constexpr const char* RuntimeAssetPackFile = "AssetPack.lap";
	inline constexpr const char* RuntimeShaderPackFile = "ShaderPack.lsp";

	inline constexpr const char* PlatformSuffix =
#ifdef LUX_PLATFORM_LINUX
		"-linux-x86_64";
#else
		"-windows-x86_64";
#endif

	inline constexpr const char* PlatformExportLabel =
#ifdef LUX_PLATFORM_LINUX
		"-Linux-x86_64";
#else
		"-Windows-x86_64";
#endif

	inline constexpr const char* RuntimeExeName =
#ifdef LUX_PLATFORM_LINUX
		"Lux-Runtime";
#else
		"Lux-Runtime.exe";
#endif

	// Export preparation runs once on the main thread when an export starts, never per frame.
	bool PrepareAudioBanks(const Project& project, AudioBankManifest& manifest);
	bool CopyAudioBanks(const Project& project, const AudioBankManifest& manifest, const std::filesystem::path& assets);
	bool CopyAudioLibraries(const std::filesystem::path& runtimeDirectory, const std::filesystem::path& exportRoot);

	bool FileExists(const std::filesystem::path& path);
	std::string SanitizeBuildName(std::string value);

	bool CopyFileIfExists(const std::filesystem::path& source, const std::filesystem::path& destination, bool required = false);
	bool CopyDirectoryRecursive(const std::filesystem::path& source, const std::filesystem::path& destination, bool skipDebugFiles = false);
	std::filesystem::path FindFirstExistingDirectory(std::initializer_list<std::filesystem::path> candidates);

	std::filesystem::path FindRepositoryRootFrom(std::filesystem::path start);
	std::filesystem::path GetRuntimeOutputDirectory(RuntimeExportTarget target);
	bool IsBuildConfigurationDirectory(const std::filesystem::path& path);
	std::filesystem::path GetRuntimeExecutablePath(RuntimeExportTarget target);
	bool IsRuntimeExecutableOutdated(const std::filesystem::path& runtimeExe, const std::filesystem::path& repositoryRoot);

	std::filesystem::path ResolveScriptProjectFile(Ref<Project> project);
	bool IsScriptModuleOutdated(const std::filesystem::path& scriptModule, const std::filesystem::path& scriptProject);

	// Run the MSBuild/make/dotnet subprocess and block until it exits, reading only the paths they
	// are given. They run on RuntimeBuildJob's worker; outError gets a reason for the export window.
	bool BuildRuntimeExecutable(RuntimeExportTarget target, const std::filesystem::path& repositoryRoot, std::string& outError);
	bool BuildScriptModule(RuntimeExportTarget target, const std::filesystem::path& scriptProject, const std::filesystem::path& scriptModule, std::string& outError);

	struct RuntimeBuildResult
	{
		bool RuntimeFailed = false;
		bool ScriptsFailed = false;
		std::string Error; // empty on success
	};

	// Builds Lux-Runtime and/or the active project's scripts on a named worker thread, so the editor
	// keeps drawing through a build that takes a minute or more. Main thread only: Start resolves every
	// path from the working directory and the active project, the worker reads nothing else, and Poll
	// hands the result back. One build at a time, because two MSBuilds on one project collide.
	class RuntimeBuildJob
	{
	public:
		RuntimeBuildJob();
		~RuntimeBuildJob();

		// False when a build is already running or a requested build cannot start; GetLastError says why.
		bool Start(RuntimeExportTarget target, bool buildRuntime, bool buildScripts);
		// Call once per frame. Returns the result once, on the first call after the worker finished.
		std::optional<RuntimeBuildResult> Poll();
		// Blocks until a running build finishes and drops its result: a subprocess started through
		// std::system cannot be interrupted.
		void Wait();

		bool IsRunning() const { return m_Running; }
		std::string GetStatusText() const;
		// Why the last build (or the last Start) failed; empty after a success.
		const std::string& GetLastError() const { return m_LastError; }
	private:
		enum class Step : uint8_t { Runtime, Scripts };

		Thread m_Thread;
		RuntimeExportTarget m_Target = RuntimeExportTarget::Release;
		std::filesystem::path m_RepositoryRoot; // empty: no Lux-Runtime build
		std::filesystem::path m_ScriptProject; // empty: no script build
		std::filesystem::path m_ScriptModule;
		// Written by the worker, read by the main thread only after m_Finished.
		RuntimeBuildResult m_Result;
		std::atomic<Step> m_Step = Step::Runtime;
		std::atomic<bool> m_Finished = false;
		bool m_Running = false;
		std::chrono::steady_clock::time_point m_StartTime;
		std::string m_LastError;
	};

}
