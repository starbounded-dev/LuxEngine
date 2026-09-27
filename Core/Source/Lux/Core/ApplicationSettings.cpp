// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "ApplicationSettings.h"

#include "Lux/Serialization/Yaml.h"

#include <fstream>
#include <iostream>

namespace Lux {

	static void CreateDirectoriesIfNeeded(const std::filesystem::path& path)
	{
		std::filesystem::path directory = path.parent_path();
		if (directory.empty())
			return;

		if (!std::filesystem::exists(directory))
			std::filesystem::create_directories(directory);
	}

	ApplicationSettings::ApplicationSettings(const std::filesystem::path& filepath)
		: m_FilePath(filepath)
	{
		Deserialize();
	}

	void ApplicationSettings::Serialize()
	{
		Yaml::Writer out;
		out << Yaml::BeginMap;
		out << Yaml::Key << "Lux Application Settings" << Yaml::Value << Yaml::BeginMap;
		for (const auto& [key, value] : m_Settings)
			out << Yaml::Key << key << Yaml::Value << value;
		out << Yaml::EndMap;
		out << Yaml::EndMap;

		CreateDirectoriesIfNeeded(m_FilePath);
		std::ofstream fout(m_FilePath);
		fout << out.c_str();

		fout.close();
	}

	bool ApplicationSettings::Deserialize()
	{
		std::ifstream stream(m_FilePath);
		if (!stream.good())
			return false;

		std::stringstream strStream;
		strStream << stream.rdbuf();

		Yaml::Node data = Yaml::Load(strStream.str());
		Yaml::Node settings = data["Lux Application Settings"];
		if (!settings)
			settings = data["Core Application Settings"];
		if (!settings)
			settings = data["Hazel Application Settings"];
		if (!settings)
			return false;

		m_Settings.clear();
		for (auto it = settings.begin(); it != settings.end(); it++)
		{
			const auto& key = it->first.as<std::string>();
			const auto& value = it->second.as<std::string>();
			m_Settings[key] = value;
		}

		stream.close();
		return true;
	}

	bool ApplicationSettings::HasKey(std::string_view key) const
	{
		return m_Settings.find(std::string(key)) != m_Settings.end();
	}

	std::string ApplicationSettings::Get(std::string_view name, const std::string& defaultValue) const
	{
		if (!HasKey(name))
			return defaultValue;

		return m_Settings.at(std::string(name));
	}

	float ApplicationSettings::GetFloat(std::string_view name, float defaultValue) const
	{
		if (!HasKey(name))
			return defaultValue;

		const std::string& string = m_Settings.at(std::string(name));
		return std::stof(string);
	}

	int ApplicationSettings::GetInt(std::string_view name, int defaultValue) const
	{
		if (!HasKey(name))
			return defaultValue;

		const std::string& string = m_Settings.at(std::string(name));
		return std::stoi(string);
	}

	void ApplicationSettings::Set(std::string_view name, std::string_view value)
	{
		m_Settings[std::string(name)] = value;
	}

	void ApplicationSettings::SetFloat(std::string_view name, float value)
	{
		m_Settings[std::string(name)] = std::to_string(value);
	}

	void ApplicationSettings::SetInt(std::string_view name, int value)
	{
		m_Settings[std::string(name)] = std::to_string(value);
	}

}
