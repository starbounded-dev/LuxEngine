#pragma once

#include <cstdint>
#include <string>

namespace Lux {

	// Discord Social SDK integration: authenticates against Discord and publishes Rich Presence.
	//
	// The whole subsystem is opt-in at compile time via LUX_ENABLE_DISCORD (premake "--discord"),
	// and opt-in again at runtime via the "Discord.RichPresenceEnabled" application setting. When
	// either is off every function below is a no-op, so call sites never need to guard with #ifdef.
	//
	// Discord not running, or the user never having connected, is the expected common case - it is
	// reported as a warning and never fails a frame.
	class DiscordSocial
	{
	public:
		enum class State : uint8_t
		{
			Disabled,		// compiled out, or turned off in settings
			Disconnected,	// ready to Connect(), no token
			Authenticating,	// browser consent in flight
			Connecting,		// have a token, waiting on Client::Status::Ready
			Ready,			// presence can be published
			Failed			// see GetLastError()
		};

		// True when the SDK was compiled in (LUX_ENABLE_DISCORD). Independent of whether the
		// integration is switched on in settings - use this to tell "this build has no Discord
		// support" apart from "Discord support is turned off", which look identical from
		// GetState() alone.
		static bool IsAvailable();

		// Constructs the client and, if a saved refresh token exists, resumes the session silently.
		// Never opens a browser - an unattended launch must not steal focus.
		static void Init();
		static void Shutdown();

		// Pumps discordpp::RunCallbacks(). Must be called once per frame from the main thread;
		// every SDK callback is dispatched from inside this call, so subsystem state stays
		// main-thread-only and needs no locking.
		static void Update();

		// Begins the interactive OAuth2 (PKCE) flow. Opens the user's browser for consent.
		static void Connect();

		// Drops the session and deletes the saved token.
		static void Disconnect();

		static State GetState();
		static bool IsReady();
		static const std::string& GetUsername();
		static const std::string& GetLastError();

		// The two text lines plus the optional large/small artwork shown on the profile.
		// Image keys reference assets uploaded to the Discord application's Art Assets page
		// (or an http(s) URL); the *Text fields are the hover tooltips. Leave any field empty
		// to omit it. A count-up "elapsed" timer is added automatically from the session start,
		// so presence content doesn't carry it.
		struct PresenceInfo
		{
			std::string Details;      // top line, e.g. "Editing MyGame"
			std::string State;        // second line, e.g. the scene name
			std::string LargeImage;   // asset key or URL for the big square icon
			std::string LargeText;    // tooltip for the large image
			std::string SmallImage;   // asset key or URL for the small circular badge
			std::string SmallText;    // tooltip for the small image

			bool operator==(const PresenceInfo& other) const = default;
		};

		// Publishes a Playing activity. Cheap to call every frame: an unchanged payload is dropped
		// rather than re-sent, since Discord rate-limits presence updates. Calls made before the
		// client is Ready are cached and flushed on ready.
		static void SetPresence(const PresenceInfo& presence);

		// Convenience overload for the common text-only case.
		static void SetPresence(const std::string& details, const std::string& state);
		static void ClearPresence();
	};

}
