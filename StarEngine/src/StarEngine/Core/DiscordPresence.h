#pragma once

#include "StarEngine/Core/Application.h"
#include "StarEngine/Platform/Discord/DiscordSocialSDK.h"

namespace StarEngine::DiscordPresence
{
	inline void Set(const DiscordActivity& activity)
	{
		auto& app = Application::Get();
		auto discord = app.GetDiscord();
		if (!discord || !discord->IsReady())
			return;

		discord->UpdateActivity(activity);
	}

	inline void Clear()
	{
		auto& app = Application::Get();
		auto discord = app.GetDiscord();
		if (!discord || !discord->IsReady())
			return;

		discord->ClearActivity();
	}
}
