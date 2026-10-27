# Desktop audio platforms — Phase 15

Windows and Linux share the FMOD Studio/Core + Asterwave implementation. Console SDK,
hardware, suspend/resume and certification integration are **on hold** until access is available.
There are no console build targets or claims of console readiness in this phase.

## Project profiles

1. Open **Project Settings → Audio → Desktop Audio Profiles**.
2. Enable **Override defaults** for the operating system you want to configure. Existing projects
   keep using their defaults until a profile is enabled.
3. Enter the exact platform name from your FMOD Studio project's build settings. `Desktop` works
   with the sample project. To use separate Windows/Linux outputs, first create those platform
   configurations in FMOD Studio. Lux does not create or rename Studio platforms.
4. Set **Bank Output** to the corresponding output directory, relative to the `.fspro` directory
   (for example `Build/Windows` or `Build/Linux`). The name and directory must match Studio's setup.
5. Use **Copy Default Budgets** to copy the default voice, CPU, Asterwave, memory, bus and focus settings.
   Then adjust this profile's limits. To add/remove bus paths, change the default bus list and copy
   it again; individual profile bus warning limits can be edited independently.
6. Optionally enable **Mute When Unfocused**. It defaults off. It mutes output while unfocused or
   minimized, preserves authored bus state and script pause state, and leaves FMOD timelines
   advancing. Scene simulation still pauses when the application is minimized.
7. Save and reopen the project to apply budgets/focus behavior. Click **Build Banks Now**, then
   run validation in **View → Audio Debugger** before Play or export.

Platform names allow 1–64 ASCII letters, digits, spaces, underscores and hyphens, without leading
or trailing spaces. Build requests target exactly one platform. Unknown names fail in Studio;
invalid names are rejected before launching the tool.

The host operating system selects its enabled profile for bank builds, Play, validation and native
exports. Disabling a profile restores project defaults without deleting the profile's settings.
Play loads the selected directory even if another profile's banks were previously loaded. A failed
rebuild may use existing output from that directory; if loading fails, Play continues with no
project banks and reports the failure, rather than playing a different profile's catalog.

Exports package the selected banks and effective budgets/focus setting. Authoring profile records
are not needed by the player. Runtime format 23 keeps its bounded settings block; absent new fields
use defaults. Build the Windows player on Windows and the Linux player on Linux. This does not
cross-compile executables or generate banks automatically during export.

## SDK setup

Use compatible FMOD Studio and Engine SDK versions from the 2.03 family. Current Linux validation
uses Engine SDK 2.03.14; the installed Studio command-line tool is 2.03.12.

FMOD and Asterwave are required. Extract the native FMOD Engine SDK into a subdirectory of
`Core/vendor/FMOD/`; Premake detects its headers and native link library. Multiple matching SDKs
require an explicit selection. Asterwave compiles statically from an exact private source revision:

```powershell
# Fetch with Git credentials that can read the private Asterwave repository.
python scripts/FetchAsterwave.py
python scripts/FetchAsterwave.py --check

# Optional alternate dependency locations, selected before generation:
$env:LUX_FMOD_SDK = 'C:\SDKs\FMOD Studio API Windows'
$env:LUX_ASTERWAVE_ROOT = 'C:\Source\Asterwave'
# Optional if Studio's command-line tool is not found automatically:
$env:LUX_FMOD_STUDIO_CL = 'C:\Program Files\FMOD SoundSystem\FMOD Studio 2.03.14\fmodstudiocl.exe'
.\scripts\Win-GenProjects.bat --last
```

The default Asterwave location is `Core/vendor/Asterwave`. Its revision is pinned in
`scripts/dependencies/Asterwave.json`; `--root PATH` or `LUX_ASTERWAVE_ROOT` selects another
checkout. Generation checks the local revision and source layout without a network fetch.

Use your actual installation paths. FMOD's root must contain `api/core/inc/fmod.hpp`,
`api/studio/inc/fmod_studio.hpp`, and these x64 files:

- `api/core/lib/x64/fmod_vc.lib` and `fmod.dll`.
- `api/studio/lib/x64/fmodstudio_vc.lib` and `fmodstudio.dll`.

Build **Core**, **Editor** and **Lux-Runtime** in the generated Visual Studio solution. Verify fresh
executable timestamps and that `fmod.dll` and `fmodstudio.dll` were copied beside both applications.
Asterwave is linked into the applications and has no separate runtime library. Open the project,
build its selected Studio platform, test event playback and focus changes, then export and launch
the standalone game independently of the editor.

Linux accepts the same environment variables with Linux paths. FMOD must provide linkable
`libfmod.so` / `libfmodstudio.so` and their shipping `.so.14` files; applications receive these
under `lib/`. Asterwave compiles from the same native source. Regenerate after changing roots.

Generation reports missing files or a mismatched source revision. SDK packages, private source
and generated paths stay local; their licence/provenance requirements remain unchanged.
Neither dependency setup nor these desktop profiles require a console SDK.

## Verification limits

Desktop verification combines native builds, the focused Asterwave adapter fixture, the full
FMOD suite, Studio target builds, and interactive playback/export checks. See
`tests/audio/README.md` for the separate commands and prerequisites. Disposable SDK-layout tests
check required files and paths with spaces; they do not establish native Windows compilation or
listening quality. An editor playback/listening pass and packaged-game launch still require the
platform SDK and host. Console work remains deferred.
