#include "lpch.h"
#include "DiscordSocial.h"

#include "Lux/Core/Application.h"
#include "Lux/Utilities/FileSystem.h"

#ifdef LUX_ENABLE_DISCORD
	// The SDK's implementation lives in DiscordppImpl.cpp; here we only need the declarations.
	#include <discordpp.h>

	#include <format>
#endif

namespace Lux {

	namespace {
		const std::string s_EmptyString;
	}

#ifndef LUX_ENABLE_DISCORD

	// Built without the Social SDK. Every entry point degrades to a no-op so call sites in
	// Application and the editor panels stay free of #ifdef.

	bool DiscordSocial::IsAvailable() { return false; }
	void DiscordSocial::Init() {}
	void DiscordSocial::Shutdown() {}
	void DiscordSocial::Update() {}
	void DiscordSocial::Connect() {}
	void DiscordSocial::Disconnect() {}

	DiscordSocial::State DiscordSocial::GetState() { return State::Disabled; }
	bool DiscordSocial::IsReady() { return false; }
	const std::string& DiscordSocial::GetUsername() { return s_EmptyString; }
	const std::string& DiscordSocial::GetLastError() { return s_EmptyString; }

	void DiscordSocial::SetPresence(const std::string&, const std::string&) {}
	void DiscordSocial::ClearPresence() {}

#else

	namespace {

		struct DiscordData
		{
			std::shared_ptr<discordpp::Client> Client;
			DiscordSocial::State State = DiscordSocial::State::Disabled;

			uint64_t ApplicationID = 0;
			std::string Username;
			std::string LastError;

			// Last presence we actually sent, so repeat calls can be dropped. Discord rate-limits
			// presence updates, and the editor would otherwise push one every frame.
			std::string SentDetails;
			std::string SentState;
			bool HasSentPresence = false;

			// Presence requested before the client reached Ready, flushed on ready.
			std::string PendingDetails;
			std::string PendingState;
			bool HasPendingPresence = false;
		};

		DiscordData* s_Data = nullptr;

		std::filesystem::path GetTokenPath()
		{
			return FileSystem::GetPersistentStoragePath() / "DiscordToken.txt";
		}

		// NOTE: the refresh token is stored in plaintext under the user's roaming AppData. The
		// Social SDK offers no encrypted credential store, and this matches what other engines do,
		// but it is worth being explicit: anything that can read the user's profile can read this
		// token. It is deleted on Disconnect().
		std::string LoadSavedToken()
		{
			std::ifstream stream(GetTokenPath());
			if (!stream)
				return {};

			std::string token;
			std::getline(stream, token);
			return token;
		}

		void SaveToken(const std::string& refreshToken)
		{
			std::ofstream stream(GetTokenPath(), std::ios::trunc);
			if (!stream)
			{
				LUX_CORE_WARN_TAG("Discord", "Failed to persist refresh token to {}", GetTokenPath().string());
				return;
			}

			stream << refreshToken;
		}

		void ClearSavedToken()
		{
			std::error_code ec;
			std::filesystem::remove(GetTokenPath(), ec);
		}

		void FlushPresence();

		void OnStatusChanged(discordpp::Client::Status status, discordpp::Client::Error error, int32_t errorDetail)
		{
			if (!s_Data)
				return;

			switch (status)
			{
				case discordpp::Client::Status::Ready:
				{
					s_Data->State = DiscordSocial::State::Ready;
					s_Data->LastError.clear();

					// V2 returns nullopt when there is no user; the non-V2 overload is deprecated
					// and hands back a dummy object instead.
					if (auto user = s_Data->Client->GetCurrentUserV2(); user.has_value())
						s_Data->Username = user->DisplayName();

					LUX_CORE_INFO_TAG("Discord", "Connected as {}", s_Data->Username.empty() ? "<unknown>" : s_Data->Username);

					FlushPresence();
					break;
				}
				case discordpp::Client::Status::Connecting:
				case discordpp::Client::Status::Connected:
					s_Data->State = DiscordSocial::State::Connecting;
					break;
				case discordpp::Client::Status::Disconnected:
				{
					// Disconnected with an error means the session dropped; without one it is just
					// the idle state we start in.
					if (error != discordpp::Client::Error::None)
					{
						s_Data->State = DiscordSocial::State::Failed;
						s_Data->LastError = std::format("{} ({})", discordpp::Client::ErrorToString(error), errorDetail);
						LUX_CORE_WARN_TAG("Discord", "Disconnected: {}", s_Data->LastError);
					}
					else if (s_Data->State != DiscordSocial::State::Authenticating)
					{
						s_Data->State = DiscordSocial::State::Disconnected;
					}

					s_Data->Username.clear();
					s_Data->HasSentPresence = false;
					break;
				}
				default:
					break;
			}
		}

		void FlushPresence()
		{
			if (!s_Data || !s_Data->HasPendingPresence)
				return;

			s_Data->HasPendingPresence = false;
			DiscordSocial::SetPresence(s_Data->PendingDetails, s_Data->PendingState);
		}

		// Exchanges an authorization code (or a saved refresh token) for a live session.
		void ApplyToken(const std::string& accessToken, const std::string& refreshToken)
		{
			if (!refreshToken.empty())
				SaveToken(refreshToken);

			s_Data->Client->UpdateToken(discordpp::AuthorizationTokenType::Bearer, accessToken,
				[](discordpp::ClientResult result)
				{
					if (!s_Data)
						return;

					if (!result.Successful())
					{
						s_Data->State = DiscordSocial::State::Failed;
						s_Data->LastError = result.Error();
						LUX_CORE_WARN_TAG("Discord", "Failed to apply token: {}", s_Data->LastError);
						return;
					}

					s_Data->State = DiscordSocial::State::Connecting;
					s_Data->Client->Connect();
				});
		}

	}

	bool DiscordSocial::IsAvailable()
	{
		return true;
	}

	void DiscordSocial::Init()
	{
		LUX_PROFILE_FUNCTION("DiscordSocial::Init");

		if (s_Data)
			return;

		const ApplicationSettings& settings = Application::Get().GetSettings();
		if (settings.Get("Discord.RichPresenceEnabled", "false") != "true")
		{
			LUX_CORE_TRACE_TAG("Discord", "Rich Presence disabled in application settings");
			return;
		}

		const std::string applicationID = settings.Get("Discord.ApplicationID", "");
		if (applicationID.empty())
		{
			LUX_CORE_WARN_TAG("Discord", "Rich Presence is enabled but Discord.ApplicationID is unset - skipping");
			return;
		}

		s_Data = lnew DiscordData();
		s_Data->State = State::Disconnected;

		try
		{
			s_Data->ApplicationID = std::stoull(applicationID);
		}
		catch (const std::exception&)
		{
			s_Data->State = State::Failed;
			s_Data->LastError = std::format("'{}' is not a valid application ID", applicationID);
			LUX_CORE_WARN_TAG("Discord", "{}", s_Data->LastError);
			return;
		}

		s_Data->Client = std::make_shared<discordpp::Client>();
		s_Data->Client->SetStatusChangedCallback(&OnStatusChanged);

		// Resume a previous session if we have one. This deliberately does not open a browser -
		// an unattended launch must never steal focus.
		const std::string refreshToken = LoadSavedToken();
		if (refreshToken.empty())
		{
			LUX_CORE_TRACE_TAG("Discord", "No saved session; waiting for an explicit Connect()");
			return;
		}

		s_Data->State = State::Connecting;
		s_Data->Client->RefreshToken(s_Data->ApplicationID, refreshToken,
			[](discordpp::ClientResult result, std::string accessToken, std::string newRefreshToken,
			   discordpp::AuthorizationTokenType, int32_t, std::string)
			{
				if (!s_Data)
					return;

				if (!result.Successful())
				{
					// A stale token is not an error worth surfacing loudly - drop it and fall back
					// to the disconnected state so the user can reconnect from the settings panel.
					LUX_CORE_WARN_TAG("Discord", "Saved session expired, sign in again to restore presence");
					ClearSavedToken();
					s_Data->State = State::Disconnected;
					return;
				}

				ApplyToken(accessToken, newRefreshToken);
			});
	}

	void DiscordSocial::Shutdown()
	{
		if (!s_Data)
			return;

		if (s_Data->Client)
		{
			// Drop presence so a closed editor doesn't linger on the user's profile.
			if (s_Data->State == State::Ready)
				s_Data->Client->ClearRichPresence();

			s_Data->Client->Disconnect();

			// One last pump so the disconnect actually leaves the process.
			discordpp::RunCallbacks();
		}

		delete s_Data;
		s_Data = nullptr;
	}

	void DiscordSocial::Update()
	{
		if (!s_Data)
			return;

		LUX_PROFILE_FUNCTION("DiscordSocial::Update");
		discordpp::RunCallbacks();
	}

	void DiscordSocial::Connect()
	{
		if (!s_Data || !s_Data->Client)
			return;

		if (s_Data->State == State::Authenticating || s_Data->State == State::Ready)
			return;

		s_Data->State = State::Authenticating;
		s_Data->LastError.clear();

		auto codeVerifier = s_Data->Client->CreateAuthorizationCodeVerifier();

		discordpp::AuthorizationArgs args{};
		args.SetClientId(s_Data->ApplicationID);
		args.SetScopes(discordpp::Client::GetDefaultPresenceScopes());
		args.SetCodeChallenge(codeVerifier.Challenge());

		LUX_CORE_INFO_TAG("Discord", "Opening browser for authorization");

		s_Data->Client->Authorize(args,
			[codeVerifier](discordpp::ClientResult result, std::string code, std::string redirectUri)
			{
				if (!s_Data)
					return;

				if (!result.Successful())
				{
					s_Data->State = State::Failed;
					s_Data->LastError = result.Error();
					LUX_CORE_WARN_TAG("Discord", "Authorization failed: {}", s_Data->LastError);
					return;
				}

				s_Data->Client->GetToken(s_Data->ApplicationID, code, codeVerifier.Verifier(), redirectUri,
					[](discordpp::ClientResult tokenResult, std::string accessToken, std::string refreshToken,
					   discordpp::AuthorizationTokenType, int32_t, std::string)
					{
						if (!s_Data)
							return;

						if (!tokenResult.Successful())
						{
							s_Data->State = State::Failed;
							s_Data->LastError = tokenResult.Error();
							LUX_CORE_WARN_TAG("Discord", "Token exchange failed: {}", s_Data->LastError);
							return;
						}

						ApplyToken(accessToken, refreshToken);
					});
			});
	}

	void DiscordSocial::Disconnect()
	{
		if (!s_Data)
			return;

		ClearSavedToken();

		if (s_Data->Client)
		{
			if (s_Data->State == State::Ready)
				s_Data->Client->ClearRichPresence();

			s_Data->Client->Disconnect();
		}

		s_Data->State = State::Disconnected;
		s_Data->Username.clear();
		s_Data->HasSentPresence = false;
		s_Data->HasPendingPresence = false;

		LUX_CORE_INFO_TAG("Discord", "Disconnected");
	}

	DiscordSocial::State DiscordSocial::GetState()
	{
		return s_Data ? s_Data->State : State::Disabled;
	}

	bool DiscordSocial::IsReady()
	{
		return s_Data && s_Data->State == State::Ready;
	}

	const std::string& DiscordSocial::GetUsername()
	{
		return s_Data ? s_Data->Username : s_EmptyString;
	}

	const std::string& DiscordSocial::GetLastError()
	{
		return s_Data ? s_Data->LastError : s_EmptyString;
	}

	void DiscordSocial::SetPresence(const std::string& details, const std::string& state)
	{
		if (!s_Data)
			return;

		if (s_Data->State != State::Ready)
		{
			// Hold the most recent request; it is flushed once the client reaches Ready.
			s_Data->PendingDetails = details;
			s_Data->PendingState = state;
			s_Data->HasPendingPresence = true;
			return;
		}

		if (s_Data->HasSentPresence && s_Data->SentDetails == details && s_Data->SentState == state)
			return;

		s_Data->SentDetails = details;
		s_Data->SentState = state;
		s_Data->HasSentPresence = true;

		discordpp::Activity activity;
		activity.SetType(discordpp::ActivityTypes::Playing);
		activity.SetDetails(details);
		activity.SetState(state);

		s_Data->Client->UpdateRichPresence(std::move(activity),
			[](discordpp::ClientResult result)
			{
				if (!result.Successful())
					LUX_CORE_WARN_TAG("Discord", "Failed to update Rich Presence: {}", result.Error());
			});
	}

	void DiscordSocial::ClearPresence()
	{
		if (!s_Data)
			return;

		s_Data->HasSentPresence = false;
		s_Data->HasPendingPresence = false;

		if (s_Data->State == State::Ready)
			s_Data->Client->ClearRichPresence();
	}

#endif

}
