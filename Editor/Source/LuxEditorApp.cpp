#include "EditorLayer.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Utilities/CommandLineParser.h"

#include "Lux/EntryPoint.h"

namespace Lux {

	class LuxEditor : public Application
	{
	public:
		LuxEditor(const ApplicationSpecification& spec)
			: Application(spec)
		{
			PushLayer(new EditorLayer());
		}
		~LuxEditor()
		{

		}
	};

	Application* CreateApplication(int argc, char** argv)
	{

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

		Lux::ApplicationSpecification specification;
		specification.Name = "Lux Editor";
		specification.WindowWidth = 1600;
		specification.WindowHeight = 900;
		specification.StartMaximized = true;
		specification.VSync = true;
		//specification.RenderConfig.ShaderPackPath = "Resources/ShaderPack.lsp";

		/*specification.ScriptConfig.CoreAssemblyPath = "Resources/Scripts/Hazel-ScriptCore.dll";
		specification.ScriptConfig.EnableDebugging = true;
		specification.ScriptConfig.EnableProfiling = true;*/

		specification.CoreThreadingPolicy = Lux::ThreadingPolicy::SingleThreaded;

		return new LuxEditor(specification);
	}
}
