// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Audio/DialogueTable.h"
#include "Lux/Audio/AudioAccessibilitySettings.h"
#include "Lux/Audio/AudioPerformanceSettings.h"
#include "Lux/Audio/AudioOcclusionSettings.h"

#include "Lux/Audio/AudioZoneSettings.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "Lux/Asset/AssetManager/EditorAssetManager.h"
#include "Lux/Asset/AssetManager/RuntimeAssetManager.h"
#include "Lux/Core/Base.h"
#include "Lux/Audio/AudioBankManifest.h"
#include "Lux/Audio/AcousticMaterial.h"
#include "Lux/Core/Ref.h"
#include "Lux/Renderer/RendererTypes.h"

#include <glm/glm.hpp>

namespace Lux
{
	enum class PhysicsAPIType : uint8_t
	{
		Box2D = 0,
		Jolt = 1
	};

	enum class PhysicsCaptureMethod : uint8_t
	{
		LiveDebug = 0,
		CaptureToFile = 1
	};

	enum class RuntimeExportTarget : uint8_t
	{
		Debug = 0,
		Release = 1,
		Dist = 2
	};

	const char* RuntimeExportTargetToString(RuntimeExportTarget target);
	RuntimeExportTarget RuntimeExportTargetFromString(std::string_view value);

	struct ProjectAudioSettings
	{
		double FileStreamingDurationThreshold = 1.0;

		// Populated only from the binary runtime package, never from editor YAML.
		AudioBankManifest RuntimeBanks;
		AssetHandle SurfaceTable = 0;
		DialogueSettings Dialogue;
		AudioAccessibilityConfig Accessibility;
		AudioPerformanceSettings Performance;
		AudioDesktopProfile Windows, Linux;
		std::string StudioPlatform = "Desktop";
		AcousticMaterialSettings AcousticMaterials;
		AudioZoneReverbMode ZoneReverbMode = AudioZoneReverbMode::Layered;
		AudioOcclusionSettings Occlusion;

		// The FMOD Studio project (.fspro) that authors this game's audio, relative to the asset
		// directory. Sound designers work in the Studio app; the engine consumes only the banks it
		// builds. Empty means the project has no authored audio yet.
		std::filesystem::path StudioProjectPath = "Audio/SampleProject/SampleProject.fspro";

		// Where fmodstudiocl writes built banks, relative to the .fspro's own directory. "Desktop"
		// is FMOD's default platform name; a project targeting consoles would build several of
		// these side by side.
		std::filesystem::path StudioBankOutputPath = "Build/Desktop";

		// Rebuild banks from the .fspro before entering Play when any source file is newer than the
		// built banks. Stale banks are the most common "why didn't my change take effect", and the
		// check is a timestamp comparison, so it costs nothing when nothing changed.
		bool RebuildBanksOnPlay = true;

		// Let the FMOD Studio app connect to the running engine and remix live. Costs a socket and
		// a little memory; the reason to author in Studio at all.
		bool EnableLiveUpdate = true;
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
		uint32_t QualityPreset = 1; // Lux::QualityPreset
		bool EnableFrustumCulling = true;
		bool EnableOcclusionCulling = true;
		bool EnableGPUDrivenRendering = true;
		bool EnableMeshLODs = true;
		float MeshLODDistanceScale = 1.0f;
		bool EnableVariableRateShading = true;
		bool EnableMeshShaders = false;
		bool EnableGTAO = true;
		bool GTAOBentNormals = false;
		int GTAODenoisePasses = 4;
		// XeGTAO sample counts. Serialized like every other resolved quality value,
		// because the tier in the project file is only a label on load.
		uint32_t GTAOSliceCount = 9;
		uint32_t GTAOStepsPerSlice = 3;
		float AOShadowTolerance = 1.0f;
		bool EnableSSR = true;
		bool EnableJumpFlood = true;
		uint32_t RenderScaleMode = 0; // SceneRendererOptions::RenderResolutionScaleMode
		// Only used when RenderScaleMode is FixedResolution.
		uint32_t FixedRenderWidth = 1920;
		uint32_t FixedRenderHeight = 1080;
		float DynamicResolutionMinScale = 0.5f;
		float DynamicResolutionMaxScale = 1.0f;
		float DynamicResolutionTargetGPUTime = 16.67f;
		float TextureMipBias = 0.0f;
		bool EnableDistanceMipBias = false;
		float DistanceMipBiasStart = 50.0f;
		float DistanceMipBiasEnd = 250.0f;
		float DistanceMipBiasMax = 2.0f;
		float OcclusionDepthBias = 0.003f;
		float OcclusionBoundsScale = 1.15f;
		uint32_t GTAOResolutionScale = 2; // SceneRendererOptions::EffectResolutionScale
		uint32_t SSRQuality = 1; // SceneRendererOptions::SSRQualityPreset
		uint32_t SSRResolutionScale = 2; // SceneRendererOptions::EffectResolutionScale

		// Antialiasing. SMAA needs the vendored lookup tables (Core/vendor/smaa) to be
		// present; without them these load but the renderer reports SMAA unavailable.
		bool EnableSMAA = false;
		float SMAAThreshold = 0.1f;
		float SMAALocalContrastAdaptationFactor = 2.0f;

		bool SoftShadows = true;
		bool EnableShadowCulling = true;
		float MaxShadowDistance = 200.0f;
		float ShadowFade = 25.0f;
		uint32_t ActiveShadowCascadeCount = 3;
		float ShadowCascadeSplitLambda = 0.82f;
		float ShadowCascadeNearPlaneOffset = 0.0f;
		float ShadowCascadeFarPlaneOffset = 50.0f;
		float ShadowCascadeTransitionFade = 1.0f;
		uint32_t ShadowFilterMode = 2; // SceneRendererOptions::ShadowFilterMode::Hybrid
		uint32_t DirectionalPCSSCascadeCount = 1;
		float ShadowPCFRadiusTexels = 1.25f;
		float SpotShadowPCFRadiusTexels = 1.5f;
		uint32_t ShadowResolution = 1; // SceneRendererOptions::ShadowResolutionTier (Tier_2K)

		bool BloomEnabled = true;
		uint32_t BloomResolutionScale = 2; // SceneRendererOptions::EffectResolutionScale
		float BloomThreshold = 1.0f;
		float BloomKnee = 0.1f;
		float BloomUpsampleScale = 1.0f;
		float BloomIntensity = 1.0f;
		float BloomDirtIntensity = 1.0f;

		bool DOFEnabled = false;
		uint32_t DOFResolutionScale = 1; // SceneRendererOptions::EffectResolutionScale
		float DOFFocusDistance = 0.0f;
		float DOFBlurSize = 1.0f;

		bool SSRHalfRes = true;
		int SSRMaxSteps = 70;
		float SSRBrightness = 0.7f;
		float SSRDepthTolerance = 0.8f;
	};

	struct ProjectRuntimeExportSettings
	{
		std::string GameName;
		uint32_t WindowWidth = 1920;
		uint32_t WindowHeight = 1080;
		bool Fullscreen = false;
		bool VSync = true;
		std::filesystem::path IconPath;
		AssetHandle IconHandle = 0;
		RuntimeExportTarget TargetConfig = RuntimeExportTarget::Release;
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
		PhysicsAPIType CurrentPhysicsAPI = PhysicsAPIType::Jolt;

		std::string ProjectFileName;
		std::filesystem::path ProjectDirectory;

		ProjectAudioSettings Audio;
		ProjectPhysicsSettings Physics;
		ProjectSceneRendererSettings SceneRenderer;
		ProjectRuntimeExportSettings RuntimeExport;
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

		// Absolute path to the FMOD Studio project file, or an empty path when the project has no
		// authored audio. Callers must check emptiness rather than assuming a .fspro exists.
		std::filesystem::path GetStudioProjectPath() const
		{
			if (m_Config.Audio.StudioProjectPath.empty())
				return {};

			if (m_Config.Audio.StudioProjectPath.is_absolute())
				return m_Config.Audio.StudioProjectPath;

			return GetAssetDirectory() / m_Config.Audio.StudioProjectPath;
		}

		// Native desktop builds select their host OS profile; runtime exports flatten its settings.
		const AudioDesktopProfile& GetAudioDesktopProfile() const
		{
#ifdef LUX_PLATFORM_WINDOWS
			return m_Config.Audio.Windows;
#else
			return m_Config.Audio.Linux;
#endif
		}
		const AudioPerformanceSettings& GetAudioPerformance() const
		{
			const auto& profile = GetAudioDesktopProfile();
			return profile.Enabled ? profile.Performance : m_Config.Audio.Performance;
		}
		const std::string& GetStudioPlatform() const
		{
			const auto& profile = GetAudioDesktopProfile();
			return profile.Enabled ? profile.StudioPlatform : m_Config.Audio.StudioPlatform;
		}

		// Absolute bank directory, relative to the .fspro for authoring or Assets for runtime manifests.
		std::filesystem::path GetStudioBankDirectory() const
		{
			if (!m_Config.Audio.RuntimeBanks.Directory.empty())
				return GetAssetDirectory() / m_Config.Audio.RuntimeBanks.Directory;

			const std::filesystem::path studioProject = GetStudioProjectPath();
			if (studioProject.empty())
				return {};

			const auto& profile = GetAudioDesktopProfile();
			return studioProject.parent_path() / (profile.Enabled ? profile.BankOutputPath : m_Config.Audio.StudioBankOutputPath);
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
		static Ref<Project> LoadRuntime(const std::filesystem::path& path, Ref<AssetPack> assetPack);
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
