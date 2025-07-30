#pragma once

#include <string>
#include <filesystem>

#include <StarEngine/Core/Assert.h>

#include "StarEngine/Asset/RuntimeAssetManager.h"
#include "StarEngine/Asset/EditorAssetManager.h"

namespace StarEngine {

	struct ProjectConfig
	{
		std::string Name = "Untitled";

		AssetHandle StartScene;

		std::filesystem::path AssetDirectory;
		std::filesystem::path AssetRegistryPath; // Relative to AssetDirectory
		std::filesystem::path ScriptModulePath;
	};

	class Project : public RefCounted
	{
	public:
		const std::filesystem::path& GetProjectDirectory() { return m_ProjectDirectory; }
		std::filesystem::path GetAssetDirectory() { return GetProjectDirectory() / s_ActiveProject->m_Config.AssetDirectory; }
		std::filesystem::path GetAssetRegistryPath() { return GetAssetDirectory() / s_ActiveProject->m_Config.AssetRegistryPath; }
		std::filesystem::path GetAssetFileSystemPath(const std::filesystem::path& path) { return GetAssetDirectory() / path; }

		std::filesystem::path GetAssetAbsolutePath(const std::filesystem::path& path);

		inline static Ref<AssetManagerBase> GetAssetManager() { return s_AssetManager; }

		static const std::filesystem::path& GetActiveProjectDirectory()
		{
			SE_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetProjectDirectory();
		}

		static std::filesystem::path GetActiveAssetDirectory()
		{
			SE_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetAssetDirectory();
		}

		static std::filesystem::path GetActiveAssetRegistryPath()
		{
			SE_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetAssetRegistryPath();
		}

		static std::filesystem::path GetActiveAssetFileSystemPath(const std::filesystem::path& path)
		{
			SE_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetAssetFileSystemPath(path);
		}

		static std::filesystem::path GetScriptModulePath()
		{
			SE_CORE_ASSERT(s_ActiveProject);
			return std::filesystem::path(s_ActiveProject->m_ProjectDirectory) / s_ActiveProject->GetConfig().AssetDirectory / s_ActiveProject->GetConfig().ScriptModulePath;
		}

		static std::filesystem::path GetScriptModuleFilePath()
		{
			SE_CORE_ASSERT(s_ActiveProject);
			return GetScriptModulePath();
		}

		ProjectConfig& GetConfig() { return m_Config; }

		void ReloadScriptEngine();

		static Ref<Project> GetActive() { return s_ActiveProject; }
		std::shared_ptr<AssetManagerBase> GetAssetManager() { return m_AssetManager; }
		std::shared_ptr<RuntimeAssetManager> GetRuntimeAssetManager() { return std::static_pointer_cast<RuntimeAssetManager>(m_AssetManager); }
		std::shared_ptr<EditorAssetManager> GetEditorAssetManager() { return std::static_pointer_cast<EditorAssetManager>(m_AssetManager); }

		static Ref<Project> New();
		static Ref<Project> Load(const std::filesystem::path& path);
		static bool SaveActive(const std::filesystem::path& path);
		static void UpdateEditorAssetManager();

	private:
		ProjectConfig m_Config;
		std::filesystem::path m_ProjectDirectory;
		std::shared_ptr<AssetManagerBase> m_AssetManager;
		inline static bool m_HasInitializedScriptEngine = false;

		inline static Ref<Project> s_ActiveProject;
	};

}
