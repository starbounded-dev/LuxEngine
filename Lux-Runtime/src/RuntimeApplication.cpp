#include "RuntimeLayer.h"

#include "Lux/EntryPoint.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Utilities/CommandLineParser.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Core/ApplicationSettings.h"

#include <yaml-cpp/yaml.h>

#include <charconv>

namespace Lux
{
	namespace
	{
		constexpr const char* s_RuntimeProjectFile = "Project.luxruntime";
		constexpr const char* s_RuntimeSettingsFile = "RuntimeSettings.yaml";
		constexpr const char* s_RuntimeShaderPackFile = "ShaderPack.lsp";

		std::filesystem::path ResolveRuntimeProjectFile(const std::filesystem::path& projectPath)
		{
			if (projectPath.empty())
				return std::filesystem::path("Assets") / s_RuntimeProjectFile;

			if (projectPath.extension() == ".luxruntime")
				return projectPath;

			const std::filesystem::path assetDirectoryPath = projectPath / "Assets" / s_RuntimeProjectFile;
			if (std::filesystem::exists(assetDirectoryPath))
				return assetDirectoryPath;

			return projectPath / s_RuntimeProjectFile;
		}

		void ApplyRuntimeSettings(ApplicationSpecification& specification, const std::filesystem::path& projectPath)
		{
			const std::filesystem::path runtimeProjectFile = ResolveRuntimeProjectFile(projectPath);
			const std::filesystem::path settingsFile = runtimeProjectFile.parent_path() / s_RuntimeSettingsFile;
			if (!std::filesystem::exists(settingsFile))
				return;

			YAML::Node data;
			try
			{
				data = YAML::LoadFile(settingsFile.string());
			}
			catch (const YAML::Exception& e)
			{
				LUX_CORE_WARN("Could not load runtime settings '{}': {}", settingsFile.string(), e.what());
				return;
			}

			YAML::Node runtimeNode = data["Runtime"];
			if (!runtimeNode)
				runtimeNode = data;

			specification.Name = runtimeNode["GameName"].as<std::string>(specification.Name);
			specification.WindowWidth = runtimeNode["WindowWidth"].as<uint32_t>(specification.WindowWidth);
			specification.WindowHeight = runtimeNode["WindowHeight"].as<uint32_t>(specification.WindowHeight);
			specification.Fullscreen = runtimeNode["Fullscreen"].as<bool>(specification.Fullscreen);
			specification.VSync = runtimeNode["VSync"].as<bool>(specification.VSync);

			// Optional per-game threading override (defaults to whatever CreateApplication resolved).
			const std::string threadingPolicy = runtimeNode["ThreadingPolicy"].as<std::string>("");
			if (!threadingPolicy.empty())
				specification.CoreThreadingPolicy = ThreadingPolicyFromString(threadingPolicy);

			std::filesystem::path iconPath = runtimeNode["IconPath"].as<std::string>("");
			if (!iconPath.empty())
			{
				if (iconPath.is_relative())
					iconPath = (settingsFile.parent_path() / iconPath).lexically_normal();
				specification.IconPath = iconPath;
			}
		}

		void ApplyRuntimeShaderPack(ApplicationSpecification& specification, const std::filesystem::path& projectPath)
		{
			const std::filesystem::path runtimeProjectFile = ResolveRuntimeProjectFile(projectPath);
			const std::filesystem::path exportRoot = runtimeProjectFile.parent_path().parent_path();
			const std::filesystem::path shaderPackPath = exportRoot / "Assets" / s_RuntimeShaderPackFile;
			if (std::filesystem::exists(shaderPackPath))
				specification.RenderConfig.ShaderPackPath = shaderPackPath.string();
		}

		uint32_t ParseUIntOption(CommandLineParser& cli, const std::string& optionName, uint32_t fallback)
		{
			const std::string_view value = cli.GetOpt(optionName);
			if (value.empty())
				return fallback;

			uint32_t parsed = fallback;
			const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
			if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
			{
				LUX_CORE_WARN("Ignoring invalid --{} value '{}'.", optionName, value);
				return fallback;
			}

			return parsed;
		}

		RendererDiagnosticsMode ParseRendererDiagnosticsMode(std::string_view value, RendererDiagnosticsMode fallback)
		{
			if (value.empty())
				return fallback;

			if (value == "off")
				return RendererDiagnosticsMode::Off;
			if (value == "basic")
				return RendererDiagnosticsMode::Basic;
			if (value == "full")
				return RendererDiagnosticsMode::Full;

			LUX_CORE_WARN("Unknown renderer diagnostics mode '{}'. Expected off, basic, or full.", value);
			return fallback;
		}

		bool ParseBoolOption(std::string_view value, bool fallback, std::string_view optionName)
		{
			if (value.empty())
				return fallback;

			if (value == "on" || value == "true" || value == "1" || value == "yes")
				return true;
			if (value == "off" || value == "false" || value == "0" || value == "no")
				return false;

			LUX_CORE_WARN("Unknown {} value '{}'. Expected on/off, true/false, or 1/0.", optionName, value);
			return fallback;
		}
	}

	class RuntimeApplication : public Application
	{
	public:
		RuntimeApplication(const ApplicationSpecification& specification, std::filesystem::path projectPath, RuntimeBenchmarkConfig benchmarkConfig)
			: Application(specification), m_ProjectPath(std::move(projectPath)), m_BenchmarkConfig(std::move(benchmarkConfig))
		{
			s_IsRuntime = true;

			// The runtime never exports/serializes meshes, so meshes keep only
			// positions + indices on the CPU (physics cooking) after GPU upload.
			MeshSource::SetRetainFullCPUGeometry(false);
		}

		void OnInit() override
		{
			PushLayer(new RuntimeLayer(m_ProjectPath, m_BenchmarkConfig));
		}

	private:
		std::filesystem::path m_ProjectPath;
		RuntimeBenchmarkConfig m_BenchmarkConfig;
	};

	Application* CreateApplication(int argc, char** argv)
	{
		CommandLineParser cli(argc, argv);

		const std::string_view workingDirectory = cli.GetOpt("C");
		if (!workingDirectory.empty())
			FileSystem::SetWorkingDirectory(workingDirectory);

		std::filesystem::path projectPath = ".";
		if (std::string_view projectOpt = cli.GetOpt("project"); !projectOpt.empty())
			projectPath = projectOpt;
		else
		{
			std::vector<std::string_view> rawArgs = cli.GetRawArgs();
			if (!rawArgs.empty())
				projectPath = rawArgs.front();
		}

		ApplicationSpecification specification;
		specification.Name = LUX_VERSION_LONG;
		specification.WindowWidth = 1920;
		specification.WindowHeight = 1080;
		specification.WindowDecorated = true;
		specification.Fullscreen = false;
		specification.Resizable = false;
		specification.StartMaximized = false;
		specification.EnableImGui = false;
		specification.VSync = false;
		specification.IconPath = "Resources/Editor/Hazel-IconLogo-2023.png";
		specification.RenderConfig.FramesInFlight = 3;
		// Base policy from App.lsettings (defaults to multi-threaded); RuntimeSettings.yaml may override per-game.
		{
			ApplicationSettings settings("App.lsettings");
			specification.CoreThreadingPolicy = ThreadingPolicyFromString(settings.Get("Core.ThreadingPolicy", "Multi"));
		}
		ApplyRuntimeSettings(specification, projectPath);
		ApplyRuntimeShaderPack(specification, projectPath);

		RuntimeBenchmarkConfig benchmarkConfig;
		if (const std::string_view benchmark = cli.GetOpt("benchmark"); !benchmark.empty())
		{
			if (benchmark != "sponza")
			{
				LUX_CORE_WARN("Unknown benchmark '{}'. Only 'sponza' is currently supported.", benchmark);
			}
			else
			{
				benchmarkConfig.Enabled = true;
				benchmarkConfig.Name = "sponza";
				benchmarkConfig.WarmupFrames = ParseUIntOption(cli, "benchmark-warmup", benchmarkConfig.WarmupFrames);
				benchmarkConfig.CaptureFrames = ParseUIntOption(cli, "benchmark-frames", benchmarkConfig.CaptureFrames);
				if (const std::string_view outputPath = cli.GetOpt("benchmark-output"); !outputPath.empty())
					benchmarkConfig.OutputPath = outputPath;

				specification.Name = "Lux Runtime Benchmark - Sponza";
				specification.WindowWidth = 1920;
				specification.WindowHeight = 1080;
				specification.Fullscreen = false;
				specification.Resizable = false;
				specification.StartMaximized = false;
				specification.VSync = false;
				specification.RenderConfig.DiagnosticsMode = RendererDiagnosticsMode::Basic;
				specification.RenderConfig.EnableGraphicsValidation = false;
			}
		}

		specification.RenderConfig.DiagnosticsMode = ParseRendererDiagnosticsMode(
			cli.GetOpt("renderer-diagnostics"),
			specification.RenderConfig.DiagnosticsMode);
		specification.VSync = ParseBoolOption(cli.GetOpt("vsync"), specification.VSync, "--vsync");

		return new RuntimeApplication(specification, projectPath, benchmarkConfig);
	}
}
