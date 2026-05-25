#include "lpch.h"
#include "Project.h"

#include "ProjectSerializer.h"

#include "Lux/Audio/AudioEngine.h"
#include "Lux/Scripting/ScriptEngine.h"

namespace Lux
{
	std::filesystem::path Project::GetAssetAbsolutePath(const std::filesystem::path& path) const
	{
		return GetAssetDirectory() / path;
	}

	void Project::ReloadScriptEngine()
	{
		ScriptEngine::ReloadAssembly();
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

		s_ActiveProject->m_Config.ProjectDirectory = s_ActiveProject->m_ProjectDirectory;
		s_ActiveProject->m_Config.ProjectFileName = s_ActiveProject->m_ProjectFilePath.filename().string();

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

	void Project::SetActiveRuntime(Ref<Project> project, Ref<AssetPack> assetPack)
	{
		if (s_AssetManager)
		{
			s_AssetManager->Shutdown();
			s_AssetManager = nullptr;
		}

		s_ActiveProject = project;
		if (AudioEngine::HasInitializedEngine())
		{
			AudioEngine::Shutdown();
			AudioEngine::SetInitalizedEngine(false);
		}

		if (!s_ActiveProject)
			return;

		if (s_ActiveProject->m_ProjectDirectory.empty())
			s_ActiveProject->m_ProjectDirectory = s_ActiveProject->m_Config.ProjectDirectory;
		if (s_ActiveProject->m_ProjectFilePath.empty() && !s_ActiveProject->m_ProjectDirectory.empty())
			s_ActiveProject->m_ProjectFilePath = s_ActiveProject->m_ProjectDirectory / s_ActiveProject->m_Config.ProjectFileName;

		s_ActiveProject->m_Config.ProjectDirectory = s_ActiveProject->m_ProjectDirectory;
		s_ActiveProject->m_Config.ProjectFileName = s_ActiveProject->m_ProjectFilePath.filename().string();

		s_AssetManager = Ref<RuntimeAssetManager>::Create();
		GetRuntimeAssetManager()->SetAssetPack(assetPack);

		if (!AudioEngine::HasInitializedEngine())
		{
			AudioEngine::Init();
			AudioEngine::SetInitalizedEngine(true);
		}
	}

	Ref<Project> Project::New()
	{
		Ref<Project> project = Ref<Project>::Create();
		project->m_Config.DefaultNamespace = project->m_Config.Name;
		SetActive(project);
		return s_ActiveProject;
	}

	Ref<Project> Project::LoadRuntime(const std::filesystem::path& path, Ref<AssetPack> assetPack)
	{
		Ref<Project> project = Ref<Project>::Create();

		ProjectSerializer serializer(project);
		if (!serializer.DeserializeRuntime(path))
			return nullptr;

		project->m_ProjectFilePath = path.lexically_normal();
		project->m_ProjectDirectory = path.parent_path();
		project->m_Config.ProjectDirectory = project->m_ProjectDirectory;
		project->m_Config.ProjectFileName = project->m_ProjectFilePath.filename().string();

		SetActiveRuntime(project, assetPack);
		return s_ActiveProject;
	}

	Ref<Project> Project::Load(const std::filesystem::path& path)
	{
		Ref<Project> project = Ref<Project>::Create();

		ProjectSerializer serializer(project);
		if (!serializer.Deserialize(path))
			return nullptr;

		project->m_ProjectFilePath = path.lexically_normal();
		project->m_ProjectDirectory = path.parent_path();
		project->m_Config.ProjectDirectory = project->m_ProjectDirectory;
		project->m_Config.ProjectFileName = project->m_ProjectFilePath.filename().string();
		SetActive(project);
		return s_ActiveProject;
	}

	bool Project::SaveActive(const std::filesystem::path& path)
	{
		LUX_CORE_ASSERT(s_ActiveProject);

		if (!s_ActiveProject->m_Config.StartScene.empty() && GetEditorAssetManager())
			s_ActiveProject->m_Config.StartSceneHandle = GetEditorAssetManager()->GetAssetHandleFromFilePath(s_ActiveProject->m_Config.StartScene);

		if (s_ActiveProject->m_Config.DefaultNamespace.empty())
			s_ActiveProject->m_Config.DefaultNamespace = s_ActiveProject->m_Config.Name;

		s_ActiveProject->m_ProjectFilePath = path.lexically_normal();
		s_ActiveProject->m_ProjectDirectory = path.parent_path();
		s_ActiveProject->m_Config.ProjectDirectory = s_ActiveProject->m_ProjectDirectory;
		s_ActiveProject->m_Config.ProjectFileName = s_ActiveProject->m_ProjectFilePath.filename().string();

		ProjectSerializer serializer(s_ActiveProject);
		if (!serializer.Serialize(path))
			return false;

		return true;
	}

	void Project::OnSerialized()
	{
	}

	void Project::OnDeserialized()
	{
		if (m_Config.DefaultNamespace.empty())
			m_Config.DefaultNamespace = m_Config.Name;

		if (m_Config.ScriptModulePath.empty())
			m_Config.ScriptModulePath = std::filesystem::path("Scripts/Binaries") / (m_Config.Name + ".dll");
	}
}
