# Lux history review and corrected baseline

The completed [LuckyEngine implementation comparison](C:/Git/luxengine/docs/LuckyEngine-Comparison.md) builds on this baseline, revalidates the remaining candidates against pinned current source, and replaces the original stale rankings.

Reviewed October 23, 2026. Lux is the user's independently developed game engine, deeply inspired by Hazel; it is not a Hazel fork. LuckyEngine is a separate Hazel fork used as an engineering reference.

The earlier Lucky comparison inspected the checkout at `366345f` from May 9, 2026. That is substantially behind Lux's live development history. Its missing-feature rankings are withdrawn. Current work must be assessed from pinned current source and its history.

**User clarification, October 23, 2026:** Temporal rendering was removed because the agent implementation worked badly and repeated repair attempts failed or made it worse. It may return later; it is not a current priority. This supersedes historical commit wording or repository guidance that presents non-temporal rendering as a permanent product rule. The GPU frame-slot/NRI design was Claude's implementation approach, not an architecture the user independently selected or validated. The user directs the project; agent-authored implementation choices remain open to technical review. Do not convert implementation history into user intent, or treat an existing design as proven correct or optimal merely because it landed.

## History coverage and branch identity

- Enumerated **all 1,135 unique commits reachable from the five current remote heads**, back to initial commit `a7f8fef` on January 18, 2025. Read their subjects and inspected selected complete messages, migration records, and current implementation paths.
- Verified graph completeness for those heads: every recorded parent is present in the retrieved commit set.
- `dev` has **1,130 reachable commits**; `feature/render-features` has **1,125**. They share 1,120 commits; dev has 10 exclusive commits and the feature branch has 5. Neither head contains the other.
- The old local refs contain **34 additional commits** outside the current remote ancestry, including two stash/index commits. These are retained in the history record as historical local state, not current features.
- Combined metadata record: **1,169 unique commits**. [Full commit ledger](Lux-Commit-History.json) includes remote commit messages, parent IDs, branch membership, and old local-ref metadata.
- This reviews the entire reachable timeline and selected implementation changes. It is not a claim that every line of every historical diff was audited. Deleted, unreachable commits not exposed by the inspected refs are outside this scope.

| Ref | Pinned head | Meaning |
|---|---|---|
| Remote dev | `cc25be60385cf342a61030c2b12204c226b94ffa` | Current main development state; foliage materials PR #34 merged |
| Remote feature/render-features | `7ae13e595bf289b08b6d2f9b9021fed26ef7c00a` | Area lights, refraction, and an FFT-bloom spike beyond its shared base |
| Remote master | `7d645c93b240cc833a07ae6486e2298ba02614bf` | Historical default branch, not the current engine baseline |
| Remote copilot/repo-review-luxengine and copilot/scene-system-analysis | same as master | Historical branches |
| Local checked-out dev | `366345f4a34a8ed83f122a22d92e1ee03af87a52` | May snapshot with existing staged/unstaged work; does not describe current remote dev |

The render-feature branch does **not** yet include the ten dev-only foliage commits at these heads. Area/refraction/bloom and the newly merged foliage work must not be described as one already-integrated build.

No checkout, reset, merge, pull, push, or engine implementation change was performed. Existing local work was preserved.

## How the engine developed

| Period | Development and its significance |
|---|---|
| January–March 2025 | Application/events/layers, OpenGL abstractions, batched 2D rendering, ECS, scene serialization, editor camera, picking, and Box2D established the original engine foundation. |
| March–July 2025 | C# interop and serialized fields, projects, MSDF text, asset management/thumbnails, miniaudio, and Tracy were added. These explain older APIs but do not establish the current host/audio backend. |
| February–April 2026 | Lux reorganization and Vulkan/NVRHI work changed references, threads, application services, serialization, renderer/editor infrastructure, async assets, hierarchy, prefabs, and scene ownership. Large commits require reading their bodies, not treating their short WIP subjects as absence of functionality. |
| May–June 2026 | Runtime export/player, render graph, Jolt 3D physics, persistent RenderScene/GPUScene/material/texture layers, and deferred rendering appeared. |
| Late June–July 2026 | Clustered lighting became the opaque path; the opaque forward path and technique selector were removed. Performance work added dirty-row uploads, descriptor invalidation, batched uploads, GPU-driven culling, LODs, meshlets, shadow filtering/caches, and runtime memory reductions. |
| July 2026 | Coral replaced Mono; C# physics bindings and Linux editor/runtime/build support followed. |
| August 2026 | The poorly working temporal implementation was removed after unsuccessful repair attempts; spatial AA and quality categories remained. Temporal work can be revisited later and is not a current priority. Unwired atmosphere/cloud/fog remnants were removed. Editor redesign and undo/redo work expanded. |
| September 2026 | Prefab variants/overrides, live scripting UI, material editing, FMOD/Vercidium acoustics, controller features, scoped undo improvements, and the rapidyaml migration substantially changed the engine. |
| October 2026 | NRI migration replaced GPU ownership, recording, descriptors, submission, presentation, queries, and barriers. Retained-scene work then reduced CPU submission costs. Physical lighting, shadows, sRGB correctness, foliage, runtime export hardening, and render features followed. |

Dates above group development by period; exact commit times are in the ledger in UTC. User-facing commit-day interpretations use America/Toronto.

## Current mechanisms: problem, implementation, constraints

These sections describe mechanisms and historical rationales. Their presence in commits is not proof that they are the best solution. Distinguish the user's goals from agent design decisions, and distinguish recorded verification from independently repeated evidence.

### GPU lifetime and the NRI migration

**Reported problem:** The migration history identifies nonsequential MAILBOX image acquisition and unsafe reuse of per-frame data as motivation. A swapchain image index and a reusable frame slot have different meanings; verifying the exact affected paths still requires tracing ownership, completion, and indexing in the implementation.

**Solution:** [318f346b](https://github.com/starbounded-dev/LuxEngine/commit/318f346b81eb8db260f2a114bb08c0e66ed377b2) introduced a monotonic render-frame number, a frame slot computed modulo frames-in-flight, completion waits before slot reuse, and frame-tagged GPU deletion. [8593cc4d](https://github.com/starbounded-dev/LuxEngine/commit/8593cc4dcbe7037a566009d79ee1056e68b38342) moved command-buffer ownership and submission onto NRI command-buffer pools and per-queue timeline fences. Swapchain acquire semaphores are consumed by the submission that actually draws to the back buffer.

**Current consequence:** NRI is the actual GPU implementation, not a wrapper still depending on NVRHI recording. [8247c3fd](https://github.com/starbounded-dev/LuxEngine/commit/8247c3fd7f0d4b1ea4e587b9916a386502fc09ec) removed the NVRHI dependency on October 13. Renderer/RHI owns the abstraction; raw Vulkan remains for device bring-up, shader tooling, Tracy, and explicitly recorded exceptions. See [RHIDevice](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/Core/Source/Lux/Renderer/RHI/RHIDevice.h#L79) and [renderer invariants](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/.claude/docs/Rendering.md#L525).

**Comparison rule:** Review Claude's implementation against the problem and compare it with Hazel/Lucky's actual methods. Evaluate deletion timing, frame-slot reuse, queue waits, complexity, and raw-pointer ownership; explain which approach should be kept or adapted and why. Matching API names do not establish matching lifetime guarantees. The user's role as director does not imply technical endorsement of the chosen design.

### Persistent render data and CPU submission

**Problem:** Thousands of unchanged meshes repeatedly incurred mesh resolution, bounds calculation, material-table hashing, full proxy reconstruction, and submission lookups.

**Solution:** [f38c2762](https://github.com/starbounded-dev/LuxEngine/commit/f38c27627c9a46242a74896881ca0397d5f43099) retained proxy state, tied recomputation to changed inputs, and reference-counted material/texture rows. [41c99b82](https://github.com/starbounded-dev/LuxEngine/commit/41c99b8258e43af77f540abcf2118e6256d2ec79) added dense previous-input records in sync order, allowing an unchanged entity to avoid even reading its proxy. Per-proxy submission records and material/asset revisions keep cached results current.

**Tradeoff:** Correct invalidation is part of the design. Mesh reloads, material edits, scene replacement, and changed table revisions must invalidate the right cache. See [RenderScene.h](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/Core/Source/Lux/Renderer/RenderScene.h#L83). The commit records an A-B-A sync result of 1.27 / 0.53 / 1.28 ms for one optimization; those are historical reported measurements, not measurements repeated during this review.

### GPU culling, LODs, and shadow-cache transitions

**Problem:** GPU visibility alone did not eliminate CPU rebuilding/submitting all instance groups each frame. Moving and newly settled casters also need different shadow treatment.

**Solution:** [319aaaa1](https://github.com/starbounded-dev/LuxEngine/commit/319aaaa1df10e8304029d00f65a8bade5aa02bcd) added persistent main-view cull groups. An early frustum/LOD dispatch writes indirect prepass inputs; the depth prepass and G-buffer select consistent LODs. [5877911e](https://github.com/starbounded-dev/LuxEngine/commit/5877911e1bb0d0575f28a483df9975b6815b5acd) added shadow cull groups and GPU per-target/LOD outputs for stable casters. Moving casters retain a CPU path. [7695bb40](https://github.com/starbounded-dev/LuxEngine/commit/7695bb4035106515f789f8fac26479679e04d68e) keeps groups on eligible transform-only changes and preserves shadows while recently moved objects settle.

**Tradeoff:** These are different mechanisms from a per-frame CPU sphere test or Lucky's meshlet culling. Replacing them wholesale could reintroduce CPU cost or break the settle window. The source has active [culling resource creation](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/Core/Source/Lux/Renderer/SceneRenderer.cpp#L1824) and graph dispatches, not merely the May declarations.

### Opaque rendering, quality, and historical removals

**Problem:** Opaque pipeline duplication and temporal implementation failures are separate issues. The user says the temporal implementation was unreliable, frustrating, and repeatedly failed to improve despite repair attempts.

**Implementation history:** [eefd3a5c](https://github.com/starbounded-dev/LuxEngine/commit/eefd3a5c96fe90f0684bd7ce8ecbdf8b412c6177) removed opaque forward/tiled code and the RenderingTechnique selector; opaque rendering is clustered deferred, while transparency uses forward shading. [e97cd113](https://github.com/starbounded-dev/LuxEngine/commit/e97cd113ff747e0d335dcfd28207eaf9207226eb) removed TAA, SMAA T2x, and GTAO/SSR temporal accumulation. Spatial processing and per-category quality controls remained. The commit's broad anti-temporal rationale must not be attributed to the user: the user removed a badly implemented system and remains open to adding a properly implemented temporal technique later.

[e878071a](https://github.com/starbounded-dev/LuxEngine/commit/e878071a975861b18981e2d747cb103d6ea5fc50) removed the unwired atmosphere/cloud/fog stack. Its message explains that its pass/material creation was missing; the removal cleaned up consumers of an inactive implementation.

**Comparison rule:** Investigate why a feature was removed and confirm current intent with the user's explicit statements. Temporal rendering is deferred, not prohibited. Future investigation may compare methods and failure causes, but implementing it again is not the current priority.

### Scripting and preservation of authored data

**Problem:** The old embedded Mono implementation and its lifecycle were not the current scripting architecture. Reloading or missing assemblies also risks losing authored field values.

**Solution:** [94a33f5b](https://github.com/starbounded-dev/LuxEngine/commit/94a33f5b635b8c8623e7f24f3237995d085c50b9) replaced Mono with Coral/.NET, metadata caches, managed objects, scene-owned ScriptStorage, dual idle/live field storage, shadow-copy reload, and integrated scene/editor/runtime lifecycle. Later work preserves unresolved script fields through loading, copying, and saving, and binds them when the script resolves.

**Current consequence:** Suggesting a Mono-to-Coral migration is obsolete. Compare Lucky's host/engine ownership with Lux's existing singleton host and scene storage rather than assuming the runtime needs replacing. See [current ScriptEngine](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/Core/Source/Lux/Scripting/ScriptEngine.h) and [current scripting architecture](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/.claude/docs/Architecture-LuxEngine.md#L473).

### Physics, audio, serialization, and editor workflows

**Physics:** [ab90a0b9](https://github.com/starbounded-dev/LuxEngine/commit/ab90a0b96dbe92b4049e000f0408c3da59df344e) introduced Jolt after the stale May checkout. Current [PhysicsScene.cpp](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/Core/Source/Lux/Physics/PhysicsScene.cpp#L825) steps Jolt with an accumulator/substep strategy and exposes a fixed-step entry. It includes bodies, colliders/cooking, queries, and controllers. Lux does not use Lucky's robotics fixed-phase scheduler; the simulation-thread scaffold remains unwired.

**Audio:** FMOD Core/Studio and Vercidium Audio replaced miniaudio. Audio now includes banks/events, scene ownership, acoustics/materials, zones/portals, music/dialogue, captions/accessibility, budgets, and runtime packaging. A Lucky miniaudio implementation is not a presumed upgrade. Relevant changes include [fa679b3f](https://github.com/starbounded-dev/LuxEngine/commit/fa679b3fb9d08b6f4de464a731c5bc8d7e5d912e) and the subsequent full-audio phases.

**Serialization:** [07dceeb1](https://github.com/starbounded-dev/LuxEngine/commit/07dceeb17e628a0e96e2ed19791b120042f43f8f) removed yaml-cpp after the migration to Lux::Yaml over rapidyaml. Compatibility checks use recorded YAML values rather than merely testing whether files parse.

**Undo:** [93e663b8](https://github.com/starbounded-dev/LuxEngine/commit/93e663b8b6d64c9ae57293f216a996e98b7673ef) addresses full-scene serialization on every edit by capturing scoped entities, with debug checks against a full snapshot. Later undo/redo applies patches in place. The problem is preserving state and bounding edit cost, not simply adding an undo stack.

### Foliage on current dev

**Problem:** Averaging alpha into smaller mip levels makes distant cutout foliage lose coverage. Inconsistent alpha tests between depth, shading, and shadow passes remove valid fragments under depth-equal rendering.

**Solution:** [703d998a](https://github.com/starbounded-dev/LuxEngine/commit/703d998ad641538380ee5c887e2da10a0cfd7e41) builds cutoff-specific, coverage-preserving albedo mip chains using per-mip alpha histograms and rescaling. Every alpha-tested pass samples the same variant. Meshlet pre-depth now supplies matching UV/material inputs and supports two-sided draws. Opaque pre-depth remains depth-only so opaque fragments do not pay for a discard/material read.

**Additional behavior:** Diffuse transmission adds a back-facing diffuse lobe for thin surfaces; two-sided transparent materials draw back faces before front faces. These changes are in dev at [cc25be60](https://github.com/starbounded-dev/LuxEngine/commit/cc25be60385cf342a61030c2b12204c226b94ffa).

### New render features on the separate feature branch

- **Area lights:** [f236be15](https://github.com/starbounded-dev/LuxEngine/commit/f236be15c3d7473d72cc5d8cdad08f638b3a3014) and [c974c62d](https://github.com/starbounded-dev/LuxEngine/commit/c974c62d30e92c270253fee8922e1f2637c7905b) implement rect/disk/sphere/tube lights through LTC integration. A capped list of the nearest 32 lights feeds a separate additive pass that is absent when no area lights exist; transparent shading uses the same lighting function.
- **Refraction:** [d2c1b4f8](https://github.com/starbounded-dev/LuxEngine/commit/d2c1b4f84d42e236fe47b8f2910d6df3ba930785) adds transmission/IOR/thickness/attenuation inputs. It reuses the pre-convolved opaque scene, chooses roughness mips, applies absorption, and falls back to environment radiance off-screen. Pre-convolution runs for refractive content even with SSR disabled.
- **FFT bloom:** [7ae13e59](https://github.com/starbounded-dev/LuxEngine/commit/7ae13e595bf289b08b6d2f9b9021fed26ef7c00a) is a measured spike behind a temporary, nonserialized option. Padding prevents circular convolution wrap; a normalized kernel redistributes energy. It does not establish that the default mip bloom has been replaced.
- Frosted editor glass, G-buffer slimming, meshlet performance work, and compact tangent vertices remain plan items unless later implementation is verified. [The plan](https://github.com/starbounded-dev/LuxEngine/blob/7ae13e595bf289b08b6d2f9b9021fed26ef7c00a/docs/RENDER_FEATURES_PLAN.md) explicitly distinguishes intended work from built phases.

## Corrections to the earlier Lucky comparison

| Earlier conclusion | Correct current baseline |
|---|---|
| Lux uses NVRHI | NVRHI was removed; Lux uses its NRI RHI layer and explicit state/lifetime model. |
| Lux needs Coral scripting | Coral migration and scene/editor/runtime integration already landed in July. |
| Lux lacks Jolt 3D physics | Jolt bodies, shapes, queries, controllers, cooking, and stepping exist. |
| GPU culling is only declarations | Main-view and shadow GPU culling, indirect draws, LODs, and persistent cull groups are active. |
| Only one directional projection exists | Cascaded directional shadows, caster filtering/caching, and point shadows exist. |
| Deferred selector needs connecting | The selector and opaque forward path were intentionally deleted; clustered deferred is the opaque renderer. |
| Lux needs generated LOD/meshlet support | Both exist; actual performance and default settings matter. |
| Lux's editor is single-threaded | Windows defaults to a real render thread; Linux defaults to single-threaded. Correctness under both is required. |
| Lux has only a timer/stats panel | Tracy CPU/GPU, golden image/performance captures, per-pass/stage metrics, and threading A/B tools already exist. |
| miniaudio describes current audio | It was replaced by the FMOD/VA system. |

The fixed render-command arena's unchecked capacity is still visible in current [RenderCommandQueue.cpp](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/Core/Source/Lux/Renderer/RenderCommandQueue.cpp#L25), so this remains a concrete candidate for a future problem/solution comparison. Lux already routes submissions differently for main/render/background callers; blindly adding Lucky's allocation mutex would not analyze that existing single-producer design.

Other old findings, including imported node instancing, require tracing the current complete import/render/runtime path before being retained. The old report is not an approved change list.

## Ground rules for the next comparison

1. Pin the exact Lux and Lucky commits/branches.
2. Trace what problem each implementation addresses and the failure that motivated it.
3. Follow the real data flow, ownership, thread context, invalidation, and GPU lifetime.
4. Explain the mechanism and its costs, fallbacks, and limitations.
5. Check whether Lux already solves that problem differently, and distinguish user priorities from implementation failures or agent-authored rationales.
6. Propose a focused adaptation only when it fits Lux's goals and offers a measurable or correctness benefit.

Current implementation constraints include GLSL-first shaders; the shared global (set, binding) namespace; main-thread ImGui construction with render-thread GPU work on Windows; engine/runtime compatibility; and self-contained editor features. The renderer currently uses spatial processing, but non-temporal rendering is not a permanent product constraint; temporal work is deferred by priority. Async transfer and the simulation-thread scaffold must not be presented as fully enabled production behavior merely because their types exist. Existing frame-slot and other agent-authored designs remain candidates for review rather than unquestioned foundations.

Builds, editor workflows, graphics validation, and historical performance results quoted by commits were not repeated in this review. This establishes a corrected historical and source baseline for subsequent investigation.

