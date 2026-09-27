// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "TieringSerializer.h"

#include "Lux/Serialization/Yaml.h"

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

		Yaml::Writer out;
		out << Yaml::BeginMap;
		out << Yaml::Key << "TieringSettings" << Yaml::Value;
		{
			out << Yaml::BeginMap;
			out << Yaml::Key << "RendererScale" << Yaml::Value << tieringSettings.RendererTS.RendererScale;
			out << Yaml::Key << "Windowed" << Yaml::Value << tieringSettings.RendererTS.Windowed;
			out << Yaml::Key << "VSync" << Yaml::Value << tieringSettings.RendererTS.VSync;
			out << Yaml::Key << "EnableShadows" << Yaml::Value << tieringSettings.RendererTS.EnableShadows;
			out << Yaml::Key << "ShadowQuality" << Yaml::Value << Utils::ShadowQualitySettingToString(tieringSettings.RendererTS.ShadowQuality);
			out << Yaml::Key << "ShadowResolution" << Yaml::Value << Utils::ShadowResolutionSettingToString(tieringSettings.RendererTS.ShadowResolution);
			out << Yaml::Key << "EnableAO" << Yaml::Value << tieringSettings.RendererTS.EnableAO;
			out << Yaml::Key << "AmbientOcclusionQuality" << Yaml::Value << Utils::AmbientOcclusionQualitySettingToString(tieringSettings.RendererTS.AOQuality);
			out << Yaml::Key << "SSRQuality" << Yaml::Value << Utils::SSRQualitySettingToString(tieringSettings.RendererTS.SSRQuality);
			out << Yaml::Key << "EnableBloom" << Yaml::Value << tieringSettings.RendererTS.EnableBloom;
			out << Yaml::EndMap;
		}
		out << Yaml::EndMap;

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

		Yaml::Node data = Yaml::Load(strStream.str());
		Yaml::Node tieringSettings = data["TieringSettings"];
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
