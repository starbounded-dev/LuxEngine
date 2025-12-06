#include "sepch.h"
#include "DiscordSocialSDK.h"

// Define implementation in this file only
#define DISCORDPP_IMPLEMENTATION
#include <discordpp.h>

namespace StarEngine {

	DiscordSocialSDK::DiscordSocialSDK()
	{
	}

	DiscordSocialSDK::~DiscordSocialSDK()
	{
		Shutdown();
	}

	bool DiscordSocialSDK::Initialize(uint64_t applicationId)
	{
		if (m_Status != DiscordSDKStatus::Uninitialized)
			return false;

		m_ApplicationId = applicationId;
		m_Status = DiscordSDKStatus::Initializing;

		discordpp::ClientCreateOptions options{};
		auto* client = new discordpp::Client(options);
		m_Client = client;

		// Optional logging from SDK -> SE logger
		client->AddLogCallback(
			[](auto message, auto /*severity*/)
			{
				SE_CORE_INFO("[Discord] {}", message);
			},
			discordpp::LoggingSeverity::Info
		);

		// Status + error callback
		client->SetStatusChangedCallback(
			[this, client](auto status, auto error, auto details)
			{
				SE_CORE_INFO(
					"Discord status: {}",
					discordpp::Client::StatusToString(status)
				);

				if (status == discordpp::Client::Status::Ready)
				{
					m_Status = DiscordSDKStatus::Ready;
					SE_CORE_INFO("Discord Social SDK is ready!");

					auto userOpt = client->GetCurrentUserV2();
					if (userOpt.has_value())
					{
						auto& userHandle = *userOpt;
						m_CurrentUser.Id = userHandle.Id();
						m_CurrentUser.Username = userHandle.Username();

						if (auto global = userHandle.GlobalName(); global.has_value())
							m_CurrentUser.GlobalName = *global;

						if (auto avatar = userHandle.Avatar(); avatar.has_value())
							m_CurrentUser.AvatarHash = *avatar;
					}

					if (OnReady)
						OnReady();   // 🔹 fire the callback here


				}
				else if (error != discordpp::Client::Error::None)
				{
					m_Status = DiscordSDKStatus::Error;
					SE_CORE_ERROR(
						"Discord Social SDK status error: {} (details: {})",
						discordpp::Client::ErrorToString(error),
						details
					);
				}
			});

		SE_CORE_INFO("Discord Social SDK initialized (waiting for auth/Connect)");
		return true;
	}

	void DiscordSocialSDK::Shutdown()
	{
		if (m_Client)
		{
			auto* client = static_cast<discordpp::Client*>(m_Client);
			client->Drop();
			delete client;
			m_Client = nullptr;
		}
		m_Status = DiscordSDKStatus::Uninitialized;
	}

	void DiscordSocialSDK::Update()
	{
		if (!m_Client)
			return;

		// Global callback pump
		discordpp::RunCallbacks();
	}

	void DiscordSocialSDK::Connect()
	{
		if (!m_Client)
			return;

		auto* client = static_cast<discordpp::Client*>(m_Client);
		client->Connect();
	}


	void DiscordSocialSDK::Authorize(const std::function<void(bool, const std::string&)>& callback)
	{
		if (!m_Client)
		{
			SE_CORE_ERROR("DiscordSocialSDK::Authorize called with null client");
			callback(false, "");
			return;
		}

		auto* client = static_cast<discordpp::Client*>(m_Client);

		SE_CORE_INFO("DiscordSocialSDK::Authorize: starting OAuth flow");

		// PKCE verifier
		auto codeVerifier = client->CreateAuthorizationCodeVerifier();

		discordpp::AuthorizationArgs args{};
		args.SetClientId(m_ApplicationId);
		args.SetScopes(discordpp::Client::GetDefaultPresenceScopes());
		args.SetCodeChallenge(codeVerifier.Challenge());

		client->Authorize(
			args,
			[this, client, codeVerifier, callback](auto result, auto code, auto redirectUri)
			{
				if (!result.Successful())
				{
					SE_CORE_ERROR(
						"Discord authorize failed: {}",
						result.ToString()
					);
					callback(false, "");
					return;
				}

				SE_CORE_INFO("Discord authorize success, exchanging code for token…");

				client->GetToken(
					m_ApplicationId,
					code,
					codeVerifier.Verifier(),
					redirectUri,
					[this, client, callback](auto tokenResult,
						auto accessToken,
						auto /*refreshToken*/,
						auto tokenType,
						auto /*expiresIn*/,
						auto /*scopes*/)
					{
						if (!tokenResult.Successful())
						{
							SE_CORE_ERROR(
								"Discord token exchange failed: {}",
								tokenResult.ToString()
							);
							callback(false, "");
							return;
						}

						SE_CORE_INFO("Discord token exchange success, updating token…");

						client->UpdateToken(
							tokenType,
							accessToken,
							[this, client, callback, accessToken](auto updateResult)
							{
								if (!updateResult.Successful())
								{
									SE_CORE_ERROR(
										"Discord token update failed: {}",
										updateResult.ToString()
									);
									callback(false, "");
									return;
								}

								SE_CORE_INFO("Discord token update success, connecting…");
								client->Connect(); // important

								callback(true, accessToken);
							});
					});
			});
	}


	void DiscordSocialSDK::UpdateToken(const std::string& accessToken)
	{
		if (!m_Client)
			return;

		auto* client = static_cast<discordpp::Client*>(m_Client);

		client->UpdateToken(
			discordpp::AuthorizationTokenType::Bearer,
			accessToken.c_str(),
			[this, client](auto result)
			{
				if (!result.Successful())
				{
					SE_CORE_ERROR("Discord UpdateToken failed: {}", result.ToString());
				}
				else
				{
					SE_CORE_INFO("Discord UpdateToken succeeded, connecting…");
					client->Connect();
				}
			}
		);
	}


	void DiscordSocialSDK::UpdateActivity(const DiscordActivity& activity)
	{
		if (!m_Client || m_Status != DiscordSDKStatus::Ready)
			return;

		auto* client = static_cast<discordpp::Client*>(m_Client);

		discordpp::Activity discordActivity{};

		// Basic text
		if (!activity.State.empty())
			discordActivity.SetState(activity.State);
		if (!activity.Details.empty())
			discordActivity.SetDetails(activity.Details);

		// Timestamps
		if (activity.StartTimestamp > 0 || activity.EndTimestamp > 0)
		{
			discordpp::ActivityTimestamps timestamps{};
			if (activity.StartTimestamp > 0)
				timestamps.SetStart(static_cast<uint64_t>(activity.StartTimestamp));
			if (activity.EndTimestamp > 0)
				timestamps.SetEnd(static_cast<uint64_t>(activity.EndTimestamp));
			discordActivity.SetTimestamps(timestamps);
		}

		// Assets
		if (!activity.LargeImageKey.empty() || !activity.SmallImageKey.empty())
		{
			discordpp::ActivityAssets assets{};

			if (!activity.LargeImageKey.empty())
			{
				assets.SetLargeImage(activity.LargeImageKey);
				if (!activity.LargeImageText.empty())
					assets.SetLargeText(activity.LargeImageText);
			}

			if (!activity.SmallImageKey.empty())
			{
				assets.SetSmallImage(activity.SmallImageKey);
				if (!activity.SmallImageText.empty())
					assets.SetSmallText(activity.SmallImageText);
			}

			discordActivity.SetAssets(assets);
		}

		// Join secret
		if (!activity.JoinSecret.empty())
		{
			discordpp::ActivitySecrets secrets{};
			secrets.SetJoin(activity.JoinSecret);
			discordActivity.SetSecrets(secrets);
		}

		client->UpdateRichPresence(
			std::move(discordActivity),
			[](auto result)
			{
				if (result.Successful())
					SE_CORE_TRACE("Discord rich presence updated");
				else
					SE_CORE_ERROR("Failed to update Discord rich presence: {}", result.ToString());
			}
		);
	}

	void DiscordSocialSDK::ClearActivity()
	{
		if (!m_Client || m_Status != DiscordSDKStatus::Ready)
			return;

		auto* client = static_cast<discordpp::Client*>(m_Client);

		discordpp::Activity empty{};
		client->UpdateRichPresence(
			std::move(empty),
			[](auto /*result*/)
			{
				SE_CORE_TRACE("Discord rich presence cleared");
			}
		);
	}

	void DiscordSocialSDK::GetFriends(const std::function<void(const std::vector<DiscordUser>&)>& callback)
	{
		if (!m_Client || m_Status != DiscordSDKStatus::Ready)
		{
			callback({});
			return;
		}

		auto* client = static_cast<discordpp::Client*>(m_Client);

		std::vector<DiscordUser> friends;
		auto relationships = client->GetRelationships();

		for (const auto& relationship : relationships)
		{
			if (relationship.DiscordRelationshipType() != discordpp::RelationshipType::Friend)
				continue;

			auto userOpt = relationship.User();
			if (!userOpt.has_value())
				continue;

			auto user = *userOpt;

			DiscordUser du{};
			du.Id = user.Id();
			du.Username = user.Username();

			if (auto global = user.GlobalName(); global.has_value())
				du.GlobalName = *global;

			if (auto avatar = user.Avatar(); avatar.has_value())
				du.AvatarHash = *avatar;

			friends.push_back(std::move(du));
		}

		callback(friends);
	}

} // namespace StarEngine
