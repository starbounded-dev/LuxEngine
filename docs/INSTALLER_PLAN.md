# LuxEngine Installer Plan

A self-installing **Lux Installer**: one executable, built with LuxEngine in C++, that carries the
whole editor inside it and installs, updates, repairs and uninstalls it, on Windows first and then
Linux. The installer is itself a Lux app with an engine-drawn window, so every install exercises the
engine on the user's machine. This is a planning document written 2026-11-09 from
`feature/native-audio-system` at `9f5178b7`. It is not a description of what exists:
`.claude/docs/Architecture-LuxEngine.md` stays the authority for what is built.

## Goal card

- **Goal:** "a game/runtime made with LuxEngine that installs itself" and the editor.
- **Success:**
  1. Downloading `LuxEngine-Setup-<version>.exe` and running it installs a working editor with
     Start-menu and uninstall entries, with nothing else to download except the .NET SDK if it's
     missing.
  2. Running a newer setup over an install updates it in place. The installed copy also offers
     Repair and Uninstall, and Uninstall leaves nothing behind except user data.
  3. The same flows work on Linux from `LuxEngine-Setup-<version>-linux.run`.
  4. CI builds both installers with every dev pre-release, and a silent install/uninstall test
     passes on both platforms.
  5. Two editions from one codebase: **`LuxInstaller-Offline`**, with the editor bundled, and
     **`LuxInstaller`**, small, which downloads the editor from the release it belongs to (added
     2026-11-09).
- **Non-goals:**
  - C# anywhere in the installer;
  - an auto-updater that runs in the background;
  - MSI/winget/Flatpak packages (possible follow-ups);
  - code signing now (SmartScreen warnings accepted; the build leaves room for signing later).
- **User's decisions (2026-11-09):**
  - **C++ only.**
  - **Bundled payload**, then (same day) **two editions**: `LuxInstaller-Offline` (bundled) and
    `LuxInstaller` (downloads the payload).
  - **"It can do it all":** install, update, repair, uninstall.
  - **Windows first, then Linux.**
  - **DirectX:** a DirectX backend is planned, and the installer must run natively on it when it
    lands.
  - **Signing:** SmartScreen warnings are accepted for now; the owner is looking into code-signing
    certificates.
- **Constraints:**
  - Game makers install nothing extra (the product principle).
  - The installer uses only the engine's renderer API (no direct Vulkan), so a future DX12
    backend (`LUX_HAS_DX12` scaffolding) runs it unchanged.

## Decisions this plan is built on

| Decision | Choice | Consequence |
|---|---|---|
| What the installer is | A third app, **`Lux-Installer`** (NEW premake project beside `Editor` and `Lux-Runtime`), linking Core with its own C++ layer (user: C++) | No game-script layer. It builds in Dist (no shader compiler, no Tracy), so it consumes a shader pack, exactly like today's Dist Editor. |
| No .NET in the installer | NEW `ApplicationSpecification::EnableScripting` (default true); the installer sets false so `Application` skips `ScriptEngine::InitializeHost` (`Application.cpp:128`) (technical) | The installer starts without any .NET installed. Editor and Runtime behave as today. |
| Single file | Windows: delay-load `nethost.dll` and the assimp DLL in `Lux-Installer` (`/DELAYLOAD`), which Core links but the installer never calls. Linux: link those statically or load them lazily (technical) | The exe starts alone in an empty folder. Phase 0 proves it; today `Lux-Runtime` needs both DLLs beside it (`Lux-Runtime/premake5.lua:58-75`). |
| Payload | Appended to the executable: zstd-compressed files, a file table with sizes and hashes, and a footer with a magic and offsets (user: bundled; technical: format) | One implementation reads and writes it, shared by the installer and the build tool. Before Renderer init, the installer reads its own `Resources/` subset (shader pack, fonts, UI art) out of the payload into a temp folder. |
| Editions | One `Lux-Installer` binary. **Offline** = the exe with the payload appended. **Online** = the same exe with a small appended manifest naming the release and the payload's URL, size and hash. It downloads the payload archive, verifies the hash, then runs the same install steps (user: two editions; technical: one binary) | Install, update, repair and uninstall code is shared. CI publishes the payload archive (`LuxEngine-<ver>.lpayload`) as a release asset, so both editions read one format and the online one needs no zip reader. |
| Downloads (online edition) | The OS's own HTTPS client: WinHTTP on Windows, libcurl loaded at runtime on Linux; resumable, with a progress bar (technical) | No vendored HTTP or TLS library. Offline machines use the Offline edition. The download URL comes only from the manifest built into the exe, never from the network. |
| Compression | **zstd**, decoder-only in the installer (BSD-3, vendored through `Dependencies.lua`) (technical) | No compression library is vendored today; CI zips with 7-Zip (`main.yml:423`). zstd decodes much faster than deflate at a better ratio. Size cost: Phase 0 records it (the decoder is small). |
| Signing-ready layout | The payload is appended **before** signing; signing is always the last step (technical) | Authenticode requires the certificate table to be the last thing in the file ([MSRC on MS13-098](https://www.microsoft.com/en-us/msrc/blog/2013/12/ms13-098-update-to-enhance-the-security-of-authenticode)). The reader finds the footer just before the certificate table when there is one, so adding a certificate later changes nothing else. |
| Install location | Per user by default (`%LOCALAPPDATA%\Programs\LuxEngine`, `~/.local/share/luxengine`), with no admin rights. Choosing an all-users location relaunches elevated (Windows `runas`, Linux `pkexec`) (technical, conventional) | Most installs never see a UAC prompt. |
| Installed state | An install manifest (version, files with hashes, shortcuts, registry entries) written in the install folder, and a copy of the installer kept there as the maintenance tool (technical) | Update replaces only changed files, Repair restores by hash, Uninstall removes exactly what was installed. |
| UI | Drawn by the engine: `Renderer2D` with MSDF text, plus a few images (technical: it's the showcase) | Built once, branded, and DirectX-ready through the renderer API. A `--silent` mode does everything without a window for CI and power users. |
| Graphics failure | If renderer startup fails, a native message box (Windows `MessageBoxW`, Linux stderr plus `zenity` when present), with `--silent` as the escape hatch (technical) | Almost every GPU runs Vulkan; this only covers VMs, remote sessions and broken drivers. |
| .NET SDK | Detect `dotnet --list-sdks` 9.x after installing. If it's missing, explain why and offer to open Microsoft's download page; never download silently (technical, consent) | The editor already needs the .NET SDK for game scripts; the installer just makes it obvious. |

## Part 0 — Where we are

| Capability | State | Evidence |
|---|---|---|
| Apps on Core | ✅ Working | `Editor` and `Lux-Runtime` (`Lux-Runtime/premake5.lua`: `links { "Core" }`, `kind "ConsoleApp"`, a `WindowedApp` filter at :156, an `.rc`/`.ico`). |
| App options | ✅ Working | `ApplicationSpecification` (`Application.h:27-52`): `WindowDecorated`, `Resizable`, `StartMaximized`, `EnableImGui`, `RenderConfig.ShaderPackPath`, `IconPath`, `SettingsPath`. No scripting toggle: `Application` always calls `ScriptEngine::InitializeHost()` (`Application.cpp:128`). |
| Loose DLL dependencies | ⚠️ Blocks a single file | Windows apps need `nethost.dll` (Coral) and the assimp DLL beside the exe, plus the `Resources/` folder (`Lux-Runtime/premake5.lua:58-75`). |
| Dist rendering without a compiler | ✅ Starts (2026-11-09) | The Dist Editor loads the stamped `Resources/ShaderPack.lsp` (`eed580fd`, `b66511e0`). Its viewport has not been looked at yet. |
| 2D UI drawing | ✅ Working | `Renderer2D` (quads, MSDF text), used by `Lux-Runtime`'s HUD (`RuntimeLayer.cpp:468` `DrawString`). |
| Windows shell helpers | ⚠️ Partial | `WindowsFileSystem.cpp`: `SHGetKnownFolderPath` (125), `RegSetValueExA` (155). `FileSystem.cpp`: `ShellExecute` explore/open (179/194). No shortcut (`IShellLink`), uninstall-entry, elevation or self-delete helpers. |
| Compression | ❌ None vendored | `Core/vendor/` has no zlib/miniz/zstd. |
| Release packages | ✅ Working | CI's Release build packages the editor folder and zips it with 7-Zip (`main.yml` `Package:` steps). The `release` job publishes dev pre-releases with the Windows and Linux zips. |
| Version | ✅ Working | `Version.h` and the CI-baked `BuildVersion.h` (`LUX_VERSION`, `v2026.N-<commit>` tags). |
| DirectX | ⚠️ Scaffolding only | `LUX_HAS_DX11/DX12` branches reserved for a future backend (intentional, `CLAUDE.md`). |

Nothing installer-specific exists yet.

## Part 1 — Design

```
LuxEngine-Setup-<ver>.exe = [ Lux-Installer (Dist) ][ payload: file table + zstd frames ][ footer ][ (signature, later) ]

main()
 ├─ read own footer → extract Resources subset to %TEMP%/LuxInstaller-<ver>/
 ├─ --silent ? run the steps without a window
 └─ Application{ EnableScripting=false, WorkingDirectory=temp, ShaderPackPath=temp/... }
     └─ InstallerLayer (main thread): pages Welcome → Location → Progress → Done
          │      or, when installed: Update / Repair / Uninstall
          └─ InstallWorker (Lux::Thread, NEW): extract, hash, write files, shortcuts, registry,
                manifest; progress through atomics; cancel flag
```

**Code layout (NEW):**
- `Core/Source/Lux/Install/`: payload format (`InstallerArchive.{h,cpp}`), manifest, and steps.
- `Core/Platform/Windows/WindowsInstallerShell.cpp` and `Core/Platform/Linux/LinuxInstallerShell.cpp`:
  shortcuts and `.desktop`, uninstall registration, file association / mime type, elevation,
  self-delete.
- `Lux-Installer/`: the app and its layer.
- `tools/PackInstaller/`: a console tool that appends a payload to a built `Lux-Installer`, using
  the same archive code.

**Threads:**
- **Main thread:** UI only, reading progress atomics.
- **Install worker:** a `Lux::Thread` with `LUX_PROFILE_THREAD` doing file I/O, hashing, shell
  calls (`CoInitializeEx` on that thread for `IShellLink`) and registry writes.
- No ECS, no `Renderer::Submit` work from the worker. Correct under both render-thread policies.

**Updates while the editor runs:** the installer refuses to replace files while `Editor` is
running from that folder (process check), and asks the user to close it.

**DirectX:** the installer only calls `Renderer2D`/`Renderer` APIs, so the future DX12 backend
needs no installer changes. A DX12-only machine would also need a DX12 shader pack, which belongs
to the backend's own plan.

## Part 2 — Phases

### Phase 0 — Prove a lone Lux exe and a signed payload
**Goal:** settle the three unknowns before building on them.
**Spikes (throwaway):**
1. A Dist Lux app with scripting skipped and the `nethost`/assimp DLLs delay-loaded starts from an
   empty folder, using a shader pack and fonts extracted from its own file to `%TEMP%`.
2. Append a payload, sign it with a self-signed certificate, check `signtool verify /pa`, then read
   the payload back.
3. zstd: decoder size added to the exe; time to extract the Release editor folder.
4. How a renderer startup failure surfaces today (VERIFY or error), on a machine forced to fail
   (for example, a missing ICD).

**Exit:** all three unknowns are answered, or the plan changes before Phase 1. One fallback idea:
a tiny non-engine stub that extracts the full app to `%TEMP%` and runs it.

### Phase 1 — `Lux-Installer` opens a window with nothing beside it
**Changes:**
- `Application.h/.cpp`: `EnableScripting`.
- **NEW** `Lux-Installer/` premake project (Windows `WindowedApp`, icon, `/DELAYLOAD` for
  `nethost.dll` and assimp).
- **NEW** `Core/Source/Lux/Install/InstallerArchive.{h,cpp}` (read/write, zstd).
- zstd vendored via `Dependencies.lua`.
- **NEW** `tools/PackInstaller`.
- Native message box on renderer startup failure.

**Playbook:** Architecture Part 4 "dependencies" (zstd: `Dependencies.lua` entry, licence, size)
and "build toggles" if a premake option is added. New files → regenerate projects.

**Verification:**
- A packed installer with a test payload, copied alone to a fresh folder, opens its window and
  logs no validation errors.
- Editor and Runtime unchanged (scripting still starts).
- An archive round-trip test (NEW, CMake under `tests/install/`) in CI's Test phase.

**Docs:** `Building.md` (the new app, zstd); Architecture § Core (`EnableScripting`, Install module).

### Phase 2 — Install on Windows
**Changes:**
- `InstallerLayer` pages: Welcome, Location (per-user default; all-users relaunches elevated),
  Progress with Cancel, Done with "Launch LuxEngine".
- Install worker: extract and verify hashes, write the manifest, Start-menu shortcut (optional
  desktop), uninstall entry (HKCU, or HKLM when elevated) pointing at the kept maintenance copy,
  `.luxproj` association.
- .NET SDK check.
- `--silent [--dir <path>]`.

**Verification:**
- Install from the real Release editor folder: the editor launches, opens LuxSample, and Play works.
- The Start-menu entry and Apps & Features entry appear.
- Cancel midway leaves no partial install.
- `--silent` passes the same checks without a window.

**Docs:** a help page, "Installing LuxEngine".

### Phase 3 — Update, Repair, Uninstall
**Changes:**
- Running a setup over an install compares versions: newer means update (changed files by hash,
  removed files deleted, user data kept); older warns before downgrading; equal offers Repair.
- The maintenance copy offers Repair and Uninstall.
- Uninstall removes manifest files, shortcuts, registry and association, then self-deletes (a temp
  copy finishes the job).
- Refuse while the editor runs from the install folder.

**Verification:**
- Install an older version, then update to newer: changed files only, LuxSample preferences kept.
- Corrupt a file, and Repair restores it.
- Uninstall leaves the folder, registry and Start menu clean.
- All of the above in `--silent`.

### Phase 4 — CI builds and tests the installer
**Changes:**
- The Release build packs `LuxEngine-Setup-<ver>.exe` from the packaged editor folder (a new
  `Package:` step).
- The `release` job attaches it to the dev pre-release next to the zips.
- A `Test:` job installs silently into a temp folder on a hosted runner, checks files and HKCU
  entries, launches `Editor --version`, uninstalls, and checks it's clean.

**Verification:** a dev push shows the setup exe on the pre-release and the install test passes.

### Phase 4b — The online edition `LuxInstaller`
**Changes:**
- **NEW** `Core/Source/Lux/Install/InstallerDownload.{h,cpp}`, with `Core/Platform/Windows`
  WinHTTP and `Core/Platform/Linux` libcurl (`dlopen`) backends: resumable download into the temp
  folder, progress, cancel.
- The edition manifest is appended by `tools/PackInstaller --online`.
- The archive hash is verified before any file is written.
- CI uploads `LuxEngine-<ver>.lpayload` plus both setup exes to the pre-release.

**Verification:**
- The online setup (a few MB) installs the same files as Offline, byte for byte.
- Pulling the network midway resumes on retry.
- A tampered payload (hash mismatch) is refused before writing anything.
- `--silent` works for both.

**Rollback:** the Offline edition is unaffected.

### Phase 5 — Linux
**Changes:**
- `Lux-Installer` builds on Linux (statically linked or lazy-loaded shared libraries, so it's a
  single file).
- `LinuxInstallerShell`: `~/.local/share/luxengine` (or `/opt` via `pkexec`), a `.desktop` entry
  and icon, a `.luxproj` mime type through `xdg-mime`, uninstall.
- `LuxEngine-Setup-<ver>-linux.run`.
- CI silent install test on Ubuntu.

**Verification:** silent install/update/uninstall in CI. A windowed run on a Linux desktop by the
owner (the Linux presentation path is still unaccepted in the remaster plan).

### Phase 6 — Signing hook and polish
**Changes:**
- An optional CI signing step that runs last and is skipped when no certificate secret exists.
- The installer shows "unsigned build" honestly in its About text until then.
- Install-page polish: art and an optional sound, using the native audio system.

**Verification:** a self-signed CI run verifies with `signtool verify /pa`, and the payload still reads.

## Part 3 — Risks

| Risk | Likelihood | Early detection |
|---|---|---|
| Core pulls in more loose DLLs or files than `nethost`/assimp (shaderc, Aftermath, Discord) | Medium | Phase 0 runs the exe from an empty folder under Process Monitor. |
| Renderer startup failure ends in VERIFY instead of an error we can report | Medium | Phase 0 forces a failure. A small change in device creation may be needed. |
| SmartScreen or antivirus flags an unsigned self-extractor | High (accepted) | Expected until signing. Keep the payload format plain and document it. |
| A large payload makes the exe slow to start | Low | The installer reads only the footer and the Resources subset at start. |
| Updating files the editor has locked | Medium | Process check plus refusal. Windows `MoveFileEx` delay-delete as a last resort. |
| Linux single-file linking (assimp, X11, Vulkan loader) | Medium | Phase 5 starts with the Phase 0 spike on Linux. |

## Part 4 — Open questions

- Online edition: always the release it was built for (as planned), or offer "latest" too? Latest
  would make it an updater; default is the pinned release.
- Should Lux itself use the installer to ship games? The same code could install an exported game
  (a "Make installer" export option). Follow-up.
- winget/MSIX listing later? Needs signing first.
- Per-version side-by-side installs (keep 2026.10 and 2026.11)? Default: one install, updated in place.

## Part 5 — Research notes

- **Authenticode and appended data:** the certificate table must be the last thing in the file;
  data appended after signing invalidates it
  ([MSRC](https://www.microsoft.com/en-us/msrc/blog/2013/12/ms13-098-update-to-enhance-the-security-of-authenticode)).
  Tricks that grow the certificate table to hide a payload exist
  ([signed-nsis-exe-append-payload](https://github.com/jason-klein/signed-nsis-exe-append-payload)),
  but are fragile. This plan appends before signing instead.
- **Prior art, not re-verified this session:**
  - NSIS, Inno Setup and 7-Zip SFX installers are an exe with a data overlay, signed after building.
  - Battle.net's installer is an engine-drawn app.
  - Per-user installs under `%LOCALAPPDATA%\Programs` with an HKCU uninstall entry need no admin
    rights; VS Code's user installer works this way.
- **Rejected:**
  - C# UI (user);
  - downloading the payload (user);
  - an MSI (no engine UI, and it needs WiX tooling at build time).

## Handoff

Start with Phase 0. Implement each phase with `/dev`, review with `/cr`, and ship with `/send-pr`.
