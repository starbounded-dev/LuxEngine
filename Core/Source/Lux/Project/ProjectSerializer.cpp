#include "lpch.h"
#include "ProjectSerializer.h"

#include "ProjectRuntimeFormat.h"

#include "Lux/Core/Log.h"
#include "Lux/Serialization/FileStream.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <yaml-cpp/yaml.h>

namespace YAML
{
	template<>
	struct convert<glm::vec3>
	{
		static Node encode(const glm::vec3& rhs)
		{
			Node node;
			node.push_back(rhs.x);
			node.push_back(rhs.y);
			node.push_back(rhs.z);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec3& rhs)
		{
			if (!node.IsSequence() || node.size() != 3)
				return false;

			rhs.x = node[0].as<float>();
			rhs.y = node[1].as<float>();
			rhs.z = node[2].as<float>();
			return true;
		}
	};
}

namespace Lux
{
	namespace
	{
		YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& value)
		{
			out << YAML::Flow;
			out << YAML::BeginSeq << value.x << value.y << value.z << YAML::EndSeq;
			return out;
		}

		std::filesystem::path NormalizeRegistryPath(const std::filesystem::path& projectDirectory, const std::filesystem::path& assetDirectory, const std::filesystem::path& rawPath)
		{
			if (rawPath.empty())
				return (assetDirectory / "AssetRegistry.lzr").lexically_normal();

			if (rawPath.is_absolute())
				return std::filesystem::relative(rawPath, projectDirectory).lexically_normal();

			const auto normalizedAssetDirectory = assetDirectory.lexically_normal();
			const auto normalizedRawPath = rawPath.lexically_normal();

			std::string rawString = normalizedRawPath.generic_string();
			std::string assetDirString = normalizedAssetDirectory.generic_string();
			if (!assetDirString.empty() && rawString.rfind(assetDirString, 0) == 0)
				return normalizedRawPath;

			return (normalizedAssetDirectory / normalizedRawPath).lexically_normal();
		}

		bool IsNumericString(const std::string& value)
		{
			if (value.empty())
				return false;

			for (char ch : value)
			{
				if (!std::isdigit((unsigned char)ch))
					return false;
			}

			return true;
		}

		void CreateDirectoriesIfNeeded(const std::filesystem::path& path)
		{
			const auto directory = path.parent_path();
			if (!directory.empty() && !std::filesystem::exists(directory))
				std::filesystem::create_directories(directory);
		}

		const char* PhysicsCaptureMethodToString(PhysicsCaptureMethod method)
		{
			switch (method)
			{
				case PhysicsCaptureMethod::LiveDebug: return "LiveDebug";
				case PhysicsCaptureMethod::CaptureToFile: return "CaptureToFile";
			}

			return "LiveDebug";
		}

		PhysicsCaptureMethod PhysicsCaptureMethodFromString(std::string_view value)
		{
			if (value == "LiveDebug")
				return PhysicsCaptureMethod::LiveDebug;
			if (value == "CaptureToFile")
				return PhysicsCaptureMethod::CaptureToFile;
			return PhysicsCaptureMethod::LiveDebug;
		}

		const char* RenderScaleModeToString(uint32_t mode)
		{
			switch (mode)
			{
				case 1: return "Scale75";
				case 2: return "Scale50";
				case 3: return "Dynamic";
				case 0:
				default:
					return "Scale100";
			}
		}

		uint32_t RenderScaleModeFromString(std::string_view value)
		{
			if (value == "75%" || value == "Scale75" || value == "0.75" || value == "1")
				return 1;
			if (value == "50%" || value == "Scale50" || value == "0.50" || value == "0.5" || value == "2")
				return 2;
			if (value == "Dynamic" || value == "3")
				return 3;
			return 0;
		}

		void SerializeSceneRendererSettings(YAML::Emitter& out, const ProjectSceneRendererSettings& settings)
		{
			out << YAML::Key << "SceneRenderer" << YAML::Value;
			out << YAML::BeginMap;

			out << YAML::Key << "Rendering" << YAML::Value;
			out << YAML::BeginMap;
			out << YAML::Key << "FrustumCulling" << YAML::Value << settings.EnableFrustumCulling;
			out << YAML::Key << "OcclusionCulling" << YAML::Value << settings.EnableOcclusionCulling;
			out << YAML::Key << "GPUDrivenRendering" << YAML::Value << settings.EnableGPUDrivenRendering;
			out << YAML::Key << "GTAO" << YAML::Value << settings.EnableGTAO;
			out << YAML::Key << "GTAOBentNormals" << YAML::Value << settings.GTAOBentNormals;
			out << YAML::Key << "GTAODenoisePasses" << YAML::Value << settings.GTAODenoisePasses;
			out << YAML::Key << "AOShadowTolerance" << YAML::Value << settings.AOShadowTolerance;
			out << YAML::Key << "SSR" << YAML::Value << settings.EnableSSR;
			out << YAML::Key << "JumpFloodOutline" << YAML::Value << settings.EnableJumpFlood;
			out << YAML::Key << "RenderScaleMode" << YAML::Value << RenderScaleModeToString(settings.RenderScaleMode);
			out << YAML::Key << "DynamicResolutionMinScale" << YAML::Value << settings.DynamicResolutionMinScale;
			out << YAML::Key << "DynamicResolutionMaxScale" << YAML::Value << settings.DynamicResolutionMaxScale;
			out << YAML::Key << "DynamicResolutionTargetGPUTime" << YAML::Value << settings.DynamicResolutionTargetGPUTime;
			out << YAML::EndMap;

			out << YAML::Key << "Shadows" << YAML::Value;
			out << YAML::BeginMap;
			out << YAML::Key << "SoftShadows" << YAML::Value << settings.SoftShadows;
			out << YAML::Key << "ShadowCulling" << YAML::Value << settings.EnableShadowCulling;
			out << YAML::Key << "MaxDistance" << YAML::Value << settings.MaxShadowDistance;
			out << YAML::Key << "DistanceFade" << YAML::Value << settings.ShadowFade;
			out << YAML::Key << "SplitLambda" << YAML::Value << settings.ShadowCascadeSplitLambda;
			out << YAML::Key << "NearOffset" << YAML::Value << settings.ShadowCascadeNearPlaneOffset;
			out << YAML::Key << "FarOffset" << YAML::Value << settings.ShadowCascadeFarPlaneOffset;
			out << YAML::Key << "CascadeFade" << YAML::Value << settings.ShadowCascadeTransitionFade;
			out << YAML::EndMap;

			out << YAML::Key << "PostFX" << YAML::Value;
			out << YAML::BeginMap;
			out << YAML::Key << "Bloom" << YAML::Value;
			out << YAML::BeginMap;
			out << YAML::Key << "Enabled" << YAML::Value << settings.BloomEnabled;
			out << YAML::Key << "Threshold" << YAML::Value << settings.BloomThreshold;
			out << YAML::Key << "Knee" << YAML::Value << settings.BloomKnee;
			out << YAML::Key << "UpsampleScale" << YAML::Value << settings.BloomUpsampleScale;
			out << YAML::Key << "Intensity" << YAML::Value << settings.BloomIntensity;
			out << YAML::Key << "DirtIntensity" << YAML::Value << settings.BloomDirtIntensity;
			out << YAML::EndMap;

			out << YAML::Key << "DOF" << YAML::Value;
			out << YAML::BeginMap;
			out << YAML::Key << "Enabled" << YAML::Value << settings.DOFEnabled;
			out << YAML::Key << "FocusDistance" << YAML::Value << settings.DOFFocusDistance;
			out << YAML::Key << "BlurSize" << YAML::Value << settings.DOFBlurSize;
			out << YAML::EndMap;

			out << YAML::Key << "SSR" << YAML::Value;
			out << YAML::BeginMap;
			out << YAML::Key << "HalfRes" << YAML::Value << settings.SSRHalfRes;
			out << YAML::Key << "MaxSteps" << YAML::Value << settings.SSRMaxSteps;
			out << YAML::Key << "Brightness" << YAML::Value << settings.SSRBrightness;
			out << YAML::Key << "DepthTolerance" << YAML::Value << settings.SSRDepthTolerance;
			out << YAML::EndMap;
			out << YAML::EndMap;

			out << YAML::EndMap;
		}

		void DeserializeSceneRendererSettings(const YAML::Node& node, ProjectSceneRendererSettings& settings)
		{
			if (!node)
				return;

			if (auto rendering = node["Rendering"])
			{
				settings.EnableFrustumCulling = rendering["FrustumCulling"].as<bool>(settings.EnableFrustumCulling);
				settings.EnableOcclusionCulling = rendering["OcclusionCulling"].as<bool>(settings.EnableOcclusionCulling);
				settings.EnableGPUDrivenRendering = rendering["GPUDrivenRendering"].as<bool>(settings.EnableGPUDrivenRendering);
				settings.EnableGTAO = rendering["GTAO"].as<bool>(settings.EnableGTAO);
				settings.GTAOBentNormals = rendering["GTAOBentNormals"].as<bool>(settings.GTAOBentNormals);
				settings.GTAODenoisePasses = rendering["GTAODenoisePasses"].as<int>(settings.GTAODenoisePasses);
				settings.AOShadowTolerance = rendering["AOShadowTolerance"].as<float>(settings.AOShadowTolerance);
				settings.EnableSSR = rendering["SSR"].as<bool>(settings.EnableSSR);
				settings.EnableJumpFlood = rendering["JumpFloodOutline"].as<bool>(settings.EnableJumpFlood);
				settings.RenderScaleMode = RenderScaleModeFromString(rendering["RenderScaleMode"].as<std::string>(RenderScaleModeToString(settings.RenderScaleMode)));
				settings.DynamicResolutionMinScale = rendering["DynamicResolutionMinScale"].as<float>(settings.DynamicResolutionMinScale);
				settings.DynamicResolutionMaxScale = rendering["DynamicResolutionMaxScale"].as<float>(settings.DynamicResolutionMaxScale);
				settings.DynamicResolutionTargetGPUTime = rendering["DynamicResolutionTargetGPUTime"].as<float>(settings.DynamicResolutionTargetGPUTime);
			}

			if (auto shadows = node["Shadows"])
			{
				settings.SoftShadows = shadows["SoftShadows"].as<bool>(settings.SoftShadows);
				settings.EnableShadowCulling = shadows["ShadowCulling"].as<bool>(settings.EnableShadowCulling);
				settings.MaxShadowDistance = shadows["MaxDistance"].as<float>(settings.MaxShadowDistance);
				settings.ShadowFade = shadows["DistanceFade"].as<float>(settings.ShadowFade);
				settings.ShadowCascadeSplitLambda = shadows["SplitLambda"].as<float>(settings.ShadowCascadeSplitLambda);
				settings.ShadowCascadeNearPlaneOffset = shadows["NearOffset"].as<float>(settings.ShadowCascadeNearPlaneOffset);
				settings.ShadowCascadeFarPlaneOffset = shadows["FarOffset"].as<float>(settings.ShadowCascadeFarPlaneOffset);
				settings.ShadowCascadeTransitionFade = shadows["CascadeFade"].as<float>(settings.ShadowCascadeTransitionFade);
			}

			if (auto postFX = node["PostFX"])
			{
				if (auto bloom = postFX["Bloom"])
				{
					settings.BloomEnabled = bloom["Enabled"].as<bool>(settings.BloomEnabled);
					settings.BloomThreshold = bloom["Threshold"].as<float>(settings.BloomThreshold);
					settings.BloomKnee = bloom["Knee"].as<float>(settings.BloomKnee);
					settings.BloomUpsampleScale = bloom["UpsampleScale"].as<float>(settings.BloomUpsampleScale);
					settings.BloomIntensity = bloom["Intensity"].as<float>(settings.BloomIntensity);
					settings.BloomDirtIntensity = bloom["DirtIntensity"].as<float>(settings.BloomDirtIntensity);
				}

				if (auto dof = postFX["DOF"])
				{
					settings.DOFEnabled = dof["Enabled"].as<bool>(settings.DOFEnabled);
					settings.DOFFocusDistance = dof["FocusDistance"].as<float>(settings.DOFFocusDistance);
					settings.DOFBlurSize = dof["BlurSize"].as<float>(settings.DOFBlurSize);
				}

				if (auto ssr = postFX["SSR"])
				{
					settings.SSRHalfRes = ssr["HalfRes"].as<bool>(settings.SSRHalfRes);
					settings.SSRMaxSteps = ssr["MaxSteps"].as<int>(settings.SSRMaxSteps);
					settings.SSRBrightness = ssr["Brightness"].as<float>(settings.SSRBrightness);
					settings.SSRDepthTolerance = ssr["DepthTolerance"].as<float>(settings.SSRDepthTolerance);
				}
			}
		}

		void WriteSceneRendererRuntimeSettings(FileStreamWriter& serializer, const ProjectSceneRendererSettings& settings)
		{
			serializer.WriteRaw(settings.EnableFrustumCulling);
			serializer.WriteRaw(settings.EnableOcclusionCulling);
			serializer.WriteRaw(settings.EnableGPUDrivenRendering);
			serializer.WriteRaw(settings.EnableGTAO);
			serializer.WriteRaw(settings.GTAOBentNormals);
			serializer.WriteRaw(settings.GTAODenoisePasses);
			serializer.WriteRaw(settings.AOShadowTolerance);
			serializer.WriteRaw(settings.EnableSSR);
			serializer.WriteRaw(settings.EnableJumpFlood);
			serializer.WriteRaw(settings.RenderScaleMode);
			serializer.WriteRaw(settings.DynamicResolutionMinScale);
			serializer.WriteRaw(settings.DynamicResolutionMaxScale);
			serializer.WriteRaw(settings.DynamicResolutionTargetGPUTime);

			serializer.WriteRaw(settings.SoftShadows);
			serializer.WriteRaw(settings.EnableShadowCulling);
			serializer.WriteRaw(settings.MaxShadowDistance);
			serializer.WriteRaw(settings.ShadowFade);
			serializer.WriteRaw(settings.ShadowCascadeSplitLambda);
			serializer.WriteRaw(settings.ShadowCascadeNearPlaneOffset);
			serializer.WriteRaw(settings.ShadowCascadeFarPlaneOffset);
			serializer.WriteRaw(settings.ShadowCascadeTransitionFade);

			serializer.WriteRaw(settings.BloomEnabled);
			serializer.WriteRaw(settings.BloomThreshold);
			serializer.WriteRaw(settings.BloomKnee);
			serializer.WriteRaw(settings.BloomUpsampleScale);
			serializer.WriteRaw(settings.BloomIntensity);
			serializer.WriteRaw(settings.BloomDirtIntensity);

			serializer.WriteRaw(settings.DOFEnabled);
			serializer.WriteRaw(settings.DOFFocusDistance);
			serializer.WriteRaw(settings.DOFBlurSize);

			serializer.WriteRaw(settings.SSRHalfRes);
			serializer.WriteRaw(settings.SSRMaxSteps);
			serializer.WriteRaw(settings.SSRBrightness);
			serializer.WriteRaw(settings.SSRDepthTolerance);
		}

		void ReadSceneRendererRuntimeSettings(FileStreamReader& stream, ProjectSceneRendererSettings& settings, uint32_t version)
		{
			stream.ReadRaw(settings.EnableFrustumCulling);
			if (version >= 3)
				stream.ReadRaw(settings.EnableOcclusionCulling);
			stream.ReadRaw(settings.EnableGPUDrivenRendering);
			stream.ReadRaw(settings.EnableGTAO);
			stream.ReadRaw(settings.GTAOBentNormals);
			stream.ReadRaw(settings.GTAODenoisePasses);
			stream.ReadRaw(settings.AOShadowTolerance);
			stream.ReadRaw(settings.EnableSSR);
			stream.ReadRaw(settings.EnableJumpFlood);
			if (version >= 4)
			{
				stream.ReadRaw(settings.RenderScaleMode);
				stream.ReadRaw(settings.DynamicResolutionMinScale);
				stream.ReadRaw(settings.DynamicResolutionMaxScale);
				stream.ReadRaw(settings.DynamicResolutionTargetGPUTime);
			}

			stream.ReadRaw(settings.SoftShadows);
			if (version >= 3)
				stream.ReadRaw(settings.EnableShadowCulling);
			stream.ReadRaw(settings.MaxShadowDistance);
			stream.ReadRaw(settings.ShadowFade);
			stream.ReadRaw(settings.ShadowCascadeSplitLambda);
			stream.ReadRaw(settings.ShadowCascadeNearPlaneOffset);
			stream.ReadRaw(settings.ShadowCascadeFarPlaneOffset);
			stream.ReadRaw(settings.ShadowCascadeTransitionFade);

			stream.ReadRaw(settings.BloomEnabled);
			stream.ReadRaw(settings.BloomThreshold);
			stream.ReadRaw(settings.BloomKnee);
			stream.ReadRaw(settings.BloomUpsampleScale);
			stream.ReadRaw(settings.BloomIntensity);
			stream.ReadRaw(settings.BloomDirtIntensity);

			stream.ReadRaw(settings.DOFEnabled);
			stream.ReadRaw(settings.DOFFocusDistance);
			stream.ReadRaw(settings.DOFBlurSize);

			stream.ReadRaw(settings.SSRHalfRes);
			stream.ReadRaw(settings.SSRMaxSteps);
			stream.ReadRaw(settings.SSRBrightness);
			stream.ReadRaw(settings.SSRDepthTolerance);
		}
	}

	ProjectSerializer::ProjectSerializer(Ref<Project> project)
		: m_Project(project)
	{
	}

	bool ProjectSerializer::Serialize(const std::filesystem::path& filepath)
	{
		const auto& config = m_Project->GetConfig();

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Project" << YAML::Value;
		{
			out << YAML::BeginMap;
			out << YAML::Key << "Name" << YAML::Value << config.Name;
			out << YAML::Key << "AssetDirectory" << YAML::Value << config.AssetDirectory.generic_string();
			out << YAML::Key << "AssetRegistry" << YAML::Value << config.AssetRegistryPath.generic_string();
			out << YAML::Key << "AudioCommandsRegistryPath" << YAML::Value << config.AudioCommandsRegistryPath.generic_string();
			out << YAML::Key << "MeshPath" << YAML::Value << config.MeshPath.generic_string();
			out << YAML::Key << "MeshSourcePath" << YAML::Value << config.MeshSourcePath.generic_string();
			out << YAML::Key << "AnimationPath" << YAML::Value << config.AnimationPath.generic_string();
			out << YAML::Key << "ScriptModulePath" << YAML::Value << config.ScriptModulePath.generic_string();
			out << YAML::Key << "DefaultNamespace" << YAML::Value << config.DefaultNamespace;
			out << YAML::Key << "StartScene" << YAML::Value << config.StartScene;
			out << YAML::Key << "AutomaticallyReloadAssembly" << YAML::Value << config.AutomaticallyReloadAssembly;
			out << YAML::Key << "AutoSave" << YAML::Value << config.EnableAutoSave;
			out << YAML::Key << "AutoSaveInterval" << YAML::Value << config.AutoSaveIntervalSeconds;
			out << YAML::Key << "RenderingTechnique" << YAML::Value << RenderingTechniqueToString(config.RendererTechnique);
			SerializeSceneRendererSettings(out, config.SceneRenderer);

			out << YAML::Key << "Audio" << YAML::Value;
			{
				out << YAML::BeginMap;
				out << YAML::Key << "FileStreamingDurationThreshold" << YAML::Value << config.Audio.FileStreamingDurationThreshold;
				out << YAML::EndMap;
			}

			out << YAML::Key << "Physics" << YAML::Value;
			{
				out << YAML::BeginMap;
				out << YAML::Key << "FixedTimestep" << YAML::Value << config.Physics.FixedTimestep;
				out << YAML::Key << "Gravity" << YAML::Value << config.Physics.Gravity;
				out << YAML::Key << "SolverPositionIterations" << YAML::Value << config.Physics.PositionSolverIterations;
				out << YAML::Key << "SolverVelocityIterations" << YAML::Value << config.Physics.VelocitySolverIterations;
				out << YAML::Key << "MaxBodies" << YAML::Value << config.Physics.MaxBodies;
				out << YAML::Key << "CaptureOnPlay" << YAML::Value << config.Physics.CaptureOnPlay;
				out << YAML::Key << "CaptureMethod" << YAML::Value << PhysicsCaptureMethodToString(config.Physics.CaptureMethod);

				if (!config.Physics.Layers.empty())
				{
					out << YAML::Key << "Layers" << YAML::Value << YAML::BeginSeq;
					for (const auto& layer : config.Physics.Layers)
					{
						out << YAML::BeginMap;
						out << YAML::Key << "Name" << YAML::Value << layer.Name;
						out << YAML::Key << "CollidesWithSelf" << YAML::Value << layer.CollidesWithSelf;
						out << YAML::Key << "CollidesWith" << YAML::Value << YAML::BeginSeq;
						for (const auto& collidingLayer : layer.CollidesWith)
						{
							out << YAML::BeginMap;
							out << YAML::Key << "Name" << YAML::Value << collidingLayer;
							out << YAML::EndMap;
						}
						out << YAML::EndSeq;
						out << YAML::EndMap;
					}
					out << YAML::EndSeq;
				}

				out << YAML::EndMap;
			}

			out << YAML::Key << "Log" << YAML::Value;
			{
				out << YAML::BeginMap;
				for (auto& [name, details] : Log::EnabledTags())
				{
					if (name.empty())
						continue;

					out << YAML::Key << name << YAML::Value;
					out << YAML::BeginMap;
					out << YAML::Key << "Enabled" << YAML::Value << details.Enabled;
					out << YAML::Key << "LevelFilter" << YAML::Value << Log::LevelToString(details.LevelFilter);
					out << YAML::EndMap;
				}
				out << YAML::EndMap;
			}

			out << YAML::EndMap;
		}
		out << YAML::EndMap;

		CreateDirectoriesIfNeeded(filepath);
		std::ofstream fout(filepath);
		if (!fout.is_open())
			return false;

		fout << out.c_str();
		m_Project->OnSerialized();
		return true;
	}

	bool ProjectSerializer::SerializeRuntime(const std::filesystem::path& filepath)
	{
		ProjectInfo projectInfo;

		{
			const auto& config = m_Project->GetConfig();
			AssetHandle startScene = config.StartSceneHandle;
			if (!startScene && !config.StartScene.empty() && Project::GetEditorAssetManager())
				startScene = Project::GetEditorAssetManager()->GetAssetHandleFromFilePath(config.StartScene);

			if (!startScene)
			{
				LUX_CORE_ERROR("Error building runtime project - no start scene could be found! (StartScene: {})", config.StartScene);
				return false;
			}

			projectInfo.StartScene = startScene;
			projectInfo.AudioInfo.FileStreamingDurationThreshold = config.Audio.FileStreamingDurationThreshold;
		}

		CreateDirectoriesIfNeeded(filepath);
		FileStreamWriter serializer(filepath);
		if (!serializer.IsStreamGood())
			return false;

		serializer.WriteRaw<ProjectInfo>(projectInfo);

		const auto& physics = m_Project->GetConfig().Physics;
		serializer.WriteRaw<float>(physics.FixedTimestep);
		serializer.WriteRaw<glm::vec3>(physics.Gravity);
		serializer.WriteRaw<uint32_t>(physics.PositionSolverIterations);
		serializer.WriteRaw<uint32_t>(physics.VelocitySolverIterations);
		serializer.WriteRaw<uint32_t>(physics.MaxBodies);
		serializer.WriteRaw<bool>(physics.CaptureOnPlay);
		serializer.WriteRaw<uint8_t>((uint8_t)physics.CaptureMethod);

		serializer.WriteRaw<uint32_t>((uint32_t)physics.Layers.size());
		for (const auto& layer : physics.Layers)
			serializer.WriteString(layer.Name);

		for (const auto& layer : physics.Layers)
		{
			serializer.WriteRaw<bool>(layer.CollidesWithSelf);
			serializer.WriteArray(layer.CollidesWith);
		}

		uint32_t tagCount = 0;
		for (auto& [name, details] : Log::EnabledTags())
		{
			if (!name.empty())
				tagCount++;
		}

		serializer.WriteRaw<uint32_t>(tagCount);
		for (auto& [name, details] : Log::EnabledTags())
		{
			if (name.empty())
				continue;

			serializer.WriteString(name);
			serializer.WriteRaw<bool>(details.Enabled);
			serializer.WriteRaw<uint8_t>((uint8_t)details.LevelFilter);
		}

		WriteSceneRendererRuntimeSettings(serializer, m_Project->GetConfig().SceneRenderer);

		return true;
	}

	bool ProjectSerializer::Deserialize(const std::filesystem::path& filepath)
	{
		auto& config = m_Project->GetConfig();

		YAML::Node data;
		try
		{
			data = YAML::LoadFile(filepath.string());
		}
		catch (const YAML::ParserException& e)
		{
			LUX_CORE_ERROR("Failed to load project file '{0}'\n     {1}", filepath.string(), e.what());
			return false;
		}

		auto projectNode = data["Project"];
		if (!projectNode)
			return false;

		config.Name = projectNode["Name"].as<std::string>("Untitled");
		config.AssetDirectory = projectNode["AssetDirectory"].as<std::string>("Assets");
		config.ProjectDirectory = filepath.parent_path();
		config.ProjectFileName = filepath.filename().string();

		if (projectNode["AssetRegistry"])
			config.AssetRegistryPath = NormalizeRegistryPath(filepath.parent_path(), config.AssetDirectory, projectNode["AssetRegistry"].as<std::string>());
		else if (projectNode["AssetRegistryPath"])
			config.AssetRegistryPath = NormalizeRegistryPath(filepath.parent_path(), config.AssetDirectory, projectNode["AssetRegistryPath"].as<std::string>());
		else
			config.AssetRegistryPath = NormalizeRegistryPath(filepath.parent_path(), config.AssetDirectory, {});

		config.AudioCommandsRegistryPath = projectNode["AudioCommandsRegistryPath"].as<std::string>(config.AudioCommandsRegistryPath.generic_string());
		config.MeshPath = projectNode["MeshPath"].as<std::string>(config.MeshPath.generic_string());
		config.MeshSourcePath = projectNode["MeshSourcePath"].as<std::string>(config.MeshSourcePath.generic_string());
		config.AnimationPath = projectNode["AnimationPath"].as<std::string>(config.AnimationPath.generic_string());
		config.ScriptModulePath = projectNode["ScriptModulePath"].as<std::string>(config.ScriptModulePath.generic_string());
		config.DefaultNamespace = projectNode["DefaultNamespace"].as<std::string>(config.Name);
		config.AutomaticallyReloadAssembly = projectNode["AutomaticallyReloadAssembly"].as<bool>(true);
		config.EnableAutoSave = projectNode["AutoSave"].as<bool>(false);
		config.AutoSaveIntervalSeconds = projectNode["AutoSaveInterval"].as<int>(300);
		config.RendererTechnique = RenderingTechnique::Forward;
		if (auto renderingTechniqueNode = projectNode["RenderingTechnique"])
		{
			if (renderingTechniqueNode.IsScalar() && !IsNumericString(renderingTechniqueNode.Scalar()))
				config.RendererTechnique = RenderingTechniqueFromString(renderingTechniqueNode.as<std::string>());
			else
				config.RendererTechnique = renderingTechniqueNode.as<int>((int)RenderingTechnique::Forward) == (int)RenderingTechnique::Deferred ? RenderingTechnique::Deferred : RenderingTechnique::Forward;
		}
		DeserializeSceneRendererSettings(projectNode["SceneRenderer"], config.SceneRenderer);
		config.StartScene.clear();
		config.StartSceneHandle = 0;

		if (auto startSceneNode = projectNode["StartScene"])
		{
			std::string rawStartScene = startSceneNode.IsScalar() ? startSceneNode.Scalar() : std::string{};
			if (IsNumericString(rawStartScene))
				config.StartSceneHandle = (uint64_t)std::stoull(rawStartScene);
			else
				config.StartScene = rawStartScene;
		}

		if (auto audioNode = projectNode["Audio"])
			config.Audio.FileStreamingDurationThreshold = audioNode["FileStreamingDurationThreshold"].as<double>(config.Audio.FileStreamingDurationThreshold);

		config.Physics = {};
		if (auto physicsNode = projectNode["Physics"])
		{
			config.Physics.FixedTimestep = physicsNode["FixedTimestep"].as<float>(config.Physics.FixedTimestep);
			config.Physics.Gravity = physicsNode["Gravity"].as<glm::vec3>(config.Physics.Gravity);
			config.Physics.PositionSolverIterations = physicsNode["SolverPositionIterations"].as<uint32_t>(config.Physics.PositionSolverIterations);
			config.Physics.VelocitySolverIterations = physicsNode["SolverVelocityIterations"].as<uint32_t>(config.Physics.VelocitySolverIterations);
			config.Physics.MaxBodies = physicsNode["MaxBodies"].as<uint32_t>(config.Physics.MaxBodies);
			config.Physics.CaptureOnPlay = physicsNode["CaptureOnPlay"].as<bool>(config.Physics.CaptureOnPlay);

			if (physicsNode["CaptureMethod"])
			{
				if (physicsNode["CaptureMethod"].IsScalar() && !IsNumericString(physicsNode["CaptureMethod"].Scalar()))
					config.Physics.CaptureMethod = PhysicsCaptureMethodFromString(physicsNode["CaptureMethod"].as<std::string>());
				else
					config.Physics.CaptureMethod = (PhysicsCaptureMethod)physicsNode["CaptureMethod"].as<int>((int)config.Physics.CaptureMethod);
			}

			YAML::Node physicsLayers = physicsNode["Layers"];
			if (!physicsLayers)
				physicsLayers = physicsNode["PhysicsLayers"];

			if (physicsLayers)
			{
				for (auto layerNode : physicsLayers)
				{
					ProjectPhysicsLayer layer;
					layer.Name = layerNode["Name"].as<std::string>("");
					layer.CollidesWithSelf = layerNode["CollidesWithSelf"].as<bool>(true);

					if (auto collidesWith = layerNode["CollidesWith"])
					{
						for (auto collisionLayer : collidesWith)
							layer.CollidesWith.emplace_back(collisionLayer["Name"].as<std::string>(""));
					}

					config.Physics.Layers.emplace_back(std::move(layer));
				}
			}
		}

		Log::SetDefaultTagSettings();
		if (auto logNode = projectNode["Log"])
		{
			for (auto node : logNode)
			{
				const std::string name = node.first.as<std::string>();
				auto& details = Log::EnabledTags()[name];
				details.Enabled = node.second["Enabled"].as<bool>(details.Enabled);
				details.LevelFilter = Log::LevelFromString(node.second["LevelFilter"].as<std::string>(Log::LevelToString(details.LevelFilter)));
			}
		}

		m_Project->OnDeserialized();
		return true;
	}

	bool ProjectSerializer::DeserializeRuntime(const std::filesystem::path& filepath)
	{
		FileStreamReader stream(filepath);
		if (!stream.IsStreamGood())
			return false;

		ProjectInfo projectInfo;
		stream.ReadRaw<ProjectInfo>(projectInfo);

		ProjectInfo current;
		const bool validHeader = std::memcmp(projectInfo.HeaderData.Header, current.HeaderData.Header, sizeof(current.HeaderData.Header)) == 0;
		if (!validHeader)
		{
			LUX_CORE_ERROR("Project file '{}' has an invalid runtime header", filepath.string());
			return false;
		}

		if (projectInfo.HeaderData.Version == 0 || projectInfo.HeaderData.Version > current.HeaderData.Version)
		{
			LUX_CORE_ERROR("Project version {} is not compatible with current version {}", projectInfo.HeaderData.Version, current.HeaderData.Version);
			return false;
		}

		auto& config = m_Project->GetConfig();
		config.ProjectDirectory = filepath.parent_path();
		config.ProjectFileName = filepath.filename().string();
		config.StartSceneHandle = projectInfo.StartScene;
		config.Audio.FileStreamingDurationThreshold = projectInfo.AudioInfo.FileStreamingDurationThreshold;

		stream.ReadRaw<float>(config.Physics.FixedTimestep);
		stream.ReadRaw<glm::vec3>(config.Physics.Gravity);
		stream.ReadRaw<uint32_t>(config.Physics.PositionSolverIterations);
		stream.ReadRaw<uint32_t>(config.Physics.VelocitySolverIterations);
		stream.ReadRaw<uint32_t>(config.Physics.MaxBodies);
		stream.ReadRaw<bool>(config.Physics.CaptureOnPlay);

		uint8_t captureMethod = 0;
		stream.ReadRaw<uint8_t>(captureMethod);
		config.Physics.CaptureMethod = (PhysicsCaptureMethod)captureMethod;

		uint32_t physicsLayerCount = 0;
		stream.ReadRaw<uint32_t>(physicsLayerCount);
		config.Physics.Layers.clear();
		config.Physics.Layers.resize(physicsLayerCount);

		for (uint32_t i = 0; i < physicsLayerCount; i++)
			stream.ReadString(config.Physics.Layers[i].Name);

		for (uint32_t i = 0; i < physicsLayerCount; i++)
		{
			stream.ReadRaw<bool>(config.Physics.Layers[i].CollidesWithSelf);
			stream.ReadArray(config.Physics.Layers[i].CollidesWith);
		}

		Log::SetDefaultTagSettings();
		uint32_t tagCount = 0;
		stream.ReadRaw<uint32_t>(tagCount);
		for (uint32_t i = 0; i < tagCount; i++)
		{
			std::string name;
			stream.ReadString(name);

			auto& details = Log::EnabledTags()[name];
			stream.ReadRaw(details.Enabled);

			uint8_t levelFilter = 0;
			stream.ReadRaw<uint8_t>(levelFilter);
			details.LevelFilter = (Log::Level)levelFilter;
		}

		if (projectInfo.HeaderData.Version >= 2)
			ReadSceneRendererRuntimeSettings(stream, config.SceneRenderer, projectInfo.HeaderData.Version);

		const std::filesystem::path overridesFile = filepath.parent_path() / "Project.yaml";
		if (std::filesystem::exists(overridesFile))
		{
			std::ifstream overridesStream(overridesFile);
			std::stringstream overridesString;
			overridesString << overridesStream.rdbuf();

			YAML::Node overridesData = YAML::Load(overridesString.str());
			YAML::Node rootNode = overridesData["Project"];
			if (rootNode)
			{
				if (auto logNode = rootNode["Log"])
				{
					for (auto node : logNode)
					{
						const std::string name = node.first.as<std::string>();
						auto& details = Log::EnabledTags()[name];
						details.Enabled = node.second["Enabled"].as<bool>(details.Enabled);
						details.LevelFilter = Log::LevelFromString(node.second["LevelFilter"].as<std::string>(Log::LevelToString(details.LevelFilter)));
					}
				}

				DeserializeSceneRendererSettings(rootNode["SceneRenderer"], config.SceneRenderer);
			}
		}

		m_Project->OnDeserialized();
		return true;
	}
}
