#pragma once

#include <filesystem>
#include <string>

#include "Lux/Asset/AssetManager/EditorAssetManager.h"
#include "Lux/Asset/AssetManager/RuntimeAssetManager.h"
#include "Lux/Core/Base.h"
#include "Lux/Core/Ref.h"

namespace Lux
{
	struct ProjectConfig
	{
		std::string Name = "Untitled";

		std::string StartScene;
		AssetHandle StartSceneHandle = 0;

		std::filesystem::path AssetDirectory = "Assets";
		std::filesystem::path AssetRegistryPath = "Assets/AssetRegistry.ser";
		std::filesystem::path ScriptModulePath;
	};

	class Project : public RefCounted
	{
	public:
		const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDirectory; }

		std::filesystem::path GetAssetDirectory() const
		{
			return GetProjectDirectory() / m_Config.AssetDirectory;
		}

		std::filesystem::path GetAssetRegistryPath() const
		{
			if (m_Config.AssetRegistryPath.is_absolute())
				return m_Config.AssetRegistryPath;

			return GetProjectDirectory() / m_Config.AssetRegistryPath;
		}

		std::filesystem::path GetAssetFileSystemPath(const std::filesystem::path& path) const
		{
			return GetAssetDirectory() / path;
		}

		std::filesystem::path GetAssetAbsolutePath(const std::filesystem::path& path) const;

		static const std::filesystem::path& GetActiveProjectDirectory()
		{
			LUX_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetProjectDirectory();
		}

		static std::filesystem::path GetActiveAssetDirectory()
		{
			LUX_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetAssetDirectory();
		}

		static std::filesystem::path GetActiveAssetRegistryPath()
		{
			LUX_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetAssetRegistryPath();
		}

		static std::filesystem::path GetActiveAssetFileSystemPath(const std::filesystem::path& path)
		{
			LUX_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetAssetFileSystemPath(path);
		}

		ProjectConfig& GetConfig() { return m_Config; }
		const ProjectConfig& GetConfig() const { return m_Config; }

		static Ref<Project> GetActive() { return s_ActiveProject; }
		static Ref<AssetManagerBase> GetAssetManager() { return s_AssetManager; }
		static Ref<RuntimeAssetManager> GetRuntimeAssetManager() { return s_AssetManager.As<RuntimeAssetManager>(); }
		static Ref<EditorAssetManager> GetEditorAssetManager() { return s_AssetManager.As<EditorAssetManager>(); }

		static void SetActive(Ref<Project> project);
		static void SetActiveRuntime(Ref<Project> project);

		static Ref<Project> New();
		static Ref<Project> Load(const std::filesystem::path& path);
		static bool SaveActive(const std::filesystem::path& path);

	private:
		ProjectConfig m_Config;
		std::filesystem::path m_ProjectDirectory;

		inline static Ref<Project> s_ActiveProject;
		inline static Ref<AssetManagerBase> s_AssetManager;
	};
}
