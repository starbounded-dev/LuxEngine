#include "RuntimeLayer.h"

#include "Lux/EntryPoint.h"
#include "Lux/Utilities/CommandLineParser.h"
#include "Lux/Utilities/FileSystem.h"

namespace Lux
{
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

		return new RuntimeApplication(specification, projectPath);
	}
}
