// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "EditorLayer.h"
#include "Lux/Asset/AssimpMeshImporter.h"
#include "Lux/Asset/MeshSerializer.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Utilities/CommandLineParser.h"
#include "Lux/Core/ApplicationSettings.h"
#include "Lux/Renderer/MaterialGraph/MaterialGraphRegistry.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/ShaderPack.h"
#include "Lux/Platform/Vulkan/ShaderCompiler/VulkanShaderCompiler.h"

#define LUX_LOGS_IN_PERSISTENT_STORAGE
#define LUX_CRASH_WATCHDOG L"Editor"
#include "Lux/EntryPoint.h"

namespace Lux {

	// `Editor --build-shader-pack[=path]`: writes the engine shader pack and exits, opening no project
	// (Lucky Engine's flow). CreateApplication has already replaced the material-graph include with
	// the no-graph one, so Renderer::Init compiled every shader without any project's graphs.
	class ShaderPackBuildLayer : public Layer
	{
	public:
		explicit ShaderPackBuildLayer(std::filesystem::path output)
			: Layer("ShaderPackBuild"), m_Output(std::move(output)) {}

		void OnAttach() override
		{
			// Every editor compiles (CreateApplication installs the compiler in Dist too), and no pack was
			// loaded for this run, so the library holds freshly compiled programs.
			const bool built = ShaderCompiler::IsAvailable()
				&& ShaderPack::WriteStampedPack(Renderer::GetShaderLibrary(), m_Output, Renderer::CaptureShaderPackEnvironment(ShaderPackKind::Engine));
			g_ApplicationExitCode = built ? 0 : 1;
			Application::Get().QueueEvent([]() { Application::Get().DispatchEvent<WindowCloseEvent, true>(); });
		}

	private:
		std::filesystem::path m_Output;
	};

	// Every "Resources/..." load is relative to the working directory, so an Editor.exe started from
	// anywhere else (double-click in bin/) cannot find its shaders. Walk up from the executable to the
	// first folder that holds Resources/Shaders (the Editor/ source folder, or a packaged install).
	static void EnsureResourcesWorkingDirectory()
	{
		std::error_code ec;
		if (std::filesystem::exists("Resources/Shaders", ec))
			return;

#ifdef LUX_PLATFORM_WINDOWS
		wchar_t buffer[32768];
		const DWORD length = GetModuleFileNameW(nullptr, buffer, 32768);
		const std::filesystem::path exePath = length ? std::filesystem::path(std::wstring(buffer, length)) : std::filesystem::path();
#else
		const std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe", ec);
#endif
		for (std::filesystem::path dir = exePath.parent_path(); !dir.empty(); dir = dir.parent_path())
		{
			for (const std::filesystem::path& candidate : { dir, dir / "Editor" })
			{
				if (std::filesystem::exists(candidate / "Resources" / "Shaders", ec))
				{
					std::filesystem::current_path(candidate, ec);
					return;
				}
			}
			if (dir == dir.parent_path())
				break;
		}
		LUX_CORE_ERROR("Could not find the Resources folder; start the editor from the Editor/ folder.");
	}

	// Per-user editor data used to live in the working directory (App.lsettings, imgui.ini) and in a
	// persistent-storage folder named "Editor". Copy anything the LuxEngine folder does not have yet,
	// so moving there resets nothing. Known files only: a folder called "Editor" may belong to another
	// program. The old copies are left in place.
	static void MigrateLegacyUserData(const std::filesystem::path& userDataPath)
	{
		const std::filesystem::path legacyStoragePath = userDataPath.parent_path() / "Editor";
		const std::pair<std::filesystem::path, const char*> legacyFiles[] = {
			{ legacyStoragePath / "EditorLayout.yaml", "EditorLayout.yaml" },
			{ legacyStoragePath / "UserPreferences.yaml", "UserPreferences.yaml" },
			{ "App.lsettings", "App.lsettings" },
			{ "imgui.ini", "imgui.ini" },
		};

		for (const auto& [source, fileName] : legacyFiles)
		{
			const std::filesystem::path destination = userDataPath / fileName;
			if (!FileSystem::Exists(source) || FileSystem::Exists(destination))
				continue;

			// error_code form: a failed copy must not stop the editor from starting.
			std::error_code error;
			std::filesystem::copy_file(source, destination, error);
			if (!error)
				LUX_CORE_INFO_TAG("Editor", "Migrated {} to {}", source.string(), destination.string());
			else
				LUX_CORE_WARN_TAG("Editor", "Could not migrate {} to {}: {}", source.string(), destination.string(), error.message());
		}
	}

	class LuxEditor : public Application
	{
	public:
		LuxEditor(const ApplicationSpecification& spec, std::filesystem::path shaderPackOutput)
			: Application(spec)
		{
			if (shaderPackOutput.empty())
				PushLayer(new EditorLayer());
			else
				PushLayer(new ShaderPackBuildLayer(std::move(shaderPackOutput)));
		}
		~LuxEditor()
		{

		}
	};

	Application* CreateApplication(int argc, char** argv)
	{

		EnsureResourcesWorkingDirectory();

		Lux::CommandLineParser cli(argc, argv);

		auto raw = cli.GetRawArgs();
		if (raw.size() > 1) {
			LUX_CORE_WARN("More than one project path specified, using `{}'", raw[0]);
		}

		auto cd = cli.GetOpt("C");
		if (!cd.empty()) {
			Lux::FileSystem::SetWorkingDirectory(cd);
		}

		std::string_view projectPath;
		if (!raw.empty()) projectPath = raw[0];

		// After -C, since the legacy files were relative to the working directory.
		MigrateLegacyUserData(FileSystem::GetPersistentStoragePath());

		Lux::ApplicationSpecification specification;
		specification.Name = "Lux Editor";
		specification.WindowWidth = 1600;
		specification.WindowHeight = 900;
		specification.StartMaximized = true;
		specification.DeferWindowShow = true;   // EditorLayer shows it once the startup splash closes
		specification.VSync = true;
		specification.SettingsPath = FileSystem::GetPersistentStoragePath() / "App.lsettings";
#ifdef LUX_DIST
		// The editor compiles in every config, Dist included, so material graphs and shader edits work in
		// the editor that ships. Non-Dist builds install the compiler in Renderer::Init; the Dist player
		// never installs it and so never links shaderc.
		VulkanShaderCompiler::InstallAsShaderCompiler();
#endif

		/*specification.ScriptConfig.CoreAssemblyPath = "Resources/Scripts/Hazel-ScriptCore.dll";
		specification.ScriptConfig.EnableDebugging = true;
		specification.ScriptConfig.EnableProfiling = true;*/

		// Threading policy is a user setting persisted in App.lsettings (read here because the
		// RenderThread is constructed with it before the Application object exists). Defaults to
		// multi-threaded; the "Application Settings" panel lets the user force single-threaded.
		// On Linux, default to single-threaded to avoid render thread race conditions with
		// Wayland/Vulkan swapchain management until those are resolved.
		{
			Lux::ApplicationSettings settings(specification.SettingsPath);
#ifdef LUX_PLATFORM_LINUX
			specification.CoreThreadingPolicy = Lux::ThreadingPolicyFromString(settings.Get("Core.ThreadingPolicy", "Single"));
#else
			specification.CoreThreadingPolicy = Lux::ThreadingPolicyFromString(settings.Get("Core.ThreadingPolicy", "Multi"));
#endif

			// LUX_THREADING_POLICY (Single / Multi) overrides the setting for this launch only and
			// is never saved; tests/rendering/golden_run.py --threading uses it.
			if (FileSystem::HasEnvironmentVariable("LUX_THREADING_POLICY"))
				specification.CoreThreadingPolicy = Lux::ThreadingPolicyFromString(FileSystem::GetEnvironmentVariable("LUX_THREADING_POLICY"));
		}

		// The editor imports source meshes in every config, Dist included; the player never does
		// (MeshSerializer.h). Registered before the Application exists, so before any asset loads.
		MeshSourceSerializer::SetSourceImporter([](const std::filesystem::path& path)
		{
			return AssimpMeshImporter(path).ImportToMeshSource();
		});

		std::filesystem::path shaderPackOutput;
		if (cli.HaveOpt("build-shader-pack"))
		{
			const std::string_view output = cli.GetOpt("build-shader-pack");
			shaderPackOutput = output.empty() ? std::filesystem::path("Resources/ShaderPack.lsp") : std::filesystem::path(output);
			MaterialGraphRegistry::ResetGeneratedInclude(); // before Renderer::Init compiles the shaders
		}
#ifdef LUX_DIST
		else
		{
			// The engine pack seeds the Dist editor's first launch (no 56-shader compile, ~3 s); a stale one
			// is skipped and compiled from source (Renderer::Init). Relative to the working directory, like
			// every "Resources/Shaders/..." load (the editor runs from Editor/, HelpPanel.cpp).
			specification.RenderConfig.ShaderPackPath = "Resources/ShaderPack.lsp";
		}
#endif

		return new LuxEditor(specification, std::move(shaderPackOutput));
	}
}
