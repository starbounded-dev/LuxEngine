# Feature Specification: Soundwave Completion (Acoustic Kernel Phases 4–7)

**Feature Branch**: `features/soundwave-acoustic-kernel`

**Created**: 2026-11-01

**Status**: Draft

**Input**: User description: "Finish the Soundwave acoustic kernel plan (docs/AUDIO_ACOUSTICS_PLAN.md) Phases 4-7: (4) run the Soundwave occlusion and reverb solve on the GPU compute queue, dispatched from the render thread, read back with one update of latency, within 0.5 dB of the CPU reference; (5) air absorption, deeper bounces and caustic spread, authored and observable; (6) Soundwave takes over Play completely: the AudioOcclusionSource field leaves project files while old files still load, editor path overlay, docs rewritten; (7) Vercidium Audio is removed from build, link, deploy and tests so a clean checkout with no Vercidium builds, runs and exports."

Background: Phases 1–3 are done on this branch. Sound sources are muffled by walls, heard around
corners and through doorways, and rooms reverberate according to their shape — all computed by the
engine's own acoustic library on the CPU. Vercidium Audio is still linked, deployed and required by
the build, though it no longer drives what the player hears.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A clean checkout needs nothing from Vercidium (Priority: P1)

A game maker clones LuxEngine on Windows or Linux with no Vercidium SDK present, builds it, opens a
project, presses Play, hears occlusion and room reverb, and exports a game whose folder contains no
Vercidium library.

**Why this priority**: It removes a third-party install and an unresolved licence question from
every user of the engine, and it is the plan's end state. It does not depend on the GPU work.

**Independent Test**: On a machine without the Vercidium SDK folder, build every configuration,
run the audio test suites, and export a sample game; list the exported files.

**Acceptance Scenarios**:

1. **Given** a checkout with no Vercidium SDK folder, **When** the engine is built in every
   configuration on Windows and in Debug on Linux, **Then** every build succeeds with no warning
   that an acoustics SDK is missing.
2. **Given** an exported game, **When** its folder is listed, **Then** no Vercidium library is in it.
3. **Given** the audio test suites, **When** they run with no Vercidium SDK, **Then** they pass.

---

### User Story 2 - Old projects keep working on the new path (Priority: P1)

A game maker opens a project saved before this change, including one that chose Vercidium as its
occlusion source. It loads with no error, plays with Soundwave's numbers, and saves without the old
field.

**Why this priority**: A saved project that stops loading is data loss for the user.

**Independent Test**: Load project and runtime files written by each earlier format, play them,
save, and diff.

**Acceptance Scenarios**:

1. **Given** a project file that names the old occlusion source, **When** it is opened, **Then** it
   loads silently and plays on Soundwave.
2. **Given** that project, **When** it is saved, **Then** the old field is absent and every other
   audio setting is preserved.
3. **Given** a runtime build exported before this change, **When** the new player loads its scene,
   **Then** it loads.

---

### User Story 3 - The acoustic solve runs on the graphics card (Priority: P2)

During Play the occlusion and reverb numbers are computed on the GPU, so many sources can be
solved without costing the main thread, and the numbers stay within 0.5 dB of the CPU reference.

**Why this priority**: The user asked for GPU computation ("fast rapid and perfect calculations").
The CPU path already works, so this is a performance and capacity step, not a correctness one.

**Independent Test**: Run the same scenes through both the CPU reference and the GPU path and
compare every source's two gains; profile a frame.

**Acceptance Scenarios**:

1. **Given** a test scene with sources behind walls, around corners and in rooms, **When** the GPU
   and CPU paths solve it, **Then** every occlusion gain and reverb send agrees within 0.5 dB.
2. **Given** Play running, **When** a frame is profiled, **Then** no main-thread code waits for the
   GPU, and results arrive one acoustic update after they were requested.
3. **Given** a device with no dedicated compute queue, **When** Play starts, **Then** the solve runs
   on the queue the device has, or a clear editor error is shown; it never silently stops.
4. **Given** the editor minimized or Play paused, **When** frames stop rendering, **Then** the
   acoustic state reports that it is stalled rather than silently freezing.

---

### User Story 4 - Distance, bounces and focusing are authored and visible (Priority: P3)

A sound designer sets how much air absorbs high frequencies, how many reflection bounces are
followed, and how much reflected energy spreads, then sees each of these numbers per source in the
editor's diagnostics.

**Why this priority**: The user chose "full reach, now" (plan Decision 4), but each extra is a
refinement on top of a working system.

**Independent Test**: Change each setting, save, reload, and read the diagnostics for a source.

**Acceptance Scenarios**:

1. **Given** a source 60 m away across a courtyard, **When** it plays, **Then** it stays audible at
   the stated level and is not faded twice by distance.
2. **Given** bounces set from 1 to 4, **When** a source plays in a room, **Then** each setting gives
   a different, measurable result in the diagnostics.
3. **Given** each new setting, **When** the project is saved and reloaded, **Then** the value
   survives, and out-of-range values are rejected.

---

### User Story 5 - Designers can see the sound paths (Priority: P3)

In the editor a designer turns on a path overlay and sees the direct, reflected and opening paths
for a selected source, with their losses.

**Why this priority**: It explains the numbers; without it a muffled source is a mystery.

**Independent Test**: Select a source behind a wall and a source around a corner; toggle the overlay.

**Acceptance Scenarios**:

1. **Given** a selected source behind a wall, **When** the overlay is on, **Then** its direct path
   and the wall it crosses are drawn with the loss.
2. **Given** the overlay is off, **When** the scene renders, **Then** nothing extra is drawn and no
   extra rendering cost is paid.

### Edge Cases

- A scene with no acoustic geometry: sources are unoccluded and dry, with no error.
- A device without a dedicated compute queue (supported since `b9b57287`).
- Play paused, editor minimized, or a headless test that never renders a frame.
- Scene switch while a GPU solve is in flight: no freed buffer is read.
- A project file with an unknown future key: rejected with a clear error, as today.
- More sources than the per-update budget: they take turns, as today.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The build MUST succeed on Windows (Debug, Release, Dist) and Linux (Debug, Debug-AS)
  with no Vercidium SDK present, and MUST NOT reference it in any include, link step, deployed file,
  build option or test.
- **FR-002**: Exported games MUST NOT contain any Vercidium library.
- **FR-003**: Project and runtime files from every earlier format MUST load; the old occlusion
  source field MUST be ignored on load and omitted on save; every other setting MUST be preserved.
- **FR-004**: Occlusion gain and reverb send during Play MUST come only from Soundwave.
- **FR-005**: Room reverb MUST keep varying with the space (stone room ≠ corridor) after the change.
- **FR-006**: The shipped solve MUST run on the GPU, MUST agree with the CPU reference within
  0.5 dB on both gains, with a fixed summation order in validation builds.
- **FR-007**: The main thread MUST NOT wait on the GPU for acoustic results; results MUST arrive one
  acoustic update after the request.
- **FR-008**: The acoustic system MUST report when it is stalled because frames are not rendering.
- **FR-009**: The CPU reference MUST remain built in for tests and comparison.
- **FR-010**: Air absorption MUST be authored, saved and observable, and MUST NOT fade a distant
  source twice. The model (user, 2026-11-01): air absorption only
  darkens the occlusion and reverb energy along the path, mostly at high frequency; FMOD's own
  distance fade stays in charge of loudness over distance.
- **FR-011**: Reflection bounces up to the existing maximum MUST be selectable and each count MUST
  be measurable in diagnostics.
- **FR-012**: Reflected energy spread ("caustic spread") MUST use each surface's authored
  scattering value, and MUST be observable.
- **FR-013**: Every new setting MUST be editable in the editor, saved, range-validated, and shown
  in diagnostics.
- **FR-014**: The editor MUST offer a per-source path overlay that costs nothing when off.
- **FR-015**: Bank validation, the help page, the architecture, threading and rendering docs, and
  the audio manuals MUST describe Soundwave and stop describing Vercidium.
- **FR-016**: The scripting surface MUST stay the two existing floats (occlusion and reverb send).
- **FR-017**: The work MUST build on this branch's Soundwave history. `origin/feature/asterwave-audio` is
  superseded (user, 2026-11-01) and stays unmerged; nothing from it is folded in.

### Key Entities

- **Acoustic settings**: the project's audio block — rate, budgets, bounces, reflection depth,
  reverb rows, and the new air-absorption, scattering and bounce rows.
- **Acoustic result per source**: occlusion gain (low/high), reverb send, the paths behind them,
  and diagnostics.
- **Listener reverb measurement**: decay time, send, escaped fraction.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A clean checkout with zero Vercidium files builds, runs Play and exports on both
  platforms; the exported game ships at least one fewer native library than today.
- **SC-002**: 100% of project files in the sample project and test fixtures from every earlier
  format load and play.
- **SC-003**: Across the parity scenes, the GPU and CPU results differ by at most 0.5 dB for every
  source.
- **SC-004**: The acoustic solve adds no main-thread wait and fits within the existing audio budget
  in the performance validation protocol.
- **SC-005**: A 60 m courtyard source stays audible at the stated level with air absorption on.
- **SC-006**: Stone room and corridor still differ in reverb send by more than 0.1 at equal distance.

## Assumptions

- "Caustic spread" means reflected energy spreading according to each surface's existing
  Scattering value, not a new optical model. *(ultra-ponytail note: this is the smallest reading;
  if the user meant focusing by concave surfaces, that is a separate, larger feature.)*
- A device without a dedicated compute queue runs the compute work on its graphics queue (already
  supported); there is no shipped CPU fallback (plan's call, unanswered by the user).
- New tunables live beside the existing Kernel rows in the project audio block, and per-material
  absorption stays in the existing material override block (plan's call).
- Hearing results in Play requires launching the editor, which happens only with the user's OK.
- The § 3 disclosure chore gates shipping a release, not building or merging this work; removing
  Vercidium (Phase 7) also retires the open EULA engine-clause question.
- Linux verification uses the existing Linux CI / `tests/audio/run.py`; this Windows machine cannot
  build Linux locally.
