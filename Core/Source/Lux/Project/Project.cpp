#include "lpch.h"
#include "Project.h"

#include "ProjectSerializer.h"

#include "Lux/Audio/AudioEngine.h"

namespace Lux
{
	std::filesystem::path Project::GetAssetAbsolutePath(const std::filesystem::path& path) const
	{
		return GetAssetDirectory() / path;
	}

	void Project::SetActive(Ref<Project> project)
	{
		if (s_AssetManager)
		{
			s_AssetManager->Shutdown();
			s_AssetManager = nullptr;
		}

		s_ActiveProject = project;
		if (!s_ActiveProject)
			return;

		if (AudioEngine::HasInitializedEngine())
		{
			AudioEngine::Shutdown();
			AudioEngine::SetInitalizedEngine(false);
		}

		s_AssetManager = Ref<EditorAssetManager>::Create();

		if (!s_ActiveProject->m_Config.StartScene.empty())
			s_ActiveProject->m_Config.StartSceneHandle = GetEditorAssetManager()->GetAssetHandleFromFilePath(s_ActiveProject->m_Config.StartScene);
		else if (s_ActiveProject->m_Config.StartSceneHandle)
		{
			AssetMetadata startSceneMetadata = GetEditorAssetManager()->GetMetadata(s_ActiveProject->m_Config.StartSceneHandle);
			if (startSceneMetadata.IsValid())
				s_ActiveProject->m_Config.StartScene = startSceneMetadata.FilePath.generic_string();
		}

		if (!AudioEngine::HasInitializedEngine())
		{
			AudioEngine::Init();
			AudioEngine::SetInitalizedEngine(true);
		}
	}

	void Project::SetActiveRuntime(Ref<Project> project)
	{
		if (s_AssetManager)
		{
			s_AssetManager->Shutdown();
			s_AssetManager = nullptr;
		}

		s_ActiveProject = project;
		if (!s_ActiveProject)
			return;

		s_AssetManager = Ref<RuntimeAssetManager>::Create();
	}

	Ref<Project> Project::New()
	{
		Ref<Project> project = Ref<Project>::Create();
		SetActive(project);
		return s_ActiveProject;
	}

	Ref<Project> Project::Load(const std::filesystem::path& path)
	{
		Ref<Project> project = Ref<Project>::Create();

		ProjectSerializer serializer(project);
		if (!serializer.Deserialize(path))
			return nullptr;

		project->m_ProjectDirectory = path.parent_path();
		SetActive(project);
		return s_ActiveProject;
	}

	bool Project::SaveActive(const std::filesystem::path& path)
	{
		LUX_CORE_ASSERT(s_ActiveProject);

		if (!s_ActiveProject->m_Config.StartScene.empty() && GetEditorAssetManager())
			s_ActiveProject->m_Config.StartSceneHandle = GetEditorAssetManager()->GetAssetHandleFromFilePath(s_ActiveProject->m_Config.StartScene);

		ProjectSerializer serializer(s_ActiveProject);
		if (!serializer.Serialize(path))
			return false;

		s_ActiveProject->m_ProjectDirectory = path.parent_path();
		return true;
	}
}
