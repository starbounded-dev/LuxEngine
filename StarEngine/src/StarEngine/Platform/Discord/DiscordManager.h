#pragma once

#include "StarEngine/Platform/Discord/DiscordSocialSDK.h"

#include <string>

namespace StarEngine {

	// Simple static manager to centralize rich presence text
	class DiscordManager
	{
	public:
		// appName      -> "StarEditor" or "StarEngine"
		// largeImage   -> key of your big asset in Discord app (e.g. "1446769805217759342")
		// smallImage   -> key of your small asset (e.g. "1446765425504288882")
		static void Init(DiscordSocialSDK* sdk,
			const std::string& appName,
			const std::string& largeImage,
			const std::string& smallImage);

		static void Shutdown();

		// High level events you call from Editor/Game code:
		static void OnProjectChanged(const std::string& projectName);
		static void OnSceneChanged(const std::string& sceneName);
		static void OnPlayModeChanged(bool playing);   // true = Playing, false = Editing
		static void ResetSessionStart();               // if you want to restart timer

	private:
		static void RefreshPresence(); // rebuild & send DiscordActivity

		static inline DiscordSocialSDK* s_SDK = nullptr;

		static inline std::string s_AppName;
		static inline std::string s_ProjectName;
		static inline std::string s_SceneName;
		static inline bool s_IsPlaying = false;

		static inline std::string s_LargeImageKey;
		static inline std::string s_SmallImageKey;

		static inline int64_t s_StartTimestamp = 0;
	};

} // namespace StarEngine
