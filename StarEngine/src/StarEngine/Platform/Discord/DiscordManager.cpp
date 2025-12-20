#include "sepch.h"
#include "DiscordManager.h"

#include <ctime>

namespace StarEngine {

	void DiscordManager::Init(DiscordSocialSDK* sdk,
		const std::string& appName,
		const std::string& largeImage,
		const std::string& smallImage)
	{
		s_SDK = sdk;
		s_AppName = appName;
		s_LargeImageKey = largeImage;
		s_SmallImageKey = smallImage;

		s_IsPlaying = false;
		s_ProjectName.clear();
		s_SceneName.clear();
		s_StartTimestamp = static_cast<int64_t>(std::time(nullptr));

		RefreshPresence();
	}

	void DiscordManager::Shutdown()
	{
		if (s_SDK && s_SDK->IsReady())
			s_SDK->ClearActivity();

		s_SDK = nullptr;
	}

	void DiscordManager::OnProjectChanged(const std::string& projectName)
	{
		s_ProjectName = projectName;
		RefreshPresence();
	}

	void DiscordManager::OnSceneChanged(const std::string& sceneName)
	{
		s_SceneName = sceneName;
		RefreshPresence();
	}

	void DiscordManager::OnPlayModeChanged(bool playing)
	{
		s_IsPlaying = playing;
		RefreshPresence();
	}

	void DiscordManager::ResetSessionStart()
	{
		s_StartTimestamp = static_cast<int64_t>(std::time(nullptr));
		RefreshPresence();
	}

	void DiscordManager::RefreshPresence()
	{
		if (!s_SDK || !s_SDK->IsReady())
			return;

		DiscordActivity activity{};
		activity.StartTimestamp = s_StartTimestamp;

		// State line
		activity.State = s_IsPlaying ? "Playing" : "Editing";

		// Details line: App / Project / Scene
		std::string details;

		if (!s_ProjectName.empty())
		{
			// Always start with app name
			details = s_AppName;                  // e.g. "StarEditor"
			details += " / ";
			details += s_ProjectName;             // project

			if (!s_SceneName.empty())
			{
				details += " / ";
				details += s_SceneName;           // scene
			}
		}
		else
		{
			// No project open, just show app
			details = s_AppName;
		}

		activity.Details = details;

		// Assets
		if (!s_LargeImageKey.empty())
		{
			activity.LargeImageKey = s_LargeImageKey;
			activity.LargeImageText = s_AppName;
		}

		if (!s_SmallImageKey.empty())
		{
			activity.SmallImageKey = s_SmallImageKey;
			activity.SmallImageText = "StarEngine";
		}

		s_SDK->UpdateActivity(activity);
	}

										
} // namespace StarEngine
