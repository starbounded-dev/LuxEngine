#include "RuntimeLayer.h"

#include "Lux/EntryPoint.h"
#include "Lux/Utilities/CommandLineParser.h"
#include "Lux/Utilities/FileSystem.h"

#include <yaml-cpp/yaml.h>

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
	}

	class RuntimeApplication : public Application
	{
	public:
		RuntimeApplication(const ApplicationSpecification& specification, std::filesystem::path projectPath)
			: Application(specification), m_ProjectPath(std::move(projectPath))
		{
			s_IsRuntime = true;
		}

		void OnInit() override
		{
			PushLayer(new RuntimeLayer(m_ProjectPath));
		}

	private:
		std::filesystem::path m_ProjectPath;
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
		specification.VSync = true;
		specification.IconPath = "Resources/Editor/Hazel-IconLogo-2023.png";
		specification.RenderConfig.FramesInFlight = 3;
		specification.CoreThreadingPolicy = ThreadingPolicy::SingleThreaded;
		ApplyRuntimeSettings(specification, projectPath);
		ApplyRuntimeShaderPack(specification, projectPath);

		return new RuntimeApplication(specification, projectPath);
	}
}
