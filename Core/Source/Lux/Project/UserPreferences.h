#pragma once

#include "Lux/Core/Ref.h"

#include <ctime>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

namespace Lux
{
	struct RecentProject
	{
		std::string Name;
		std::string FilePath;
		time_t LastOpened = 0;
	};

	struct UserPreferences : public RefCounted
	{
		bool ShowWelcomeScreen = true;
		std::string StartupProject;
		std::map<time_t, RecentProject, std::greater<time_t>> RecentProjects;

		std::filesystem::path FilePath;
	};

	class UserPreferencesSerializer
	{
	public:
		UserPreferencesSerializer(const Ref<UserPreferences>& preferences);

		bool Serialize(const std::filesystem::path& filepath);
		bool Deserialize(const std::filesystem::path& filepath);

	private:
		Ref<UserPreferences> m_Preferences;
	};
}
