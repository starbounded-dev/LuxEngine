#include "lpch.h"
#include "TieringSerializer.h"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <sstream>

namespace Lux
{
	namespace
	{
		void CreateDirectoriesIfNeeded(const std::filesystem::path& path)
		{
			const std::filesystem::path directory = path.parent_path();
			if (!directory.empty() && !std::filesystem::exists(directory))
				std::filesystem::create_directories(directory);
		}
	}

	bool TieringSerializer::Serialize(const Tiering::TieringSettings& tieringSettings, const std::filesystem::path& filepath)
	{
		using namespace Tiering::Renderer;

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "TieringSettings" << YAML::Value;
		{
			out << YAML::BeginMap;
			out << YAML::Key << "RendererScale" << YAML::Value << tieringSettings.RendererTS.RendererScale;
			out << YAML::Key << "Windowed" << YAML::Value << tieringSettings.RendererTS.Windowed;
			out << YAML::Key << "VSync" << YAML::Value << tieringSettings.RendererTS.VSync;
			out << YAML::Key << "EnableShadows" << YAML::Value << tieringSettings.RendererTS.EnableShadows;
			out << YAML::Key << "ShadowQuality" << YAML::Value << Utils::ShadowQualitySettingToString(tieringSettings.RendererTS.ShadowQuality);
			out << YAML::Key << "ShadowResolution" << YAML::Value << Utils::ShadowResolutionSettingToString(tieringSettings.RendererTS.ShadowResolution);
			out << YAML::Key << "EnableAO" << YAML::Value << tieringSettings.RendererTS.EnableAO;
			out << YAML::Key << "AmbientOcclusionQuality" << YAML::Value << Utils::AmbientOcclusionQualitySettingToString(tieringSettings.RendererTS.AOQuality);
			out << YAML::Key << "SSRQuality" << YAML::Value << Utils::SSRQualitySettingToString(tieringSettings.RendererTS.SSRQuality);
			out << YAML::Key << "EnableBloom" << YAML::Value << tieringSettings.RendererTS.EnableBloom;
			out << YAML::EndMap;
		}
		out << YAML::EndMap;

		CreateDirectoriesIfNeeded(filepath);
		std::ofstream fout(filepath);
		if (!fout.is_open())
			return false;

		fout << out.c_str();
		return true;
	}

	bool TieringSerializer::Deserialize(Tiering::TieringSettings& outTieringSettings, const std::filesystem::path& filepath)
	{
		using namespace Tiering::Renderer;

		std::ifstream stream(filepath);
		if (!stream.good())
			return false;

		std::stringstream strStream;
		strStream << stream.rdbuf();

		YAML::Node data = YAML::Load(strStream.str());
		YAML::Node tieringSettings = data["TieringSettings"];
		if (!tieringSettings)
			return false;

		if (tieringSettings["RendererScale"])
			outTieringSettings.RendererTS.RendererScale = tieringSettings["RendererScale"].as<float>();
		if (tieringSettings["Windowed"])
			outTieringSettings.RendererTS.Windowed = tieringSettings["Windowed"].as<bool>();
		if (tieringSettings["VSync"])
			outTieringSettings.RendererTS.VSync = tieringSettings["VSync"].as<bool>();
		if (tieringSettings["EnableShadows"])
			outTieringSettings.RendererTS.EnableShadows = tieringSettings["EnableShadows"].as<bool>();
		if (tieringSettings["ShadowQuality"])
			outTieringSettings.RendererTS.ShadowQuality = Utils::ShadowQualitySettingFromString(tieringSettings["ShadowQuality"].as<std::string>());
		if (tieringSettings["ShadowResolution"])
			outTieringSettings.RendererTS.ShadowResolution = Utils::ShadowResolutionSettingFromString(tieringSettings["ShadowResolution"].as<std::string>());
		if (tieringSettings["EnableAO"])
			outTieringSettings.RendererTS.EnableAO = tieringSettings["EnableAO"].as<bool>();

		if (tieringSettings["AmbientOcclusionQuality"])
		{
			outTieringSettings.RendererTS.AOQuality = Utils::AmbientOcclusionQualitySettingFromString(tieringSettings["AmbientOcclusionQuality"].as<std::string>());
		}
		else if (tieringSettings["AmbientOcclusion"])
		{
			const bool enableAO = tieringSettings["AmbientOcclusion"].as<bool>();
			outTieringSettings.RendererTS.AOQuality = enableAO ? AmbientOcclusionQualitySetting::High : AmbientOcclusionQualitySetting::None;
		}

		if (tieringSettings["SSRQuality"])
			outTieringSettings.RendererTS.SSRQuality = Utils::SSRQualitySettingFromString(tieringSettings["SSRQuality"].as<std::string>());
		if (tieringSettings["EnableBloom"])
			outTieringSettings.RendererTS.EnableBloom = tieringSettings["EnableBloom"].as<bool>();

		return true;
	}
}
