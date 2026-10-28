# LuxEngine Native Audio Backend Plan

This plan replaces FMOD Core and FMOD Studio with an audio backend built into LuxEngine. Sounds are
authored in the LuxEngine editor, so neither the engine nor shipped games depend on a proprietary
audio SDK or on the FMOD Studio app. It is a planning document, not a description of what exists.
`.claude/docs/Architecture-LuxEngine.md § 2.10` describes what is actually built.

Written 2026-10-28 from `feature/asterwave-audio` at `1c07162`.

---

## Goal card

- **Goal:** replace FMOD Core and FMOD Studio with a built-in, permissively licensed backend
  (miniaudio) and editor-authored Sound assets.
- **Success criteria:**
  1. A clean checkout builds and runs the Editor and Lux-Runtime with no FMOD SDK. Together with
     the separate Asterwave-optional work, it needs no private dependency at all.
  2. The sample project's two events (`Fart`, `Music`) are Sound assets authored in the editor's
     panels, and they play. No step in authoring them uses another application.
  3. The music director, dialogue and subtitles, accessibility mixer, zones, snapshots and reverb,
     physics audio and engine occlusion all work on the native backend.
  4. The headless audio tests pass with no audio device.
  5. Runtime export ships the audio data and no FMOD libraries.
  6. FMOD code, its premake option and its CI secrets are gone.
- **Non-goals:**
  - HRTF/binaural rendering.
  - Replacing Asterwave.
  - Consoles.
  - An FMOD project importer.
  - A node-based sound graph editor.
  - Physics audio for Box2D.
- **Constraints:**
  - Every phase builds, runs and could ship.
  - Existing `.luxproj`, `.luxscene` and table assets still load. FMOD references become visible
    migration warnings, never silent loss.
  - The audio API stays main-thread only.
  - Must work under both threading policies (Linux defaults to single-threaded).
  - No new install for game makers.
- **Built into the editor:** "I want this directly implemented into LuxEngine, not a separate app;
  it's in the panels." Every authoring task happens in LuxEngine editor panels, with no external
  tool:
  - importing audio;
  - building sounds;
  - the mix and snapshots;
  - music tempo and markers;
  - dialogue clips;
  - auditioning;
  - meters.
- **The user's decisions (2026-10-28):**
  - Use editor-built Sound assets.
  - Keep FMOD behind a build switch until the native backend reaches parity.
  - Parity is required for music, dialogue, accessibility, and zones/snapshots/reverb.
  - Only the sample project needs migrating.

## Decisions this plan is built on

| Decision | Choice | Consequence |
|---|---|---|
| Authoring model | **User:** everything is authored in editor panels:<br>• Sound assets (`.lsound`) in a Sound Editor panel<br>• the mix in an Audio Mixer panel, saved with the project<br>• no separate app | The FMOD Studio app is no longer needed. LuxEngine owns containers, parameters and the mixer. |
| Editor surfaces | A Sound Editor panel and an Audio Mixer panel, both dockable. They follow the existing `MaterialEditorPanel` pattern: registered with `AddPanel`, and the Sound Editor opens from the Content Browser through `RegisterItemActivateCallbackForType`. | One familiar workflow. No authoring stays in Project Settings except project-wide defaults. |
| Transition | **User:** a premake switch, `--audio-backend=fmod\|native`. FMOD stays the default until Phase 9. | Two implementations of the same engine-facing classes exist until Phase 10. Shared tests run on both. |
| Parity before deletion | **User:** music, dialogue, accessibility, zones/snapshots/reverb. | FMOD is deleted only after Phase 9 verifies all four. |
| Migration | **User:** sample project only, re-authored by hand. | No `.fspro` importer. FMOD references in other projects show the existing legacy-audio style warning. |
| Library | miniaudio 0.11.25 (public domain or MIT-0). | One vendored header with no runtime library to deploy. See the Research brief. |
| Rejected libraries | SoLoud (zlib) has had no release since 2020. OpenAL Soft is LGPL, which breaks the permissive rule. Steam Audio has no mixer or decoder. | Steam Audio is listed as an HRTF follow-up. |
| Vorbis | `stb_vorbis` from miniaudio `extras/` (public domain or MIT). | `.ogg` keeps working. Opus is not added. |
| Reverb | miniaudio's `extras/nodes/ma_reverb_node` (verblib, Freeverb-based). Its licence is checked in Phase 0. | If the licence is not permissive, write a Freeverb node (Freeverb itself is public domain). |
| Compressor and meters | Written in-engine as small custom miniaudio nodes. | miniaudio has neither. Both are tested with offline rendering. |
| Backend selection | Compile time: one header set, two `.cpp` sets chosen by premake. | No virtual dispatch on hot paths, and deleting FMOD is a folder delete. |
| Event references | `AudioEventRef` gains `AssetHandle Sound`. `Guid` is kept as the FMOD and legacy reference. | One field type covers both backends during the transition, and scenes stay openable in either build. |
| Bus addressing | Keep `bus:/` paths (`bus:/`, `bus:/SFX`). | C# `Audio.SetBusVolume` and the accessibility bus mappings keep working unchanged. |
| Occlusion and reverb on a voice | The native backend applies `AudioEventAcoustics` as a per-voice low-pass, gain and reverb send. | Authors no longer need an `Occlusion` parameter in every event. This removes a whole class of validation warnings. |
| Audio data in exports | Pack the original compressed bytes into the asset pack and stream through a miniaudio VFS over the pack. | Today the pack stores only the path (`AudioAssetSerializer::SerializeToAssetPack`). Fallback: copy the files beside the pack. |

---

## Part 0 — Where we are

Everything below was read or grepped in this session. Nothing was verified at runtime here: the
FMOD SDK is not in this container.

| Capability | State | Evidence |
|---|---|---|
| FMOD headers are confined to `Core/Source/Lux/Audio` | ✅ Read | 11 `.cpp`/`.h` files include `fmod*` headers. Everything else names FMOD only in comments or strings. |
| Audio headers are backend-neutral | ❌ No | `AudioEngine.h` includes `<fmod_common.h>` and exposes `FMOD_RESULT LockBusChannelGroup(FMOD::Studio::Bus*)`, `GetEngine()` and `GetStudioSystem()`. `AudioEventInstance.h` stores `FMOD::Studio::EventInstance*`. |
| Code outside the playback core that reaches FMOD | ⚠️ 7 call sites | `GetStudioSystem` / `GetEngine` / `LockBusChannelGroup` are called from `AudioAccessibilityMixer.cpp`, `AudioPerformance.cpp`, `AudioEventInstance.cpp`, `AudioFileUtils.cpp`, `AudioListener.cpp`, `AudioSourcePlayback.cpp` and `AudioScriptBindings.cpp:798`. |
| Engine-facing playback API | ✅ Read | `AudioEventInstance` (about 35 methods): Start/Stop, volume/pitch, parameters and labels, timeline position, snapshot intensity, programmer sound, callbacks, `SetAcoustics`. Directors and scripting use only this class and `AudioEngine`. |
| Event references | ✅ Read | `AudioEventRef { Guid, Path, BankName }` is held by:<br>• `AudioSourceComponent`, `AudioZoneComponent` (`AmbienceEvent`, `Snapshot`), `MusicDirectorComponent`, `AudioSurfaceComponent` overrides<br>• `DialogueLine`, `AudioSurfaceSounds`, `AudioValidation`<br>Its YAML is written separately in `SceneSerializer.cpp`, `DialogueTable.cpp` and `AudioSurfaceTable.cpp`; there is no shared helper. |
| Raw audio asset | ⚠️ Metadata only | `AssetType::Audio` (`.wav`, `.ogg`) → `AudioFile` (duration and format, read through FMOD in `AudioFileUtils`). The asset pack stores only the file path, not the audio. |
| Bank pipeline | ✅ Read | `AudioBankBuilder` (`fmodstudiocl`), `AudioBankManifest` (runtime project format 17+), `RuntimeExport::PrepareAudioBanks`, and the Studio fields in `ProjectAudioSettings` (`StudioProjectPath`, `StudioBankOutputPath`, `StudioPlatform`, desktop profiles, live update). |
| Engine occlusion without Asterwave | ❌ Broken | `Scene::UpdateAudioOcclusion` runs only inside the `if (m_RaytracedAudioScene)` block (`Scene.cpp` ~872–920). `SyncAudioGeometry` returns early unless Asterwave is running. This is fixed by the Asterwave-optional work (prerequisite). |
| Zone reverb fallback | ✅ Read | `UpdateAudioZones(ts, raytracedAmbienceValid)` already falls back to zones when Asterwave has no ambience. |
| Music timeline | ⚠️ FMOD-sourced | `MusicDirector` (`MusicSync { Immediate, NextBeat, NextBar, NextMarker, NextSection }`) uses FMOD's timeline beat and marker callbacks through `AudioEventInstance::DrainTimelineNotifications`. |
| Dialogue audio | ⚠️ FMOD-specific | `DialogueTranslation { Text, SpeakerName, AudioKey }`. `AudioKey` is an FMOD audio-table key played through `SetProgrammerSound`. |
| Accessibility and performance | ⚠️ FMOD DSP | Uses FMOD's compressor, channel mix and fader DSPs, bus channel-group locks, `getCPUUsage` and metering. |
| Headless tests | ✅ Read (not run) | `tests/audio/run.py` needs the FMOD SDK, `fmodstudiocl` and `AuthorFixture.js` fixtures, and renders with FMOD's no-sound output. |
| CI | ⚠️ | Windows only (`windows-2025`). It needs `AUDIO_SDK_TOKEN` (FMOD) and `ASTERWAVE_TOKEN`. The push trigger is `features/**`, which doesn't match `feature/*` branches. |
| Sample content | ✅ Read | `SampleProject.fspro` (`fart-01.wav`, `Music_1.mp3`), `FMODDemo.luxscene`, `FmodAudioDemo.cs`, `FMOD_SETUP.md`. |
| Build toggles | ✅ Read | `premake5.lua` `newoption` plus a `scripts/BuildOptions.py` `OPTIONS` entry. `--fmod` and `--raytraced-audio` are accepted but have no effect. |

---

## Part 1 — Design

```
 Editor panels:              Audio Mixer panel                 .lsound (Sound asset)
 Sound Editor ──edits──►     buses · effects · snapshots       clips · containers · params · tempo
 Content Browser (import)    (saved in the project)
                                     │                                   │
                                     ▼                                   ▼
 ┌───────────────────────── AudioEngine (static facade) ─────────────────────────────┐
 │  native:  ma_engine (device thread) · node graph · resource manager (Lux::Thread)  │
 │  fmod:    unchanged until Phase 10                                                  │
 └──────┬──────────────────────┬──────────────────────┬───────────────────────────────┘
        ▼                      ▼                      ▼
 AudioEventInstance      Mixer graph              Offline render (noDevice) for tests
 voice = ma_sound(s)     bus nodes → master       and validation
 + LPF/gain/send nodes   reverb returns · meters
        ▲
 Scene / directors / scripting — unchanged callers
```

**Ownership.**
- `AudioEngine` keeps one `Scope<Impl>` that owns the `ma_engine`, the resource manager and the
  bus graph.
- An `AudioEventInstance` (still a `Ref`) owns its voice nodes. A Sound asset is a `Ref<Sound>`
  (**NEW**), immutable while instances use it. An edit makes a new revision, and that revision
  applies to the next instance.
- Teardown order: stop voices → detach → uninit voices → uninit buses → uninit engine. The existing
  `GetEventGeneration()` check stays as the guard against stale wrappers.

**Threads.**

| Code | Thread | Rule |
|---|---|---|
| Every `AudioEngine` and `AudioEventInstance` call | Main | Unchanged contract (`Threading.md`). |
| miniaudio device callback (node graph) | Vendor audio thread | Same status as FMOD's mixer thread today. It never touches the ECS, Coral or assets. End-of-sound and meter data cross to the main thread through the existing mutex-protected mailboxes. |
| Resource-manager decoding and streaming | `Lux::Thread` "AudioStreaming" (**NEW**) | The resource manager is configured with 0 internal job threads and pumped from this thread, so it is named and profiled (`LUX_PROFILE_THREAD`). |
| Node attach and detach | Main | Done only when a voice starts or ends, never per frame. `ma_node_detach_output_bus` blocks until the audio thread finishes with that bus. Voices are pooled and paused with `ma_node_set_state` instead of churned. |

Both `ThreadingPolicy` values behave the same here: the audio thread belongs to miniaudio, not to
the render-thread policy.

**Engine fit.**

| Concern | Answer |
|---|---|
| System | Audio (Core). It depends on Asset, Project and Core. Scene owns playback (unchanged). No Renderer dependency. |
| Renderer | None. Zone and portal lines already go through `FrameRenderPacket::AudioZoneLines`. |
| Serialization | New Sound asset YAML and pack serializer. An `AudioEventRef.Sound` key (missing → 0). `ProjectAudioSettings::Mixer` in YAML plus runtime project format **26**; older formats use the default mixer. An asset-pack version bump for audio bytes; old packs load but report that a re-export is needed. |
| Editor | **NEW** panels:<br>• Sound Editor (`Panels/SoundEditor/`)<br>• Audio Mixer (`Panels/AudioMixerPanel`)<br>Content Browser: drag-and-drop audio import, "Create Sound", waveform thumbnails. The inspector's event picker lists Sound assets and has an "Edit" button that opens the Sound Editor. The existing Audio Debugger stays. Every UI phase carries an ImGui verification step. |
| Scripting | The C# API keeps its shape. `Audio.Play`/`PlayOneShot` accept a Sound handle or asset path. `event:/` paths and `Audio.LoadBank` log a migration error on the native backend and are removed in Phase 10. |
| Runtime and Dist | miniaudio is static. Audio bytes travel in the pack. Dist has no Sound editor and no ImGui. |
| Linux | miniaudio loads ALSA, PulseAudio or JACK with `dlopen` at runtime, so there is no build-time library. Desktop Linux already has PulseAudio or PipeWire. |
| Build | Add `Core/vendor/miniaudio` through `Dependencies.lua`, with one implementation unit (`MINIAUDIO_IMPLEMENTATION`). Add `--audio-backend` as a `newoption` plus an `OPTIONS` entry. New files need project regeneration. |
| Size | Measured in Phase 0. The FMOD DLLs/.so files stop being deployed in Phase 10. |

---

## Part 2 — Phases

Each phase leaves both builds compiling and the editor running. Until Phase 9, CI keeps building
FMOD as the default and adds a native build job from Phase 2 on.

### Phase 0 — Prerequisites and spike

**Goal:** the plan's foundations are proven or killed before any engine code changes.

**Changes**
- **Prerequisite (separate work, already scoped):** make Asterwave optional and run engine occlusion
  and `SyncAudioGeometry` outside the Asterwave block. Without this, success criterion 1 cannot hold.
- `Core/vendor/miniaudio/` — **NEW:**
  - `miniaudio.h` 0.11.25;
  - `extras/stb_vorbis.c`;
  - `extras/nodes/ma_reverb_node` and verblib, if licensed permissively.
- `tests/audio/native/` — **NEW:** a CMake harness that renders WAV, OGG, MP3 and FLAC through
  `ma_engine` with `noDevice = MA_TRUE` and `ma_engine_read_pcm_frames`.

**Verification**
- The offline render matches each decoded file (bit-exact for WAV and FLAC, within tolerance for lossy formats).
- An editor smoke check plays one clip on WASAPI (Windows) and PulseAudio (Linux).
- Record:
  - Editor binary size before and after.
  - Engine init time.
  - Audio-thread CPU for 64 3D voices, each with a low-pass, through 4 buses and a reverb.
- Read the licence header of every vendored file.

**Exit criteria:** numbers recorded in this plan; licences confirmed; go or no-go.

**Rollback:** delete the vendored folder.

### Phase 1 — Make the audio headers backend-neutral (FMOD only)

**Goal:** no FMOD type appears in a header outside `Audio/FMOD/`, and behaviour is unchanged.

**Changes**
- `Audio/AudioEngine.h`:
  - Remove `<fmod_common.h>`, `GetEngine`, `GetStudioSystem` and `Lock/UnlockBusChannelGroup`.
  - Add the neutral operations their callers need: bus gain and mute, master effects hook, meters,
    `IsInitialized`.
- `Audio/AudioEventInstance.h` — hold a backend-private `Scope<Impl>` instead of the FMOD pointer.
- `Audio/FMOD/` — **NEW** folder holding the current FMOD `.cpp` files and a private
  `FMODInternal.h`. Moving files means project regeneration.
- `Scripting/AudioScriptBindings.cpp:798` — use `AudioEngine::IsInitialized()`.

**Playbook:** none applies (refactor).

**Verification**
- Windows and Linux debug builds succeed.
- `tests/audio/run.py` is all green, with the same counts as before.
- `grep -rl "fmod" Core/Source --include=*.h` finds only `Audio/FMOD/`.

**Docs:** Architecture 2.10, the paragraph on the FMOD boundary.

**Exit criteria:** tests green; grep clean.

**Rollback:** revert the commit.

### Phase 2 — Native vertical slice behind `--audio-backend=native`

**Goal:** in a native build, a Sound asset assigned to an `AudioSourceComponent` plays in 3D,
follows the listener, is muffled by engine occlusion, and can be fired from C#.

**Changes**
- `premake5.lua`, `scripts/BuildOptions.py`, `Dependencies.lua`:
  - Add `--audio-backend` (default `fmod`).
  - Native skips `ValidateAudioSDK()` and the FMOD links.
- `Audio/Native/` — **NEW:**
  - `AudioEngine` (device, master bus, listeners and weights, focus mute, stats);
  - `AudioEventInstance` (one clip, volume, pitch, loop, 2D/3D, attenuation, priority, pause
    layers, `SetAcoustics` → low-pass, gain and send);
  - `AudioFileUtils` (metadata through `ma_decoder`).
- `Audio/Sound.h/.cpp` — **NEW** asset, minimal form: one clip, bus path, volume, pitch, loop,
  spatial settings.
- `Audio/AudioEventRef.h` — add `AssetHandle Sound`.
- `Audio/AudioEventRefSerializer.h/.cpp` — **NEW** shared YAML helper. `SceneSerializer`,
  `DialogueTable` and `AudioSurfaceTable` all switch to it, writing both `Guid` and `Sound`.
- `ImGui/AudioWidgets.cpp` — the picker lists Sound assets in native builds.
- `tests/audio/native/` — source and listener tests:
  - pan from position;
  - distance attenuation;
  - occlusion gain;
  - pause layering.

**Playbook — new asset type (all steps):**
1. `AssetType::Sound` and its string helpers.
2. The `Sound` class.
3. A YAML serializer plus a pack serializer.
4. Register in `AssetImporter`.
5. `.lsound` in `AssetExtensions`.
6. A Content Browser icon.

Do not reuse the legacy `SoundConfig` (`.lsoundc`) type.

**Thread and lifetime:**
- Main thread throughout.
- End-of-sound comes from the audio thread and goes through the existing notification mailbox.
- Voices are released before `AudioEngine::Shutdown`.

**Verification**
- Both backends build.
- Native tests pass headlessly.
- In the native editor: the sample `fart-01.wav` as a Sound plays on Play, pans as the camera
  moves, and is muffled behind a mesh-collider wall.
- `Audio.PlayOneShot(handle)` works from C#.
- A scene saved in the native build opens in the FMOD build with its `Guid` intact, and the other
  way round.

**Docs:** Architecture 2.10 (backend switch, Sound asset), `Building.md` (toggle), `Threading.md`
(audio thread).

**Exit criteria:** all of the above; the FMOD build's tests are unchanged.

**Rollback:** the toggle defaults to `fmod`; revert the native folder.

### Phase 3 — Sound authoring breadth and the Sound editor

**Goal:** Sounds can express what the sample, surfaces and ambience need, and are authored and
auditioned in the editor.

**Changes**
- `Sound` model:
  - **Containers:** Clip, Random (weights, avoid-repeat), Sequence, Layer, Switch (by labelled
    parameter), nested.
  - **Clip settings:** start offset and loop region per clip.
  - **Randomization:** volume and pitch ranges.
  - **Playback limits:** instance limit with steal policy (oldest, quietest, none), cooldown.
  - **Data:** a stream-or-decode flag.
  - **Parameters:** continuous ranges or labels, with curves on volume, pitch, low-pass,
    high-pass, send and layer volume.
- `Editor/Source/Panels/SoundEditor/SoundEditorPanel` — **NEW** dockable panel, modelled on
  `Panels/MaterialEditor/MaterialEditorPanel`. It goes through the editor panel playbook
  (`EditorPanel`, `AddPanel` under `PanelCategory::View`, menu toggle, regenerate). It contains:
  - a container tree (add, remove, reorder, nest);
  - a clip list with a waveform view, start offset and loop-region handles;
  - property fields and a parameter curve editor;
  - transport (play, stop, loop) with a live parameter slider to hear curves change;
  - an audition voice owned by the panel, not by a scene, so it works in edit mode.
  - Edits use the editor's undo, and save the `.lsound` as other assets do.
- Opening it:
  - double-click a `.lsound` in the Content Browser (`RegisterItemActivateCallbackForType`);
  - the inspector's "Edit" button next to any Sound picker.
- Content Browser:
  - Dropping `.wav`, `.ogg`, `.mp3` or `.flac` files imports them as `AudioFile` assets.
  - "Create Sound" from one file or a selection makes a Random container of those clips.
  - Audio files and Sounds get waveform thumbnails (**NEW** thumbnailer, like
    `MaterialThumbnailer`).

**Verification**
- Physics audio tests are ported to native and pass (`Surface` switch; `Speed`, `Weight` and
  `Impulse` curves).
- Offline-render checks for random weights, avoid-repeat, steal policy and parameter curves.
- **ImGui:** every Begin/Push closed on every path, stable `PushID` per container node and clip,
  `###` on renamable labels. Drive every container type, add, remove, reorder, audition, and undo
  in the editor with no ID-conflict popup.

**Docs:** Architecture 2.10 (Sound model).

**Exit criteria:** tests pass; the sample `Fart` re-authored as a random container of one clip
with pitch variation.

**Rollback:** the panel and the model extensions are additive.

### Phase 4 — Mixer: buses, effects, snapshots, zones and reverb

**Goal:** the mix is authored in an Audio Mixer panel; zones, snapshots and reverb work natively.

**Changes**
- `ProjectAudioSettings::Mixer` — **NEW:**
  - A bus tree with `bus:/` paths. The default is `bus:/`, `Music`, `SFX`, `Dialogue`, `Ambience`,
    matching the default accessibility mappings.
  - Per bus: volume, mute, an effect chain (low-pass, high-pass, peaking EQ, delay, reverb,
    compressor) and reverb return buses.
  - Snapshots: named property overrides with priority. Intensity 0–1 interpolates from the base
    mix; the highest-priority active snapshot wins each property.
- YAML plus runtime project format 26 (bounded, length-prefixed, after format 25). Formats 16–25
  load the default mixer.
- Native `SetBusVolume`, `SetBusMuted`, VCA equivalents (bus groups), global parameters.
- Zone snapshots map to native snapshot IDs (`AudioSnapshotRef` gains the `Sound`-style handle
  slot). `ReverbSend` drives the voice → reverb-return send for both Asterwave and zone reverb.
- `ZoneReverbMode` is unchanged.
- **Precedence rule (from Unity's documented pitfall):** a value set from script on a bus property
  overrides snapshots until it is cleared. `ClearBusVolume` is **NEW**.
- `Editor/Source/Panels/AudioMixerPanel` — **NEW** dockable panel (View → Audio Mixer). It edits
  `ProjectAudioSettings::Mixer` with undo and saves with the project. It shows:
  - a fader strip per bus: volume, mute and solo, live peak/RMS meters during Play;
  - the bus tree, with add, rename, reparent and delete;
  - each bus's effect chain, with add, reorder and bypass;
  - reverb returns;
  - a snapshot list: edit overrides, and preview each snapshot's intensity live in edit mode or
    Play.
- Project Settings keeps only a link to the panel.

**Verification**
- Zone tests ported: ambience enter and exit, snapshot coalescing, intensity blending, portal
  transfer.
- An offline render measures the reverb decay of the built-in reverb at three settings.
- Audio Mixer panel ImGui verification:
  - stable `PushID` per bus and effect;
  - `###` on renamable bus labels;
  - every scope closed on every path.
  Drive bus add, remove, rename and reparent, effect reorder and bypass, snapshot editing and
  preview, and meters in Play.

**Docs:** Architecture 2.10 (zones and snapshots, project format 26); a `Threading.md` note on
meters.

**Exit criteria:** zone tests green on native.

**Rollback:** the mixer block is additive; an old project gets the default mixer.

### Phase 5 — Accessibility, focus, budgets, meters, validation

**Goal:** the accessibility mixer and the performance and validation tools work natively.

**Changes**
- Native `AudioAccessibilityMixer`:
  - category bus gains;
  - mono downmix and compressor (**NEW** node) on master;
  - narration ducking by bus-gain automation.
- `AudioPerformance`:
  - voice counts and the virtual voices from Phase 2's culling;
  - audio-thread time measured inside the device callback;
  - resource-manager memory;
  - per-bus peak and RMS from a **NEW** meter node, published atomically and read at 4 Hz on the
    main thread.
- The global voice budget virtualizes by priority, reusing `AudioSourcePlayback`'s timeline
  advance.
- `AudioValidation` checks Sounds instead of banks: missing or undecodable clips, unknown buses,
  unused Sounds, disk size. An offline engine replaces the NOSOUND Studio system.

**Verification**
- Accessibility tests ported: gains independent of authored volume, mono, three compression
  presets, ducking.
- Performance meters checked against a known sine render.
- Validation run on the sample project.

**Docs:** Architecture 2.10 (accessibility, budgets, validation).

**Exit criteria:** all of the above green on native.

**Rollback:** per-file revert; FMOD is unaffected.

### Phase 6 — Music director

**Goal:** beds, stingers, State, Intensity and `Layer_<name>`, and beat/bar/marker/section sync
work natively, with transitions more precise than today.

**Changes**
- Sound tempo map — **NEW:** BPM, time signature, beat offset, named markers (`Section:` prefix
  kept).
- Sound Editor timeline view for music Sounds:
  - waveform with a bar/beat grid;
  - draggable markers and tempo settings;
  - play from cursor and loop the selected region;
  - a test transition button that plays the quantized switch to another bed;
  - this view gets its own ImGui verification pass.
- Native `DrainTimelineNotifications` computes beats, bars and markers from the voice's PCM cursor
  on the main thread.
- Quantized transitions schedule the next bed with `ma_sound_set_start_time_in_pcm_frames` on the
  engine clock. Today's frame-quantized switch becomes sample-accurate.

**Verification**
- Music tests ported (every `MusicSync` boundary, stale batches, reentrancy, pause, reload).
- An offline render shows the new bed's first sample within 1 sample of the target beat.

**Docs:** Architecture 2.10 (music).

**Exit criteria:** music tests green on native.

**Rollback:** the FMOD path is unchanged.

### Phase 7 — Dialogue and subtitles

**Goal:** dialogue lines play per language without FMOD programmer sounds.

**Changes**
- `DialogueTranslation` gains `AssetHandle Clip` (**NEW** key). `AudioKey` is kept as legacy.
- A line's event is a Sound with a **NEW** "clip slot". `SetProgrammerSound` becomes
  `SetClipOverride(AssetHandle)`, checked before `Start`. Subtitle duration comes from the clip
  length.
- The existing dialogue line editor (in Project Settings today) gains a clip picker per language, accepts audio files dropped
  from the Content Browser, and gets a play button per line.

**Verification**
- Dialogue tests ported: queue, interrupt, drop modes, barks and cooldowns, locale fallback,
  subtitle timing, cancellation on teardown.
- ImGui verification of the per-language clip pickers.

**Docs:** Architecture 2.10 (dialogue).

**Exit criteria:** dialogue tests green on native.

**Rollback:** the legacy key keeps the FMOD build working.

### Phase 8 — Runtime export and Lux-Runtime

**Goal:** an exported native game plays its audio from the pack with no loose files and no FMOD.

**Changes**
- `AudioAssetSerializer::SerializeToAssetPack` writes the original file bytes (asset-pack version
  bump). Old packs load and report that a re-export is needed.
- A **NEW** miniaudio VFS over the pack's file regions, so streamed Sounds read from the pack.
- `RuntimeExport`: under native, skip `PrepareAudioBanks`, write the mixer block, deploy no FMOD
  libraries.
- `Project::LoadRuntime` initializes the native engine and the mixer before scenes.

**Verification**
- Export the sample, then run `Lux-Runtime` in Release and Dist. `Fart` and the music play, and
  F10 accessibility works (non-Dist).
- The export folder contains no `fmod*` files.

**Docs:** Architecture 2.10 (runtime exports), `Building.md`.

**Exit criteria:** runtime verified on Windows and Linux.

**Rollback:** the FMOD export path is unchanged.

### Phase 9 — Flip the default and migrate the sample

**Goal:** native is the default and the sample project uses no FMOD.

**Changes**
- `--audio-backend` defaults to `native`.
- Sample project:
  - `Fart` and `Music` as Sound assets;
  - `FMODDemo.luxscene` becomes `AudioDemo.luxscene`;
  - `FmodAudioDemo.cs` becomes `AudioDemo.cs`;
  - `FMOD_SETUP.md` is replaced by a short authoring note.
- CI:
  - The native job becomes the main build, needing no FMOD secret.
  - FMOD becomes an optional job.
  - Fix the push trigger (`features/**` → `feature/**`).
- A scene with an FMOD `Guid` and no `Sound` shows an inspector migration warning and an error on
  Play, using the existing `LegacyAudio` pattern.

**Verification**
- A fresh clone builds with no FMOD SDK.
- The full test suite passes.
- Open the sample, play every demo scene, and export.

**Docs:** `CLAUDE.md` (audio section and the Product Principle's list of required software),
Architecture 2.10, `Building.md`.

**Exit criteria:** the success criteria hold except criterion 6.

**Rollback:** flip the default back.

### Phase 10 — Delete FMOD

**Goal:** no FMOD code, option, library, secret or document remains.

**Changes**
- Delete:
  - `Audio/FMOD/`, `AudioBankBuilder`, `AudioBankManifest` writing (the reader stays for formats
    17–25, with a warning);
  - the `.fspro` and `.bank` asset types;
  - the `--fmod` / `--audio-backend` options and `ValidateAudioSDK`;
  - `scripts/ci/StageAudioSDKs.py` and the FMOD steps in `main.yml`;
  - `Audio.LoadBank` and `event:/` path support in C#.
- `ProjectAudioSettings` Studio fields: read and ignored with a one-time info log.
- `AudioEventRef.Guid` becomes a legacy reference, kept for migration warnings.
- Delete the FMOD test host (`AudioTestHost.h`) and `AuthorFixture.js`.

**Verification**
- `grep -ri fmod` finds only the legacy readers and the migration messages.
- Full test suite, and a clean build on both platforms.

**Docs:** every shared doc touched above. `AGENTS.md` and `.agents/skills` only if they mention
FMOD.

**Exit criteria:** success criterion 6.

**Rollback:** revert. Phase 9 still ships.

---

## Part 3 — Verification beyond the phases

- **Offline rendering is the test oracle.** `ma_engine` with `noDevice` renders deterministic PCM,
  so tests assert on samples, gains and timing instead of "an FMOD call returned OK". This makes
  the native suite stronger than today's.
- **Shared behaviour tests run on both backends** until Phase 10, so the backends cannot drift
  silently.
- **Listening checks** on Windows and Linux at the end of Phases 2, 4, 6 and 8. These cannot be
  done in a headless container; the owner does them.

## Part 4 — Risks

| Risk | Likelihood | Detection and mitigation |
|---|---|---|
| Voice churn stalls the main thread on node detach (spinlock) | Medium | Phase 0 stress number; pooled voices; `ma_node_set_state` instead of detach. |
| Built-in reverb sounds worse than FMOD's | Medium | Phase 4 decay measurement plus listening. The reverb node sits behind the bus effect interface, so it can be replaced. |
| Self-written compressor or meter is wrong | Low | Offline-render tests against analytic signals. |
| Streaming from the asset pack through a VFS is complex | Medium | Phase 8 fallback: copy the audio files beside the pack. |
| Scope: 11 phases, several months | High | Each phase ships; Phases 6–7 can reorder; FMOD stays default until Phase 9. |
| The Asterwave-optional prerequisite slips | Medium | The native backend still works with Asterwave present; only criterion 1 waits. |
| No audio device in CI | Certain | Headless `noDevice` tests; listening checks are manual. |

## Part 5 — Open questions

- **verblib licence:** read `extras/nodes/ma_reverb_node` and verblib headers (Phase 0).
- **`docs/AUDIO_*.md` guides** (zones, music, dialogue, surfaces, accessibility) describe FMOD
  authoring: rewrite them per phase or retire them in Phase 10? Owner's call.
- **Default bus layout names:** confirm `Music`, `SFX`, `Dialogue`, `Ambience` (Phase 4).
- **HRTF:** follow-up with Steam Audio (Apache-2.0), which also offers raycast occlusion,
  transmission and reflections. It is a candidate legitimate replacement for Asterwave's role. Not
  in this plan.
- **Opus:** add only if a project needs it.

**What would invalidate the plan:** if Phase 0 finds miniaudio's node graph cannot carry 64 3D
voices with filters through 4 buses and a reverb within budget on the audio thread, revisit the
library choice before Phase 2.

## Part 6 — Research notes

- miniaudio:
  - v0.11.25 (2026-03-04), public domain or MIT-0.
  - Backends: WASAPI/DirectSound/WinMM and ALSA/PulseAudio/JACK.
  - Decoders: WAV, FLAC, MP3 built in; Vorbis and Opus as custom decoding backends.
  - Node graph: sound groups; LPF, HPF, BPF, notch, peak and shelf filters; splitter, delay.
  - Spatializer: attenuation models, doppler, cones, multiple listeners. No HRTF or occlusion.
  - Scheduling and streaming: scheduled start and stop in PCM frames; fades; async resource
    manager streaming in 1 s pages; job threads configurable (0 = the app pumps).
  - Offline: `noDevice` with `ma_engine_read_pcm_frames`.
  - Thread safety: only graph reads are lock-free; detach blocks; end callbacks run on the audio
    thread.
  - Extras: reverb, vocoder, channel combiner/separator nodes.
  - Sources: [repository](https://github.com/mackron/miniaudio), [manual](https://miniaud.io/docs/manual/index.html).
- SoLoud: zlib, last stable release 2020. Rejected. [downloads](https://solhsa.com/soloud/downloads.html)
- OpenAL Soft: LGPL. Rejected under the permissive-licence rule.
- Steam Audio: Apache-2.0 since v4.5.2 (February 2024); HRTF, occlusion, transmission,
  reflections. Follow-up. [Phoronix](https://phoronix.com/news/Steam-Audio-SDK-Fully-Open)
- Godot:
  - `AudioStreamRandomizer` is a weighted pool with pitch and volume variation.
  - `AudioStreamInteractive` is clips plus a transition table (immediate, next beat, next bar,
    clip end) with fades.
  - These map onto Sound containers and `MusicSync`.
  - Source: [docs](https://docs.godotengine.org/en/4.3/classes/class_audiostreaminteractive.html).
- Unity AudioMixer:
  - Snapshots blend as weighted mixtures.
  - A parameter set from script stops following snapshots until cleared. That is a confusing
    precedence, so Phase 4 states its rule explicitly.
  - Source: [AudioMixer](https://docs.unity3d.com/ScriptReference/Audio.AudioMixer.html).
