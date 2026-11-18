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

	// An export is the executable (and its libraries) plus this one folder holding Assets, Resources, DotNet and
	// logs; the player makes it its working directory (Lux-Runtime LUX_ENTRY_PREPARE).
	inline constexpr const char* DataFolder = "Data";
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

	// Whether a library from the runtime build folder ships with an export: never FMOD's or Vercidium's,
	// which builds from before their removal may have left in the shared output folder; never Assimp, which
	// only the editor links; and in a Dist export not the shader compiler, which a Dist player doesn't link.
	bool ShipsLibrary(const std::string& filename, RuntimeExportTarget target);

	bool FileExists(const std::filesystem::path& path);
	std::string SanitizeBuildName(std::string value);

	bool CopyFileIfExists(const std::filesystem::path& source, const std::filesystem::path& destination, bool required = false);
	// Skips Cache/ and any top-level entry named in skip (a folder or a file).
	bool CopyDirectoryRecursive(const std::filesystem::path& source, const std::filesystem::path& destination, bool skipDebugFiles = false,
		std::initializer_list<std::string_view> skip = {});
	std::filesystem::path FindFirstExistingDirectory(std::initializer_list<std::filesystem::path> candidates);

	std::filesystem::path FindRepositoryRootFrom(std::filesystem::path start);
	// The repository root the editor can build from: the project's, else the working directory's. Empty in
	// an installed editor (the release zip), which has no source tree.
	std::filesystem::path FindRepositoryRoot(const std::filesystem::path& projectDirectory);
	// An engine folder the export copies from ("Resources", "DotNet"): beside the working directory (an
	// installed editor starts there), then the repository's Editor/ folder. Empty when not found.
	std::filesystem::path FindEditorDirectory(std::string_view name, const std::filesystem::path& projectDirectory);
	std::filesystem::path GetRuntimeOutputDirectory(RuntimeExportTarget target);
	bool IsBuildConfigurationDirectory(const std::filesystem::path& path);
	// The player an installed editor ships: <working directory>/Runtime/<Config>/. The release zip carries Dist.
	std::filesystem::path GetPrebuiltRuntimeDirectory(RuntimeExportTarget target);
	std::filesystem::path GetRuntimeExecutablePath(RuntimeExportTarget target);
	// The configuration to export with. `preferred` when its player exists or can be built here; otherwise
	// the first player an installed editor ships (Dist, Release, Debug). Empty when there is none.
	std::optional<RuntimeExportTarget> ResolveExportTarget(RuntimeExportTarget preferred, const std::filesystem::path& projectDirectory);
	bool IsRuntimeExecutableOutdated(const std::filesystem::path& runtimeExe, const std::filesystem::path& repositoryRoot);

	// A game with scripts ships its own .NET runtime under DotNet/Runtime, so players install nothing. Its
	// layout is the dotnet root's (host/fxr/<version>, shared/Microsoft.NETCore.App/<version>); the player
	// points hostfxr at it (ScriptEngine::InitializeHost).
	inline constexpr const char* BundledDotNetFolder = "Runtime";
	// The dotnet root the editor itself runs scripts with: DOTNET_ROOT, else the platform default. Empty when none.
	std::filesystem::path FindDotNetRoot();
	// Copies the newest installed .NET runtime (host and shared framework, major 10 or later) into
	// destination. False, with the reason in outError, when none is installed or a copy fails.
	bool BundleDotNetRuntime(const std::filesystem::path& dotNetRoot, const std::filesystem::path& destination, std::string& outError);
	// The newest versioned subfolder of directory whose major is at least minimumMajor, a stable release before
	// any preview; empty if none.
	std::filesystem::path FindNewestVersionDirectory(const std::filesystem::path& directory, uint32_t minimumMajor);

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
