#include "sepch.h"
#include "Project.h"

#include "StarEngine/Asset/AssetManager.h"
#include "StarEngine/Audio/AudioEngine.h"
#include "StarEngine/Audio/AudioEvents/AudioCommandRegistry.h"

#include "StarEngine/Physics/PhysicsSystem.h"

#include "StarEngine/Scripting/ScriptEngine.h"

namespace StarEngine {

	Project::Project()
	{
		m_AudioCommands = Ref<AudioCommandRegistry>::Create();
	}

	Project::~Project()
	{
	}

	void Project::ReloadScriptEngine()
	{
		auto& scriptEngine = ScriptEngine::GetMutable();
		scriptEngine.Shutdown();
		scriptEngine.Initialize(this);
		scriptEngine.LoadProjectAssembly();
	}

	void Project::SetActive(Ref<Project> project)
	{
		if (s_ActiveProject)
		{
			s_AssetManager->Shutdown();
			s_AssetManager = nullptr;
			ScriptEngine::GetMutable().Shutdown();
			PhysicsSystem::Shutdown();
			AudioCommandRegistry::Shutdown();
		}

		s_ActiveProject = project;
		if (s_ActiveProject)
		{
			PhysicsAPI::SetCurrentAPI(s_ActiveProject->GetConfig().CurrentPhysicsAPI);

			s_AssetManager = Ref<EditorAssetManager>::Create();
			PhysicsSystem::Init();

			MiniAudioEngine::Get().OnProjectLoaded();

			// AudioCommandsRegistry must be deserialized after AssetManager,
			// otheriwse all of the command action targets are going to be invalid and cleared!
			WeakRef<AudioCommandRegistry> audioCommands = s_ActiveProject->m_AudioCommands;
			AudioCommandRegistry::Init(audioCommands);

			ScriptEngine::GetMutable().Initialize(project);
		}
	}

	void Project::SetActiveRuntime(Ref<Project> project, Ref<AssetPack> assetPack)
	{
		if (s_ActiveProject)
		{
			s_AssetManager = nullptr;
			ScriptEngine::GetMutable().Shutdown();
			PhysicsSystem::Shutdown();
			AudioCommandRegistry::Shutdown();
		}

		s_ActiveProject = project;
		if (s_ActiveProject)
		{
			s_AssetManager = Ref<RuntimeAssetManager>::Create();
			Project::GetRuntimeAssetManager()->SetAssetPack(assetPack);

			PhysicsSystem::Init();

			// AudioCommandsRegistry must be deserialized after AssetManager,
			// otherwise all of the command action targets are going to be invalid and cleared!
			WeakRef<AudioCommandRegistry> audioCommands = s_ActiveProject->m_AudioCommands;
			//if (!runtime)
			MiniAudioEngine::Get().OnProjectLoaded();
			AudioCommandRegistry::Init(audioCommands);
			ScriptEngine::GetMutable().Initialize(project);
		}
	}

	void Project::OnSerialized()
	{
		m_AudioCommands->WriteRegistryToFile(std::filesystem::path(m_Config.ProjectDirectory) / m_Config.AudioCommandsRegistryPath);
	}

	void Project::OnDeserialized()
	{
	}

}
