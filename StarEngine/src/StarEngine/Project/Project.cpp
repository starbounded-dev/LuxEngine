#include "sepch.h"
#include "Project.h"

#include "ProjectSerializer.h"
#include "StarEngine/Audio/AudioEngine.h"
#include "StarEngine/Scripting/ScriptEngine.h"

namespace StarEngine {

	std::filesystem::path Project::GetAssetAbsolutePath(const std::filesystem::path& path)
	{
		return GetAssetDirectory() / path;
	}

	void Project::ReloadScriptEngine()
	{
		auto& scriptEngine = ScriptEngine::GetMutable();
		scriptEngine.Shutdown();
		scriptEngine.Initialize(s_ActiveProject);
	}

	Ref<Project> Project::New()
	{
		s_ActiveProject = Ref<Project>::Create();
		return s_ActiveProject;
	}

	Ref<Project> Project::Load(const std::filesystem::path& path)
	{
		if (s_ActiveProject)
			ScriptEngine::GetMutable().Shutdown();

		Ref<Project> project = Ref<Project>::Create();

		ProjectSerializer serializer(project);
		if (serializer.Serialize(path))
		{
			if (AudioEngine::HasInitializedEngine())
			{
				AudioEngine::Shutdown();
				AudioEngine::SetInitalizedEngine(false);
			}

			project->m_ProjectDirectory = path.parent_path();
			s_ActiveProject = project;

			std::shared_ptr<EditorAssetManager> editorAssetManager = std::make_shared<EditorAssetManager>();
			s_ActiveProject->m_AssetManager = editorAssetManager;
			editorAssetManager->DeserializeAssetRegistry();

			if (!AudioEngine::HasInitializedEngine())
			{
				AudioEngine::Init();
				AudioEngine::SetInitalizedEngine(true);
			}

			if (s_ActiveProject)
				ScriptEngine::GetMutable().Initialize(project);

			return s_ActiveProject;
		}

		return nullptr;
	}

	bool Project::SaveActive(const std::filesystem::path& path)
	{
		ProjectSerializer serializer(s_ActiveProject);
		if (serializer.Deserialize(path))
		{
			s_ActiveProject->m_ProjectDirectory = path.parent_path();
			return true;
		}

		return false;
	}

	void Project::UpdateEditorAssetManager()
	{
		if (AudioEngine::HasInitializedEngine())
		{
			AudioEngine::Shutdown();
			AudioEngine::SetInitalizedEngine(false);
		}

		std::shared_ptr<EditorAssetManager> editorAssetManager = std::make_shared<EditorAssetManager>();
		s_ActiveProject->m_AssetManager = editorAssetManager;
		editorAssetManager->DeserializeAssetRegistry();

		if (!AudioEngine::HasInitializedEngine())
		{
			AudioEngine::Init();
			AudioEngine::SetInitalizedEngine(true);
		}
	}

}
