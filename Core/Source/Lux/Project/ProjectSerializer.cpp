#include "lpch.h"
#include "ProjectSerializer.h"

#include <cctype>
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace Lux
{
	namespace
	{
		std::filesystem::path NormalizeRegistryPath(const std::filesystem::path& projectDirectory, const std::filesystem::path& assetDirectory, const std::filesystem::path& rawPath)
		{
			if (rawPath.empty())
				return (assetDirectory / "AssetRegistry.ser").lexically_normal();

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
			out << YAML::Key << "ScriptModulePath" << YAML::Value << config.ScriptModulePath.generic_string();
			out << YAML::Key << "StartScene" << YAML::Value << config.StartScene;
			out << YAML::EndMap;
		}
		out << YAML::EndMap;

		std::ofstream fout(filepath);
		if (!fout.is_open())
			return false;

		fout << out.c_str();
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

		if (projectNode["AssetRegistry"])
			config.AssetRegistryPath = NormalizeRegistryPath(filepath.parent_path(), config.AssetDirectory, projectNode["AssetRegistry"].as<std::string>());
		else if (projectNode["AssetRegistryPath"])
			config.AssetRegistryPath = NormalizeRegistryPath(filepath.parent_path(), config.AssetDirectory, projectNode["AssetRegistryPath"].as<std::string>());
		else
			config.AssetRegistryPath = NormalizeRegistryPath(filepath.parent_path(), config.AssetDirectory, {});

		config.ScriptModulePath = projectNode["ScriptModulePath"].as<std::string>();
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

		return true;
	}
}
