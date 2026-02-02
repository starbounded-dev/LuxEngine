#include "sepch.h"
#include "Project.h"

#include "ProjectSerializer.h"
#include"StarEngine/Audio/AudioEngine.h"

namespace StarEngine {

	std::filesystem::path Project::GetAssetAbsolutePath(const std::filesystem::path& path)
	{
		return GetAssetDirectory() / path;
	}

	Ref<Project> Project::New()
	{
		s_ActiveProject = CreateRef<Project>();
		return s_ActiveProject;
	}

	Ref<Project> Project::Load(const std::filesystem::path& path)
	{
		Ref<Project> project = CreateRef<Project>();

		ProjectSerializer serializer(project);
		if (serializer.Deserialize(path))
		{
			if (AudioEngine::HasInitializedEngine())
			{
				AudioEngine::Shutdown();
				AudioEngine::SetInitalizedEngine(false);
			}

			project->m_ProjectDirectory = path.parent_path();
			s_ActiveProject = project;

			Ref<EditorAssetManager> editorAssetManager = std::make_shared<EditorAssetManager>();
			s_ActiveProject->m_AssetManager = editorAssetManager;
			editorAssetManager->DeserializeAssetRegistry();

			if (!AudioEngine::HasInitializedEngine())
			{
				AudioEngine::Init();
				AudioEngine::SetInitalizedEngine(true);
			}


			return s_ActiveProject;
		}

		return nullptr;
	}

	bool Project::SaveActive(const std::filesystem::path& path)
	{
		ProjectSerializer serializer(s_ActiveProject);
		if (serializer.Serialize(path))
		{
			s_ActiveProject->m_ProjectDirectory = path.parent_path();
			return true;
		}

		return false;
	}

}
