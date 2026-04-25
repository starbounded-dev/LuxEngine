#include "lpch.h"
#include "UserPreferences.h"

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

	UserPreferencesSerializer::UserPreferencesSerializer(const Ref<UserPreferences>& preferences)
		: m_Preferences(preferences)
	{
	}

	bool UserPreferencesSerializer::Serialize(const std::filesystem::path& filepath)
	{
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "UserPrefs" << YAML::Value;
		{
			out << YAML::BeginMap;
			out << YAML::Key << "ShowWelcomeScreen" << YAML::Value << m_Preferences->ShowWelcomeScreen;

			if (!m_Preferences->StartupProject.empty())
				out << YAML::Key << "StartupProject" << YAML::Value << m_Preferences->StartupProject;

			out << YAML::Key << "RecentProjects" << YAML::Value << YAML::BeginSeq;
			for (const auto& [lastOpened, project] : m_Preferences->RecentProjects)
			{
				out << YAML::BeginMap;
				out << YAML::Key << "Name" << YAML::Value << project.Name;
				out << YAML::Key << "ProjectPath" << YAML::Value << project.FilePath;
				out << YAML::Key << "LastOpened" << YAML::Value << (int64_t)lastOpened;
				out << YAML::EndMap;
			}
			out << YAML::EndSeq;
			out << YAML::EndMap;
		}
		out << YAML::EndMap;

		CreateDirectoriesIfNeeded(filepath);
		std::ofstream fout(filepath);
		if (!fout.is_open())
			return false;

		fout << out.c_str();
		m_Preferences->FilePath = filepath;
		return true;
	}

	bool UserPreferencesSerializer::Deserialize(const std::filesystem::path& filepath)
	{
		std::ifstream stream(filepath);
		if (!stream.good())
			return false;

		std::stringstream strStream;
		strStream << stream.rdbuf();

		YAML::Node data = YAML::Load(strStream.str());
		YAML::Node rootNode = data["UserPrefs"];
		if (!rootNode)
			return false;

		m_Preferences->ShowWelcomeScreen = rootNode["ShowWelcomeScreen"].as<bool>(true);
		m_Preferences->StartupProject = rootNode["StartupProject"] ? rootNode["StartupProject"].as<std::string>() : std::string{};
		m_Preferences->RecentProjects.clear();

		YAML::Node recentProjects = rootNode["RecentProjects"];
		if (recentProjects)
		{
			for (auto recentProject : recentProjects)
			{
				RecentProject entry;
				entry.Name = recentProject["Name"].as<std::string>("");
				entry.FilePath = recentProject["ProjectPath"].as<std::string>("");
				entry.LastOpened = recentProject["LastOpened"] ? (time_t)recentProject["LastOpened"].as<int64_t>() : time(nullptr);

				while (m_Preferences->RecentProjects.contains(entry.LastOpened))
					entry.LastOpened--;

				m_Preferences->RecentProjects[entry.LastOpened] = entry;
			}
		}

		m_Preferences->FilePath = filepath;
		return true;
	}
}
