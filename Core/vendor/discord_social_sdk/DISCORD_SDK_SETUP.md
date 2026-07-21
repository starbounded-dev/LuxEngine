# Discord Social SDK

The Discord integration (`Core/Source/Lux/Social/`) is **opt-in** and off by default. Nothing in a
default build references it, so you only need any of this if you want Rich Presence.

## What's checked in

The full archive Discord ships is ~1.3 GB — it bundles every platform (Linux, macOS
`.xcframework`, Android `.aar`, arm64, plus debug binaries). What lives here is a **trimmed
Windows-release subset**, about 11 MB, which is small enough to vendor the same way assimp and
mono are:

```
include/discordpp.h                    <- the single-header C++ API
include/cdiscord.h                     <- REQUIRED: discordpp.h includes it on line 4
lib/release/discord_partner_sdk.lib    <- import library
bin/release/discord_partner_sdk.dll    <- copied next to the executable at build time
```

All four are required. `cdiscord.h` is easy to miss when trimming the archive by hand, and leaving
it out fails the build with a confusing `Cannot open include file: 'cdiscord.h'` pointing at
`discordpp.h`.

Current version: **1.9.17379**. To upgrade, re-download and replace all four files together —
`discordpp.h` and `cdiscord.h` are generated as a matched pair.

Note that `.gitignore` has a global `*.lib` rule, so the negation
`!Core/vendor/discord_social_sdk/**` is what keeps the import library committable. Don't remove it.

## Setup

1. Create an application at <https://discord.com/developers/applications> and note its
   **Application ID**.
2. On that application, enable access to the **Social SDK**. Discord gates this per-application.
3. Under **OAuth2**, add the redirect URI `http://127.0.0.1:51325/callback`. The desktop flow uses
   PKCE — do not add a client secret to this repo.
4. Regenerate projects with the integration enabled — either tick **Discord Social SDK** in the
   `scripts\Win-GenProjects.bat` menu, or pass the flag directly:

   ```
   scripts\Win-GenProjects.bat --discord
   ```

   Without it the SDK is not included, `LUX_ENABLE_DISCORD` is undefined, and the subsystem
   compiles to no-ops.

5. Build and launch the Editor, then open **Application Settings → Discord**. Enter the
   Application ID, tick *Enable Rich Presence*, and restart. Press **Connect** to authorize.

## Notes

- We link the **release** libraries in every configuration. It is a C ABI behind a DLL, so there is
  no CRT mismatch; the debug binaries are only useful for debugging the SDK itself.
- `discord_krisp.dll` is not copied to the output directory — it is only needed for voice chat,
  which this integration does not use.
- The refresh token is stored in plaintext at
  `%APPDATA%/Lux/DiscordToken.txt`. The SDK provides no encrypted credential store. It is deleted
  when you press **Disconnect**.
