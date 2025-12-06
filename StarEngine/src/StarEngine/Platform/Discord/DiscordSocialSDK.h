#pragma once

#include "StarEngine/Core/Base.h"
#include <string>
#include <vector>
#include <functional>

namespace discordpp { class Client; }

namespace StarEngine {

	enum class DiscordSDKStatus
	{
		Uninitialized,
		Initializing,
		Ready,
		Error
	};

	struct DiscordUser
	{
		uint64_t Id = 0;
		std::string Username;
		std::string GlobalName;
		std::string AvatarHash;
	};

	struct DiscordActivity
	{
		std::string State;
		std::string Details;
		int64_t StartTimestamp = 0;
		int64_t EndTimestamp = 0;
		std::string LargeImageKey;
		std::string LargeImageText;
		std::string SmallImageKey;
		std::string SmallImageText;
		int32_t PartySize = 0;
		int32_t PartyMax = 0;
		std::string JoinSecret;
	};

	class DiscordSocialSDK
	{
	public:
		DiscordSocialSDK();
		~DiscordSocialSDK();

		bool Initialize(uint64_t applicationId);
		void Shutdown();

		void Update();

		void Connect();

		void Authorize(const std::function<void(bool success, const std::string& accessToken)>& callback);
		void UpdateToken(const std::string& accessToken);

		void UpdateActivity(const DiscordActivity& activity);
		void ClearActivity();

		void GetFriends(const std::function<void(const std::vector<DiscordUser>&)>& callback);

		DiscordSDKStatus GetStatus() const { return m_Status; }
		bool IsReady() const { return m_Status == DiscordSDKStatus::Ready; }

		const DiscordUser& GetCurrentUser() const { return m_CurrentUser; }

		std::function<void()> OnReady;
		void SetOnReadyCallback(const std::function<void()>& cb) { OnReady = cb; }

		discordpp::Client* GetRawClient() const { return static_cast<discordpp::Client*>(m_Client); }
	private:
		void* m_Client = nullptr;  // discordpp::Client*
		DiscordSDKStatus m_Status = DiscordSDKStatus::Uninitialized;
		uint64_t m_ApplicationId = 0;
		DiscordUser m_CurrentUser;
	};

}
