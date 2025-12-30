#include "EditorLayer.h"
#include "StarEngine/Utilities/FileSystem.h"
#include "StarEngine/Utilities/CommandLineParser.h"

#include "StarEngine/EntryPoint.h"

#ifdef SE_PLATFORM_WINDOWS
#include <Shlobj.h>
#endif

class StarEditorApplication : public StarEngine::Application
{
public:
	StarEditorApplication(const StarEngine::ApplicationSpecification& specification, std::string_view projectPath)
		: Application(specification), m_ProjectPath(projectPath), m_UserPreferences(StarEngine::Ref<StarEngine::UserPreferences>::Create())
	{
		if (projectPath.empty())
			m_ProjectPath = "SandboxProject/Sandbox.starproj";
	}

	virtual void OnInit() override
	{
		// Persistent Storage
		{
			m_PersistentStoragePath = StarEngine::FileSystem::GetPersistentStoragePath() / "StarEditor";

			if (!StarEngine::FileSystem::Exists(m_PersistentStoragePath))
				StarEngine::FileSystem::CreateDirectory(m_PersistentStoragePath);
		}

		// User Preferences
		{
			StarEngine::UserPreferencesSerializer serializer(m_UserPreferences);
			if (!StarEngine::FileSystem::Exists(m_PersistentStoragePath / "UserPreferences.yaml"))
				serializer.Serialize(m_PersistentStoragePath / "UserPreferences.yaml");
			else
				serializer.Deserialize(m_PersistentStoragePath / "UserPreferences.yaml");

			if (!m_ProjectPath.empty())
				m_UserPreferences->StartupProject = m_ProjectPath;
			else if (!m_UserPreferences->StartupProject.empty())
				m_ProjectPath = m_UserPreferences->StartupProject;
		}

		// Update the STARENGINE_DIR environment variable every time we launch
		{
			auto workingDirectory = StarEngine::FileSystem::GetWorkingDirectory();

			if (workingDirectory.stem().string() == "StarEditor")
				workingDirectory = workingDirectory.parent_path();

			StarEngine::FileSystem::SetEnvironmentVariable("STARENGINE_DIR", workingDirectory.string());
		}

		PushLayer(new StarEngine::EditorLayer(m_UserPreferences));
	}

private:
	std::string m_ProjectPath;
	std::filesystem::path m_PersistentStoragePath;
	StarEngine::Ref<StarEngine::UserPreferences> m_UserPreferences;
};

StarEngine::Application* StarEngine::CreateApplication(int argc, char** argv)
{
	StarEngine::CommandLineParser cli(argc, argv);

	auto raw = cli.GetRawArgs();
	if (raw.size() > 1) {
		SE_CORE_WARN("More than one project path specified, using `{}'", raw[0]);
	}

	auto cd = cli.GetOpt("C");
	if (!cd.empty()) {
		StarEngine::FileSystem::SetWorkingDirectory(cd);
	}

	std::string_view projectPath;
	if (!raw.empty())
		projectPath = raw[0];
	if (argc > 1)
		projectPath = argv[1];

	StarEngine::ApplicationSpecification specification;
	specification.Name = "StarEditor";
	specification.WindowWidth = 1600;
	specification.WindowHeight = 900;
	specification.StartMaximized = true;
	specification.VSync = true;
	// specification.RenderConfig.ShaderPackPath = "Resources/ShaderPack.hsp";

	/*specification.ScriptConfig.CoreAssemblyPath = "Resources/Scripts/Hazel-ScriptCore.dll";
	specification.ScriptConfig.EnableDebugging = true;
	specification.ScriptConfig.EnableProfiling = true;*/

	specification.CoreThreadingPolicy = ThreadingPolicy::SingleThreaded;

	return new StarEditorApplication(specification, projectPath);
}
