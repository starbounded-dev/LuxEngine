#include "lpch.h"
#include "DiscordSocial.h"

#include "Lux/Core/Application.h"
#include "Lux/Utilities/FileSystem.h"

#ifdef LUX_ENABLE_DISCORD
	// The SDK's implementation lives in DiscordppImpl.cpp; here we only need the declarations.
	#include <discordpp.h>

	#include <chrono>
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

	void DiscordSocial::SetPresence(const PresenceInfo&) {}
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

			// Milliseconds since the Unix epoch, captured at Init(). Sent as the activity start so
			// Discord renders a count-up "elapsed" timer for the whole session; kept constant across
			// presence changes so switching scenes doesn't reset it.
			uint64_t SessionStartMs = 0;

			// Last presence we actually sent, so repeat calls can be dropped. Discord rate-limits
			// presence updates, and the editor would otherwise push one every frame.
			DiscordSocial::PresenceInfo Sent;
			bool HasSentPresence = false;

			// Presence requested before the client reached Ready, flushed on ready.
			DiscordSocial::PresenceInfo Pending;
			bool HasPendingPresence = false;
		};

		DiscordData* s_Data = nullptr;

		// A persisted session. The access token authenticates directly (valid ~7 days), so on a
		// normal restart it can be reused with no network round trip; the refresh token is the
		// fallback for when it has expired.
		struct SavedSession
		{
			std::string AccessToken;
			std::string RefreshToken;
			uint64_t ExpiresAtMs = 0;   // access-token expiry, ms since epoch; 0 = unknown
		};

		std::filesystem::path GetTokenPath()
		{
			return FileSystem::GetPersistentStoragePath() / "DiscordToken.txt";
		}

		uint64_t NowMs()
		{
			return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
		}

		// NOTE: tokens are stored in plaintext under the user's roaming AppData. The Social SDK
		// offers no encrypted credential store, and this matches what other engines do, but it is
		// worth being explicit: anything that can read the user's profile can read these. Deleted
		// on Disconnect(). Format is three lines: access, refresh, expiry-ms.
		SavedSession LoadSavedSession()
		{
			std::ifstream stream(GetTokenPath());
			if (!stream)
				return {};

			SavedSession session;
			std::string expiresLine;
			std::getline(stream, session.AccessToken);
			std::getline(stream, session.RefreshToken);
			if (std::getline(stream, expiresLine) && !expiresLine.empty())
			{
				try { session.ExpiresAtMs = std::stoull(expiresLine); }
				catch (const std::exception&) { session.ExpiresAtMs = 0; }
			}
			return session;
		}

		void SaveSession(const SavedSession& session)
		{
			std::ofstream stream(GetTokenPath(), std::ios::trunc);
			if (!stream)
			{
				LUX_CORE_WARN_TAG("Discord", "Failed to persist session to {}", GetTokenPath().string());
				return;
			}

			stream << session.AccessToken << '\n'
			       << session.RefreshToken << '\n'
			       << session.ExpiresAtMs << '\n';
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
			DiscordSocial::SetPresence(s_Data->Pending);
		}

		// Hands an access token to the client and connects. persistSession controls whether the
		// session is (re)written to disk - true for freshly issued tokens (login/refresh), false
		// when we're just reusing what's already saved.
		void ConnectWithAccessToken(const std::string& accessToken, bool logSuccess)
		{
			s_Data->Client->UpdateToken(discordpp::AuthorizationTokenType::Bearer, accessToken,
				[logSuccess](discordpp::ClientResult result)
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

					if (logSuccess)
						LUX_CORE_INFO_TAG("Discord", "Resuming saved session");

					s_Data->State = DiscordSocial::State::Connecting;
					s_Data->Client->Connect();
				});
		}

		// Persists a freshly issued token set (from login or refresh) and connects with it.
		// expiresInSeconds comes straight from the token-exchange callback.
		void ApplyToken(const std::string& accessToken, const std::string& refreshToken, int32_t expiresInSeconds)
		{
			SavedSession session;
			session.AccessToken = accessToken;
			session.RefreshToken = refreshToken;
			session.ExpiresAtMs = expiresInSeconds > 0 ? NowMs() + (uint64_t)expiresInSeconds * 1000ull : 0;
			SaveSession(session);

			ConnectWithAccessToken(accessToken, false);
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
		s_Data->SessionStartMs = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();

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
		const SavedSession session = LoadSavedSession();
		if (session.AccessToken.empty() && session.RefreshToken.empty())
		{
			LUX_CORE_TRACE_TAG("Discord", "No saved session; waiting for an explicit Connect()");
			return;
		}

		s_Data->State = State::Connecting;

		// Prefer reusing the access token directly - it's valid for ~7 days, so a normal restart
		// needs no token exchange at all. Only fall back to a refresh once it's expired (or its
		// expiry is unknown). A 60s margin avoids racing an about-to-expire token.
		const bool accessUsable = !session.AccessToken.empty() &&
			session.ExpiresAtMs > NowMs() + 60'000;

		if (accessUsable)
		{
			ConnectWithAccessToken(session.AccessToken, true);
			return;
		}

		if (session.RefreshToken.empty())
		{
			LUX_CORE_TRACE_TAG("Discord", "Saved access token expired and no refresh token; sign in again");
			ClearSavedToken();
			s_Data->State = State::Disconnected;
			return;
		}

		s_Data->Client->RefreshToken(s_Data->ApplicationID, session.RefreshToken,
			[](discordpp::ClientResult result, std::string accessToken, std::string newRefreshToken,
			   discordpp::AuthorizationTokenType, int32_t expiresIn, std::string)
			{
				if (!s_Data)
					return;

				if (!result.Successful())
				{
					// Surface the real reason - "invalid_grant" (token truly stale) vs
					// "unauthorized_client" (app isn't a Public Client on the OAuth2 page) need
					// very different fixes, and hiding it behind a generic message cost a debug cycle.
					LUX_CORE_WARN_TAG("Discord", "Token refresh failed ({}); sign in again to restore presence", result.Error());
					ClearSavedToken();
					s_Data->State = State::Disconnected;
					return;
				}

				ApplyToken(accessToken, newRefreshToken, expiresIn);
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
					   discordpp::AuthorizationTokenType, int32_t expiresIn, std::string)
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

						ApplyToken(accessToken, refreshToken, expiresIn);
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
		SetPresence(PresenceInfo{ details, state });
	}

	void DiscordSocial::SetPresence(const PresenceInfo& presence)
	{
		if (!s_Data)
			return;

		if (s_Data->State != State::Ready)
		{
			// Hold the most recent request; it is flushed once the client reaches Ready.
			s_Data->Pending = presence;
			s_Data->HasPendingPresence = true;
			return;
		}

		// The session timer is constant, so it isn't part of this comparison - only the
		// content the caller controls.
		if (s_Data->HasSentPresence && s_Data->Sent == presence)
			return;

		s_Data->Sent = presence;
		s_Data->HasSentPresence = true;

		discordpp::Activity activity;
		activity.SetType(discordpp::ActivityTypes::Playing);
		activity.SetDetails(presence.Details);
		activity.SetState(presence.State);

		// Count-up "elapsed" timer since the session began.
		if (s_Data->SessionStartMs != 0)
		{
			discordpp::ActivityTimestamps timestamps;
			timestamps.SetStart(s_Data->SessionStartMs);
			activity.SetTimestamps(std::move(timestamps));
		}

		// Artwork. Discord rejects an assets object with an empty image key, so only attach one
		// when at least one image is set, and leave each individual field unset when empty.
		if (!presence.LargeImage.empty() || !presence.SmallImage.empty())
		{
			discordpp::ActivityAssets assets;
			if (!presence.LargeImage.empty())
				assets.SetLargeImage(presence.LargeImage);
			if (!presence.LargeText.empty())
				assets.SetLargeText(presence.LargeText);
			if (!presence.SmallImage.empty())
				assets.SetSmallImage(presence.SmallImage);
			if (!presence.SmallText.empty())
				assets.SetSmallText(presence.SmallText);
			activity.SetAssets(std::move(assets));
		}

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
