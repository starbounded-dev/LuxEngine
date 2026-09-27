// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "UserPreferences.h"

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

	UserPreferencesSerializer::UserPreferencesSerializer(const Ref<UserPreferences>& preferences)
		: m_Preferences(preferences)
	{
	}

	bool UserPreferencesSerializer::Serialize(const std::filesystem::path& filepath)
	{
		Yaml::Writer out;
		out << Yaml::BeginMap;
		out << Yaml::Key << "UserPrefs" << Yaml::Value;
		{
			out << Yaml::BeginMap;
			out << Yaml::Key << "ShowWelcomeScreen" << Yaml::Value << m_Preferences->ShowWelcomeScreen;

			if (!m_Preferences->StartupProject.empty())
				out << Yaml::Key << "StartupProject" << Yaml::Value << m_Preferences->StartupProject;

			out << Yaml::Key << "RecentProjects" << Yaml::Value << Yaml::BeginSeq;
			for (const auto& [lastOpened, project] : m_Preferences->RecentProjects)
			{
				out << Yaml::BeginMap;
				out << Yaml::Key << "Name" << Yaml::Value << project.Name;
				out << Yaml::Key << "ProjectPath" << Yaml::Value << project.FilePath;
				out << Yaml::Key << "LastOpened" << Yaml::Value << (int64_t)lastOpened;
				out << Yaml::EndMap;
			}
			out << Yaml::EndSeq;
			out << Yaml::EndMap;
		}
		out << Yaml::EndMap;

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

		Yaml::Node data = Yaml::Load(strStream.str());
		Yaml::Node rootNode = data["UserPrefs"];
		if (!rootNode)
			return false;

		m_Preferences->ShowWelcomeScreen = rootNode["ShowWelcomeScreen"].as<bool>(true);
		m_Preferences->StartupProject = rootNode["StartupProject"] ? rootNode["StartupProject"].as<std::string>() : std::string{};
		m_Preferences->RecentProjects.clear();

		Yaml::Node recentProjects = rootNode["RecentProjects"];
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
