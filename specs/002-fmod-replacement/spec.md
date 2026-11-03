# Feature Specification: Full FMOD Replacement (banks included)

**Feature Branch**: `002-fmod-replacement` (work continues on `feature/native-audio-system`)

**Created**: 2026-11-02

**Status**: Draft — waiting on the owner's choices (plan.md § Decisions)

**Input**: "Go over all of the plan, I want to fully replace FMOD, replace the banks, make the banks,
remove the banks and replace it with something else. I have no idea what we should do, but give me
multiple choices on how to go forward."

## Context

Phase 9 of `docs/AUDIO_NATIVE_BACKEND_PLAN.md` made the built-in (miniaudio) backend the default.
FMOD still exists as an optional build. Its **banks** are FMOD's data layer: the Studio project
builds them, the engine loads and unloads them, export copies them, and scripts call
`Audio.LoadBank`. The native backend has no equivalent yet. Every clip is stored whole in the one
asset pack, and once a clip plays it stays in memory until the game quits.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — A game ships with no FMOD anywhere (Priority: P1)

A game maker clones LuxEngine, builds it, authors sounds in the editor, and exports a game. No FMOD
SDK, Studio app, bank, secret or document appears at any step.

**Why this priority**: it is the whole goal. Everything else supports it.

**Independent Test**: a fresh clone builds with default options, the sample opens and plays, export
runs, and a case-insensitive search for "fmod" finds only legacy readers and migration messages.

**Acceptance Scenarios**:

1. **Given** a fresh clone, **When** the owner builds and exports the sample, **Then** no step asks
   for or uses FMOD, and the exported game plays all its audio.
2. **Given** the repository, **When** it is searched for FMOD, **Then** only the readers that keep
   old projects loadable, and their warnings, remain.

### User Story 2 — Audio memory follows what the game needs (Priority: P1)

A game with a lot of music and ambience runs in bounded memory. Audio for a level is ready when the
level starts, without a hitch on first play, and is freed when it is no longer needed. This is what
banks did; it now works another way (see Decisions D1–D2).

**Why this priority**: today, memory only grows. A long game would run out of memory.

**Independent Test**: a test project with two scenes, each with its own music, shows audio memory
rising on load and falling after a switch. Streamed music does not hold its whole file in memory.

**Acceptance Scenarios**:

1. **Given** scene A with 50 MB of clips, **When** the game switches to scene B, which shares none
   of them, **Then** A's clips are released within one second.
2. **Given** a 5-minute streamed music clip in an exported game, **When** it plays, **Then** it does
   not hold the decoded track or the whole file in memory.
3. **Given** a Sound used on a level's first frame, **When** the level starts, **Then** it plays
   with no decode stall the frame-time graph can see.

### User Story 3 — Old projects still open and tell the user what to fix (Priority: P2)

A project authored with FMOD (scene GUIDs, Studio settings, `.fspro` / `.bank` assets, scripts that
call `LoadBank` or play `event:/` paths) opens without crashing. It lists every reference that needs
a Sound, and can strip the FMOD data in one step.

**Why this priority**: the owner's own scenes and anyone else's older projects.

**Independent Test**: open a copy of the pre-Phase-9 sample. Every FMOD-only reference is listed,
the clean-up action removes them, and the project saves without FMOD fields.

**Acceptance Scenarios**:

1. **Given** a scene with FMOD GUIDs and no Sounds, **When** it opens, **Then** the inspector and
   the validation report name each source, and Play logs an error for each.
2. **Given** a script that calls `Audio.LoadBank`, **When** scripts build, **Then** the outcome
   matches Decision D4 (a compile error with guidance, or a warning at run time).

### User Story 4 — Remaining parity gaps closed (Priority: P3)

The features the FMOD build had that native lacks are added: a music timeline view, dialogue clip
drag-and-drop with a play button, and a validation pass over the sample in the editor.

**Independent Test**: each feature is driven in the editor once (with the owner's OK to launch).

### Edge Cases

- A Sound is referenced by two scenes and played across the switch: it must not be freed mid-play.
- A script plays a Sound that no loaded scene references: it loads on demand, with a logged hitch
  warning in Debug.
- A packed clip is streamed while the pack file is also being read for meshes: reads must not block
  the audio thread.
- An old runtime pack (formats 17–29) with a bank manifest: the game runs and its banks are ignored
  with one warning.
- A project with no audio at all: nothing audio-related is written to the pack.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The engine MUST build, run and export with no FMOD code, SDK, option or CI secret.
- **FR-002**: The engine MUST load audio data per Decision D1, and MUST release it when nothing that
  is loaded references it.
- **FR-003**: Streamed clips in an exported game MUST be read in chunks from the export's files,
  never held whole in memory (Decision D2 picks the storage).
- **FR-004**: Every Sound MUST have a loading behaviour the editor shows and lets the user change.
  Decision D1 decides whether that is per Sound, per group, or automatic.
- **FR-005**: Old projects MUST keep loading. FMOD-only references MUST be reported, and a single
  action MUST remove them (Decision D3).
- **FR-006**: Script APIs that named FMOD concepts MUST follow Decision D4. Removed APIs MUST fail
  with a message that names the replacement.
- **FR-007**: The editor MUST NOT show FMOD settings, bank lists, bank builds or Studio pickers.
- **FR-008**: The Audio Debugger MUST show loaded clips, their memory and their loading state, in
  place of loaded banks.
- **FR-009**: The sample's FMOD Studio project folder MUST be removed or archived per Decision D6,
  with the clips it holds kept as engine assets.
- **FR-010**: FMOD-only tests MUST be ported to the native backend or deleted with a reason.

### Key Entities

- **Sound** (existing `.lsound`): gains a loading behaviour (D1).
- **Audio Group** (only with D1 options B or D): a named set of Sounds loaded and unloaded together.
- **Audio data in an export**: where the clips live and how they are read (D2).
- **Legacy FMOD data**: GUIDs, Studio settings, bank manifests and `.fspro` / `.bank` assets, kept
  only so old projects load and report.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A case-insensitive search for "fmod" in source, scripts, CI and docs finds only legacy
  readers, migration messages and point-in-time plan records.
- **SC-002**: A fresh clone builds, opens the sample, plays both demo scenes and exports, with
  default options, on Windows.
- **SC-003**: After a scene switch to a scene sharing no audio, audio memory drops to that scene's
  needs (within 10%), within one second.
- **SC-004**: A 5-minute streamed track in an export holds under 1 MB of audio memory while playing.
- **SC-005**: The first play of a preloaded Sound on a level's first frame adds no frame-time spike
  above 2 ms.
- **SC-006**: Opening the pre-Phase-9 sample lists every FMOD-only reference, and one action leaves
  zero.

## Assumptions

- No FMOD Studio project importer. This decision stands from the native plan: the owner re-authors
  by hand.
- Windows is verified now and Linux later (the owner's standing rule).
- Soundwave acoustics and the mixer, music, dialogue and budget work from Phases 4–8 are unchanged.
- Listening checks are done by the owner.
