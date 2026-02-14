#include "Sandbox2D.h"
#include "Lux/Utilities/FileSystem.h"
#include "Lux/Utilities/CommandLineParser.h"

#include "Lux/EntryPoint.h"

#ifdef LUX_PLATFORM_WINDOWS
#include <Shlobj.h>
#endif

class Sandbox : public Lux::Application
{
public:
	Sandbox(const Lux::ApplicationSpecification& specification)
		: Lux::Application(specification)
	{
		//PushLayer(new ExampleLayer());
		PushLayer(new Sandbox2D());
	}

	~Sandbox()
	{

	}

};

Lux::Application* Lux::CreateApplication(int argc, char** argv)
{
	Lux::CommandLineParser cli(argc, argv);

	auto raw = cli.GetRawArgs();
	if(raw.size() > 1) {
		LUX_CORE_WARN("More than one project path specified, using `{}'", raw[0]);
	}

	auto cd = cli.GetOpt("C");
	if(!cd.empty()) {
		Lux::FileSystem::SetWorkingDirectory(cd);
	}

	std::string_view projectPath;
	if(!raw.empty()) projectPath = raw[0];

	Lux::ApplicationSpecification specification;
	specification.Name = "Sandbox";
	specification.WindowWidth = 1600;
	specification.WindowHeight = 900;
	specification.StartMaximized = true;
	specification.VSync = true;
	// specification.RenderConfig.ShaderPackPath = "Resources/ShaderPack.hsp";

	/*specification.ScriptConfig.CoreAssemblyPath = "Resources/Scripts/Hazel-ScriptCore.dll";
	specification.ScriptConfig.EnableDebugging = true;
	specification.ScriptConfig.EnableProfiling = true;*/

	specification.CoreThreadingPolicy = ThreadingPolicy::SingleThreaded;

	return new Sandbox(specification);
}
