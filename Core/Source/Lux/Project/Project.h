#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "Lux/Asset/AssetManager/EditorAssetManager.h"
#include "Lux/Asset/AssetManager/RuntimeAssetManager.h"
#include "Lux/Core/Base.h"
#include "Lux/Core/Ref.h"
#include "Lux/Renderer/RendererTypes.h"

#include <glm/glm.hpp>

namespace Lux
{
	enum class PhysicsAPIType : uint8_t
	{
		Box2D = 0
	};

	enum class PhysicsCaptureMethod : uint8_t
	{
		LiveDebug = 0,
		CaptureToFile = 1
	};

	struct ProjectAudioSettings
	{
		double FileStreamingDurationThreshold = 1.0;
	};

	struct ProjectPhysicsLayer
	{
		std::string Name;
		bool CollidesWithSelf = true;
		std::vector<std::string> CollidesWith;
	};

	struct ProjectPhysicsSettings
	{
		float FixedTimestep = 1.0f / 60.0f;
		glm::vec3 Gravity = { 0.0f, -9.81f, 0.0f };
		uint32_t PositionSolverIterations = 8;
		uint32_t VelocitySolverIterations = 2;
		uint32_t MaxBodies = 1000;
		bool CaptureOnPlay = true;
		PhysicsCaptureMethod CaptureMethod = PhysicsCaptureMethod::LiveDebug;
		std::vector<ProjectPhysicsLayer> Layers;
	};

	struct ProjectSceneRendererSettings
	{
		bool EnableFrustumCulling = true;
		bool EnableOcclusionCulling = false;
		bool EnableGPUDrivenRendering = true;
		bool EnableGTAO = true;
		bool GTAOBentNormals = false;
		int GTAODenoisePasses = 4;
		float AOShadowTolerance = 1.0f;
		bool EnableSSR = false;
		bool EnableJumpFlood = true;
		uint32_t RenderScaleMode = 0; // SceneRendererOptions::RenderResolutionScaleMode
		float DynamicResolutionMinScale = 0.5f;
		float DynamicResolutionMaxScale = 1.0f;
		float DynamicResolutionTargetGPUTime = 16.67f;

		bool SoftShadows = true;
		bool EnableShadowCulling = true;
		float MaxShadowDistance = 200.0f;
		float ShadowFade = 25.0f;
		float ShadowCascadeSplitLambda = 0.92f;
		float ShadowCascadeNearPlaneOffset = 0.0f;
		float ShadowCascadeFarPlaneOffset = 50.0f;
		float ShadowCascadeTransitionFade = 1.0f;

		bool BloomEnabled = true;
		float BloomThreshold = 1.0f;
		float BloomKnee = 0.1f;
		float BloomUpsampleScale = 1.0f;
		float BloomIntensity = 1.0f;
		float BloomDirtIntensity = 1.0f;

		bool DOFEnabled = false;
		float DOFFocusDistance = 0.0f;
		float DOFBlurSize = 1.0f;

		bool SSRHalfRes = true;
		int SSRMaxSteps = 70;
		float SSRBrightness = 0.7f;
		float SSRDepthTolerance = 0.8f;
	};

	struct ProjectConfig
	{
		std::string Name = "Untitled";

		std::string StartScene;
		AssetHandle StartSceneHandle = 0;

		std::filesystem::path AssetDirectory = "Assets";
		std::filesystem::path AssetRegistryPath = "Assets/AssetRegistry.lzr";
		std::filesystem::path AudioCommandsRegistryPath = "AudioCommandsRegistry.lzr";
		std::filesystem::path MeshPath = "Meshes";
		std::filesystem::path MeshSourcePath = "Meshes/Source";
		std::filesystem::path AnimationPath = "Animation";
		std::filesystem::path ScriptModulePath = "Scripts/Binaries/App.dll";
		std::string DefaultNamespace = "Untitled";
		bool AutomaticallyReloadAssembly = true;
		bool EnableAutoSave = false;
		int AutoSaveIntervalSeconds = 300;
		PhysicsAPIType CurrentPhysicsAPI = PhysicsAPIType::Box2D;
		RenderingTechnique RendererTechnique = RenderingTechnique::Forward;

		std::string ProjectFileName;
		std::filesystem::path ProjectDirectory;

		ProjectAudioSettings Audio;
		ProjectPhysicsSettings Physics;
		ProjectSceneRendererSettings SceneRenderer;
	};

	class Project : public RefCounted
	{
	public:
		const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDirectory; }
		const std::filesystem::path& GetProjectFilePath() const { return m_ProjectFilePath; }

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

		std::filesystem::path GetAudioCommandsRegistryPath() const
		{
			return GetAssetDirectory() / m_Config.AudioCommandsRegistryPath;
		}

		std::filesystem::path GetMeshPath() const
		{
			return GetAssetDirectory() / m_Config.MeshPath;
		}

		std::filesystem::path GetMeshSourcePath() const
		{
			return GetAssetDirectory() / m_Config.MeshSourcePath;
		}

		std::filesystem::path GetAnimationPath() const
		{
			return GetAssetDirectory() / m_Config.AnimationPath;
		}

		std::filesystem::path GetScriptModuleFilePath() const
		{
			return GetAssetDirectory() / m_Config.ScriptModulePath;
		}

		std::filesystem::path GetScriptModulePath() const
		{
			return GetScriptModuleFilePath().parent_path();
		}

		std::filesystem::path GetScriptProjectPath() const
		{
			return GetAssetDirectory() / "Scripts" / (m_Config.Name + ".csproj");
		}

		std::filesystem::path GetCacheDirectory() const
		{
			return GetProjectDirectory() / "Cache";
		}

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

		static const std::string& GetProjectName()
		{
			LUX_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetConfig().Name;
		}

		static std::filesystem::path GetActiveAudioCommandsRegistryPath()
		{
			LUX_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetAudioCommandsRegistryPath();
		}

		static std::filesystem::path GetActiveMeshPath()
		{
			LUX_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetMeshPath();
		}

		static std::filesystem::path GetActiveMeshSourcePath()
		{
			LUX_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetMeshSourcePath();
		}

		static std::filesystem::path GetActiveAnimationPath()
		{
			LUX_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetAnimationPath();
		}

		static std::filesystem::path GetActiveScriptModuleFilePath()
		{
			LUX_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetScriptModuleFilePath();
		}

		static std::filesystem::path GetActiveScriptProjectPath()
		{
			LUX_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetScriptProjectPath();
		}

		static std::filesystem::path GetActiveCacheDirectory()
		{
			LUX_CORE_ASSERT(s_ActiveProject);
			return s_ActiveProject->GetCacheDirectory();
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
		static void SetActiveRuntime(Ref<Project> project, Ref<AssetPack> assetPack);
		void ReloadScriptEngine();

		static Ref<Project> New();
		static Ref<Project> Load(const std::filesystem::path& path);
		static bool SaveActive(const std::filesystem::path& path);

	private:
		void OnSerialized();
		void OnDeserialized();

	private:
		ProjectConfig m_Config;
		std::filesystem::path m_ProjectDirectory;
		std::filesystem::path m_ProjectFilePath;

		inline static Ref<Project> s_ActiveProject;
		inline static Ref<AssetManagerBase> s_AssetManager;

		friend class ProjectSerializer;
		friend class ProjectSettingsWindow;
	};
}
