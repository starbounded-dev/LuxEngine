# Acoustic Kernel Plan — Soundwave: the acoustics Vercidium Audio gives LuxEngine, rebuilt in the engine

**Status: planning only. No engine code is changed by this document. Decisions 1-4 and the
deferred 5-6 are answered and recorded here; four items are still unanswered (§ 1, "unanswered").**

> **Fold note (2026-10-31).** The 2026-10-30 session that took the user's answers wrote nothing to
> disk: all 72 of its tool calls were read/grep/glob, and it ended mid-write with "I ran out of room
> in this chat"; its next two turns failed (`PI_AI_ERROR`, then `AGENT_STOPPED`). The answers lived
> only in that chat, whose only copy is
> `C:\Users\sheaz\Downloads\look-at-docs-audio-acoustic-plan-and-tell-me-a--session-log.json`.
> Every line below marked *user* is that record; quotes are their words. This document is now the
> durable copy — it is still **staged but uncommitted**, which is the one durability risk (§ 9.9).

- **Goal:** replace what Vercidium Audio (VA_RAY) gives LuxEngine — per-event `Occlusion` and
  `ReverbSend` energy, computed from scene geometry — with an acoustic simulation written in the
  engine's own C++, on the engine's own data, with no Vercidium link step.
- **Shape (user):** a separate **source** library named **Soundwave**, in `Core/vendor/soundwave/`,
  **no DLL**, statically linked into `Core`, **Apache-2.0, "the same license as lux"** (the repo's
  `LICENSE` is Apache-2.0; `Core/Source/Lux/Audio/AcousticMaterial.h:1-2` shows the SPDX header
  style the new files copy).
- **Boundaries:** FMOD Studio stays the playback and authoring backend and is not replaced
  (`.claude/docs/Architecture-LuxEngine.md` § 2.10:971, :987-991). The Asterwave decompilation
  repo and the Vercidium SDK sources stay unread and unported — the replacement reads no header,
  import table or log of either. The engine's own raycast occlusion path is the in-house
  starting point (`AudioOcclusion.h:58`, :69, :78, :84).
- **Deliverable:** a working GPU-compute acoustic kernel that drives a shipped Play on Windows and
  Linux, with editor authoring, serialization, bank validation, scripting, and docs all in place.
- **Compute (user):** "use the gpu for main calculation for fast rapid and perfect calculations".
  Shipped algorithm today: **compute-only**; Vulkan ray-tracing kept as documented future work.
- **Engine rules:** C++20, Vulkan-only, `Lux` namespace. `Ref<T>` / `Scope<T>`; no `new`/`delete`,
  no `shared_ptr`. Nothing expensive per frame: geometry is captured once, then tracked and
  invalidated. `LUX_CORE_ERROR_TAG` / `LUX_CORE_VERIFY`, no silent failure. Complete feature
  (runtime, editor, serialization, fallbacks). Linux builds and runs it. No new mandatory install
  (`CLAUDE.md § Product Principle`). `.claude/docs/Threading.md`, `Conventions.md`, `Rendering.md`,
  `Building.md`, `AGENTS.md § Engineering Posture`.
- **Non-goals:** replacing FMOD, replacing Asterwave, anything already native (zones, surfaces,
  validation, the raycast path as shipped), Vercidium's numerical output, HRTF/binaural, consoles.

---

## 1. Decisions the plan is built on

The user's answers are recorded here so the next session never has to re-ask them. Where a row's
"Answer" is a recommendation rather than the user's word, it is marked *(plan's call)* and can be
vetoed.

| # | Decision | Answer | What it forces |
|---|---|---|---|
| 1 | Is Vercidium a comparison target, and is a legal question put to them? | **B (user):** no written question, no comparison. "the removal/packaging/public-statement chore stays as one pending line at the very end — none of your answers decide it." | Verification is against **runs we record**, never against Vercidium's numbers. No mode set ships. The chore (§ 3) is one open line, not a gate |
| 2 | Shape and name | **Separate source library in `Core/vendor/soundwave/`, no DLL, statically linked, Apache-2.0, named Soundwave (user).** "lets call it soundwave, i dont want any dll, so put it in vendor" | A vendored source library with its own namespace, tests and docs; no extra deployable for game-makers; `Dependencies.lua` owns its include/link lines |
| 3 | When the build glue drops | **One change (user: "for the 2. i will take also A"; the "B. for the 2" slip was re-asked and never corrected — the plan's call is one change).** | The VA lines, the option text and the link step go the same moment the kernel drives Play (Phase 7) |
| 4 | Reach | **Full, in scope now (user, Docs Q2 = b):** occlusion, reflection, echogram reverb, air absorption, **caustics and >2 bounces** | Each extra needs editor authoring + serialization + bank validation to count as done (§ 9.6); they land as their own phase, not a footnote |
| 5 | Old projects that chose Vercidium's numbers | **Retarget all projects to engine values (user); the field drops out (user, Files Q1 = b)** | `AudioOcclusionSettings.h:15` `AudioOcclusionSource` leaves project files; the reader must still load old audio blocks without failing (§ 9.10) |
| 6 | Deferred: do bands/EAX stay diagnostics-only? | **Yes (user: "the deffered decosopm all yes")** | LF/HF/MID/NEAR/FAR and EAX-style numbers are diagnostic outputs, never gameplay-script surface (§ 2.10:989-990 already says this for the SDK path) |
| 7 | Deferred: does acoustic geometry stop being mirrored into the SDK's node/primitive model? | **Yes (user)** | `Scene::BuildAcousticGeometry` (Scene.cpp:2191) feeds the kernel's own geometry, not SDK primitives |
| 8 | GPU algorithm | **Compute-only over a hierarchy/grid the engine builds (user: "a obviously … for the devices that dont have any acces to ray-tracing shaders").** Ray-tracing documented as the future path: "keep it in the docs for future references … default when available, compute-only when unavailable (user)" | No new acceleration-structure infrastructure is required today; the RT variant is a documented on-ramp, not a phase |
| 9 | Which triangles are acoustic geometry | **Render meshes (user: "be is the best because for the moment its only basics scene … i wont put physics colliders on every parts"), with physics colliders kept in the docs as the future path (user)** | Cheap on upload (meshes are already resident), but a low-detail render mesh will not block sound — § 9.4 is the honest cost of this pick |
| 10 | Verification standard now that the GPU owns the math | **One tolerance we own, ≤ 0.5 dB on the two gains, plus a fixed reduction order in validation builds (user: "3. a")** | Bit-for-bit GPU/CPU claims are abandoned; the tolerance and the reduction order are authored here and become the gate |
| 11 | Is there a comparison partner at all under B? | **Yes, ours: keep the engine's own raycast occlusion path as a temporary debug/comparison mode (user: "1.a but rewrite it because it doesnt really work well").** Their words also settle scope: the new kernel is a rewrite, not a patch on that path | `AudioOcclusion` (AudioOcclusion.h:58-84, AudioOcclusion.cpp:224 budget) is the oracle for Phases 1-5 and is compiled out of shipped builds afterwards |
| 12 | Dispatch | **Compute queue, once per acoustic update, from the render thread's segment (user accepted the plan's answer: "alr lets do that")** | The render thread stays the only thing that owns GPU submission; `Renderer::Submit` from the audio window on main fills the queue (Threading.md:112-138) |
| 13 | Readback | **One update of latency, interpolated (user switched from b to a: "Pick c … readback a — alr lets do that")** | No GPU wait on the main thread; a targeted sync there is exactly what `AGENTS.md § Engineering Posture` rejects |
| 14 | Threading ownership | **Prep on JobSystem workers, GPU on the render thread, final floats handed to the audio backend on main (user: "Threading: 1. same thing with B.5")** | Workers see disjoint indices and no ECS pointers; two cores are reserved for main+render; main never waits on a job result |
| 15 | Cadence | **Reuse `UpdateRateHz` (1-120) with interpolation between updates (user: "2.C as default")** | `AudioOcclusionSettings.h:20` keeps its meaning; the budget shape stays "work, not cost" (AudioOcclusion.cpp:224) |
| 16 | C# surface | **Unchanged contract — the two floats only (user: "C# 1.a")** | `AudioEventInstance.h:116` `SetAcoustics`, :120 `kOcclusionParameter`, :121 `kReverbSendParameter` stay the whole scripting API |
| 17 | Source identity | **Name for authoring + entity UUID at runtime (user: "2.c")** | `GetGainLF(UUID)` (AudioOcclusion.h:84) remains the runtime key; a name-based lookup is editor-only |
| 18 | Doc depth | **In-code + architecture doc in the same change + a new shared doc + a manual page, with `Threading.md` / `Rendering.md` lines updated because dispatch and queue rules change (user: "Docs 1.b")** | `CLAUDE.md § Architecture Doc Maintenance` and the doc-staleness rule apply in the landing phase, not later |
| 19 | Air absorption | **Wanted (user: "i want air absorption")** — but the double-counting model is **unanswered** (§ 1, open) | Absorption is in scope; which attenuation is authoritative still decides whether distant sources fade once or twice |
| 20 | Vendor-folder deletion | **Not a disclosure act — `Core/vendor/VA_RAY/` is gitignored and was never in the repo** (`.gitignore:18`; `git ls-files` returns 0 tracked files there) | Replaces the old risk 9.5; the deletion is a local, untracked cleanup |

**Unanswered — these still gate their phases (asked in plain words, never answered):**

- **Air absorption vs the backend's distance fade** (three options were re-worded for the user;
  their last words were "still dont understand"). Plan's call if they do not choose: absorption
  scales only occlusion/reverb *energy*, FMOD's own distance fade stays authoritative for distance,
  the medium value is one per-project row with an optional per-zone override.
- **Where the new tunables live** (their words: "Wdym i dont understand what you were even
  talking about"). Plan's call: absorption per acoustic material (`AcousticMaterial.h` already
  serializes per-tag overrides) + cadence/bounce/depth rows beside the audio block
  (`AudioOcclusionSettings.h:15-22`).
- **Is a CPU path required** (for devices without a compute queue, and for the headless tests)?
  The user never answered; the plan's call is *no CPU fallback for shipping* — compute is required,
  with `LUX_CORE_VERIFY` and a clear editor error when the queue is missing — while the CPU kernel
  stays compiled in for tests and the comparison mode (Decision 11).
- **Is `origin/feature/asterwave-audio` the base or is it superseded?** § 10.4 is the decision
  line; the plan cannot start Phase 5 before it is answered.

---

## 2. Verified ledger of what exists now

Every row below was read in **this session (2026-10-31)**; line numbers are the working tree at
HEAD `eb7372bc` on `feature/lucky-merge`.

| Item | Status | Evidence | What it means for this plan |
|---|---|---|---|
| Engine raycast occlusion | **working, shipped; runtime not re-verified this session** | `Scene/Scene.cpp:2338` `Scene::UpdateAudioOcclusion`, `:2340` `m_AudioOcclusion.SetGeometry(m_AudioGeometryInputs, …GetPortalOpen(entity))`, `:2364` `m_PhysicsScene->CastRayAll(&ray, m_AudioOcclusionHits)`; `AudioOcclusion.h:58` "Main thread only. The caller supplies the raycast so this stays independent of Physics", :69 `using RayCaster = std::function<void(UUID source, …)>`, :78 `Update(timestep, listener, sources, RayCaster&)`, :84 `GetGainLF(UUID)`; `AudioOcclusion.cpp:224` `for (size_t i = 0; i < count && evaluations < m_Settings.CastBudget; ++i)`, :256 `GetGainLF` | the comparison oracle (Decision 11) and the pattern the kernel extends |
| The mode branch that is about to go away | **shipped, wired in two places** | `Scene.cpp:940` `if (m_OcclusionSource == AudioOcclusionSource::Engine)` → `:941 UpdateAudioOcclusion(…)`; `Scene.cpp:965` the same test feeding `AudioEventAcoustics`; `Scene.h:343` `AudioOcclusionSource m_OcclusionSource = AudioOcclusionSource::Engine`, set at `Scene.cpp:2281` | Decision 5 removes the choice from files; both branch sites collapse to the kernel |
| `AudioOcclusionSettings` | working, shipped, serialized | `AudioOcclusionSettings.h:15` `enum class AudioOcclusionSource : uint8_t { Engine, Raytraced }`, :20 `UpdateRateHz = 20.0f`; `AudioOcclusionSettings.cpp:27` range validate, :35/:59/:60 YAML row round-trip | the new system inherits the budget shape; the enum row is deleted from the struct per Decision 5 |
| Two-band material model, authored | working, shipped, serialized | `AcousticMaterial.h:81 AbsorptionLF`, :84 `TransmissionLF = 1.0f` ("metres through closed geometry until energy is lost"), :86 `FlatTransmissionLF`; YAML/runtime readers at `AcousticMaterial.cpp:203-208`, :235-240, :270-275, :305-307 | absorption and transmission are the user's model — air absorption builds on it; only the *defaults* are the SDK's (`AcousticMaterial.cpp:72` `vaWorldGetMaterialAbsorptionLF`, :75 `vaWorldGetMaterialTransmissionLF`, :77 `vaWorldGetMaterialFlatTransmissionLF`) — those three move to an authored table |
| The SDK's shadow over the materials | **the whole reason Decision 7's yes is legal** | `AcousticMaterial.h:11 struct VAWorld;`, :108 `GetDefaultAcousticMaterialProperties`, :111 `AcousticMaterialVAID`, :112 `ConfigureAcousticMaterials(VAWorld*, …)`; `AcousticMaterial.cpp:9 #include "vaudio.h"` | the kernel deletes these headers and includes; the tags survive untouched |
| Acoustic geometry capture | working, shipped | `Scene.h:284 SyncAudioGeometry(bool initial = false)`, :285 `BuildAcousticGeometry(const AudioGeometryInput&, AcousticGeometry&)`, :286 `UpdateAudioOcclusion`; `Scene.cpp:922` call per update, :2191 builder, :2225 capture, :2264 the lambda that builds, :2284 `SyncAudioGeometry(true)` at Play start | the kernel's GPU input is fed from this capture, not from physics colliders (Decision 9) |
| The two floats, end to end | working | `AudioEventInstance.h:116 SetAcoustics`, :120 `kOcclusionParameter = "Occlusion"`, :121 `kReverbSendParameter = "ReverbSend"`; `AudioEventInstance.cpp:629 setOptional(kOcclusionParameter, occlusion)`, :630 clamp on `ReverbSend` | the output contract the kernel must satisfy; nothing else is consumed (Decision 6 keeps bands diagnostic) |
| The SDK wrapper still in tree | working, and the thing being rebuilt | § 2.10:971 (`Scene` owns a `RaytracedAudioScene`, Pimpl), :974/:976 (join-then-mutate, one listener, tie-break, attenuation target), :978 (world pool), :982/:985 (teardown, `ShutdownWorldPool`), :989-991 (other bands and EAX diagnostic; scripted events register no emitters) | the kernel removes the wrapper; those rules exist only because the SDK is async — they are not constraints on this design |
| Headless audio regression tests | **working, but tied to the SDKs** | `tests/audio/README.md:7` `python tests/audio/run.py`, :12 "Requires `clang++`, the repository's FMOD and VA SDKs, and `fmodstudiocl`"; `tests/audio/OcclusionTests.cpp:11` and :16 read `GetDefaultAcousticMaterialProperties(…).TransmissionLF` / `FlatTransmissionLF` | Phase 7 must update the runner and these two tests, or the suite stops being runnable once the SDK presets move to the authored table |
| Removal surface (build) | mandatory today | `Dependencies.lua:21 VASDKRoot`, :23 `ValidateAudioSDK`, :27 `vaudio.h`, :40/:41 the `.lib`/`.dll`, :43 `libvaudionative.so`, :189-191 the `VARay` include/lib rows; `premake5.lua:32 ValidateAudioSDK()` (runs for every target); `Editor/premake5.lua:61` and :119 copy lines; `Lux-Runtime/premake5.lua:57` and :120 copy lines | all of it deletes in the Phase 7 change (Decision 3) |
| Removal surface (deployment) | mandatory today | `Editor/Source/RuntimeExportUtils.cpp:103` `"libvaudionative.so"`, :107 `"vaudionative.dll"` in the deployed-library lists; `docs/AUDIO_DESKTOP_PLATFORMS.md:46` "FMOD is required, along with VA", :53 `LUX_VA_SDK`, :64-65, :72-74, :82 | exported games carry one fewer native dependency (feeds `NEXT_UP_BACKLOG.md:92` 4.15) |
| Removal surface (repo) | **not a repo act** | `.gitignore:18 Core/vendor/VA_RAY/`; `git ls-files -- Core/vendor/VA_RAY` → 0 tracked | replaces the old risk 9.5; deleting the local folder changes nothing in history |
| Budgets and validation protocol | working, shipped, names the SDK | `docs/AUDIO_PERFORMANCE_VALIDATION.md:12` "VA warning | 2 ms | Last completed VA raytracing batch" (file is 89 lines); `docs/NEXT_UP_BACKLOG.md:91` row 4.14, :106 row 5.8 | the kernel is priced against the same protocol; that row is renamed in the same change, not left to mean "Vercidium" |
| Editor debug view of acoustic materials | done | § 2.10:1030-1034 | the new system needs a visualisation of **paths**, and any new overlay pass must take a fresh `(set, binding)` — that namespace is global (`AGENTS.md §` fact 4) |
| A GPU compute pass in this engine | **working precedent, everywhere** | `Renderer.h:210 BeginComputePass`, :211 `EndComputePass`, :212 `DispatchCompute`, :292/:296 `RegisterShaderDependency`; `Renderer.cpp:1138 DispatchCompute`, :390 `RT_RecordDispatch`; `PipelineCompute.h:18` class, :21 `Create(Ref<Shader>)`; `ComputePass.cpp:59 SetInput(StorageBufferSet)`, :65 `SetInput(StorageBuffer)`, :41 `IsInvalidated(set, binding)`, :120 `Validate`, :126 `Bake`; `SceneRenderer.cpp:2236-2247` the create/Validate/Bake pattern, :9579 dispatch, :9581 barrier; Architecture:273-274 `PipelineCompute`, :275-277 `DescriptorSetManager`/`ComputePass` | the kernel's GPU work is one more `ComputePass`, not a new submission path |
| A dedicated compute queue | requested at device creation | `Core/Source/Lux/Core/Window.cpp:66 deviceParams.enableComputeQueue = true`, :426-427 `IsComputeQueueAvailable()` → `QueueFamilies[GPUQueue::Compute]`; `VulkanDevice.h:90` flag, :179 `IsComputeQueueAvailable()`, :278 `m_ComputeQueue`; `VulkanDevice.cpp:541`, :667-668 `getQueue(…Compute…)` | the queue exists; whether a `ComputePass` records on it when the family is absent must be read before it is relied on (§ 10.2) |
| Linux audio | **never exercised** | § 2.10:556-557 | the pure-C++ kernel is what finally verifies it |
| The defect evidence | public in repo, reproducible | `docs/vercidium-repro/occlusion_not_responding_to_geometry.c`, `docs/vercidium-repro/occlusion_output.txt` (the folder holds **only** these two; there has never been a README under any name in any branch) | occlusion here is greenfield — there is no upstream behaviour to match |
| **Any engine-internal acoustic kernel** | **not built** | grep of `Core/Source/Lux/Audio` shows no `AcousticPath`; no `Soundwave` anywhere in the repo (0 matches in `docs/AUDIO_ACOUSTICS_PLAN.md` before this fold) | this plan's subject |
| **An unmerged audio branch that overlaps this plan** | **exists, not merged** | `git merge-base --is-ancestor origin/feature/asterwave-audio HEAD` → false; merge-base `76062c9c` (2026-10-25); 7 commits to head `4ba12bc1` (2026-10-28); it carries `docs/AUDIO_NATIVE_BACKEND_PLAN.md` (not at HEAD), `Core/Source/Lux/Audio/RaytracedAudioSceneStub.cpp`, `AsterwaveMaterials.h`, and `Dependencies.lua:21-71` Asterwave checkout machine (`ValidateAsterwaveSource`, `scripts/FetchAsterwave.py`, `--raytraced-audio` opt-in at :229-233) | § 10.4 is the decision the plan cannot make for the user; nothing in this plan may proceed as if that work were absent |

### Corrections to the 2026-10-30 ledger

The old document's anchors had drifted and several were never true. Fixed against this tree:

- `AcousticMaterial.h:11-12` was cited as "the bands the user already declared". **False** —
  :11 is `struct VAWorld;`. Today's model is LF/HF only; MID/NEAR/FAR are **NEW**, not inherited.
- `AudioOcclusion.h:64` / `:68` / `:70` / `:72` → :58 / :78 / :81 / :84.
- `AcousticMaterial.h:46` / `:49` → :84 / :86.
- `Dependencies.lua:61-65` and `:159-166` → :185-192 (`VARay = { IncludeDir … }`); the
  "VA (Vercidium Audio) SDK" text is no longer at :61-65.
- `Scene.cpp:2361-2362` "m_CastCost/m_CastsThisUpdate" and `CastRays` → do not exist; the
  budget is `AudioOcclusion.cpp:224`, the cast is `Scene.cpp:2364 CastRayAll`.
- `README.md:25` and `:246` → blank and `make clean`. The real claim is `README.md:446`.
- `docs/AUDIO_DESKTOP_PLATFORMS.md:99-104` → the file is 85 lines; see the :46/:53/:64-74 anchors.
- `docs/NEXT_UP_BACKLOG.md:105` (5.8) → :106.
- `docs/vercidium-repro/README.md:4` → that file never existed; the finding is in the two files
  above and in the commit body of "Audio: VA acoustics at full output, 3D debug view, FMOD
  Studio pipeline".
- Risk 9.5's "vendor-folder deletion is a disclosure act" → the folder is gitignored (`.gitignore:18`).

---

## 3. The one pending chore (was: the decision gate)

Decision 1 (B) retires the legal question as a *gate*. What stays, honestly, as one line at the
very end of the work — decided by nobody in that session, and not this plan's to interpret:

> **Pending:** the disclosure/removal/packaging chore around dropping Vercidium Audio, and whether
> the repository becomes public while `docs/vercidium-repro/` and the EULA's "may not disclose"
> clause sit in it. The user's own shape choice (Decision 2) makes Soundwave a named library, so
> any claim that the clause speaks only to engine-internal features no longer holds for this plan.
> Nothing in Phases 1-6 depends on this line; nothing ships in an engine release until it is
> settled (`docs/NEXT_UP_BACKLOG.md:91`, row 4.14).

---

## 4. Where Soundwave sits

| Concern | Answer |
|---|---|
| System | `Scene` owns the kernel exactly where `AudioOcclusion` sits today (Scene.h:284-286, Scene.cpp:922/:941). Soundwave is the library it calls; the library may depend on nothing in Lux except its own headers — the engine adapts the seam |
| Thread | Kernel *solve* runs on the GPU: dispatch recorded once per acoustic update in the render thread's segment (`Renderer::Submit` from the main-thread audio window, Threading.md:112-138 — on Linux main **is** render, so the lambda runs inline and the same code path is correct). CPU prep (geometry capture, grid/BVH build, source lists) runs on JobSystem workers over disjoint indices, no ECS pointers, no `Renderer::Submit` from a worker. FMOD receives the floats on main. Main never waits on a job result and never waits on the GPU |
| Ownership | `Scope<T>`-owned kernel inside `Scene`; GPU resources are NRI's — an `NRIBuffer`-backed `StorageBuffer`/`StorageBufferSet` through `ComputePass::SetInput` (ComputePass.cpp:59/:65), released through `Renderer::SubmitResourceFree` (Rendering.md:122-124) on the same render-frame tag rule (Threading.md:140-146) — and scene-transition teardown must drain the frame before the buffers retire |
| Renderer data flow | The kernel is not a render-graph pass and is not in `FrameRenderPacket`; it rides the render command queue like the existing compute passes do (`SceneRenderer.cpp:9579`). The editor's *path overlay* is the part that goes through the render graph, and needs a fresh `(set, binding)` |
| Renderer invariants | Pipeline, descriptor layout and buffers created once, cached, invalidated (`PipelineCompute::CreatePipeline` / `ComputePass::Bake`, never per frame — Rendering.md:97); new `PassDesc`/`TextureDesc` fields folded into `ComputeStructureHash()` (Rendering.md:1342); `RegisterShaderDependency` for the compute shader (Renderer.h:292/:296, Renderer.cpp:130/:158); the `(set, binding)` pair grepped against `Editor/Resources/Shaders/` before use; GLSL-first (`Editor/Resources/Shaders/*.glsl`, `.glslh`) |
| Serialization | Kernel rows in the project's audio block (YAML + runtime block, version gate, defaults for older formats); `AudioOcclusionSource` **removed** from files (Decision 5) while old audio blocks must still load (Scene reader assertions — § 9.10); per-material absorption stays in the existing override block (`AcousticMaterial.cpp:235-240` YAML, :270-275 runtime) |
| Editor | Inspect paths + materials; Help page; sample-project showcase; the Debug Panel's ambience rows (EditorLayer.cpp:3608, AudioDebugPanel.cpp:431) gain the kernel's numbers as diagnostics |
| Scripting | Two floats only, unchanged (Decision 6, :16) |
| Runtime and Dist | The kernel ships in `Core`, so `Lux-Runtime` and Dist need nothing — that is the point of Decision 2's "no dll" |
| Linux | The whole plan is the first Linux audio run (§ 2.10:556-557); single-threaded Linux means main == render, which is why the dispatch must not be a wait |
| Build | New files → `scripts\Win-GenProjects.bat` re-run (`Building.md`: premake does not regenerate itself); new library → `Dependencies.lua`; Phase 7 changes generation on **both** generators, so verification runs `vs2022` **and** `gmake2` |
| Docs | `Architecture-LuxEngine.md` § 2.10 rewritten in the landing phase; `Threading.md`, `Rendering.md`, `Building.md`, `Conventions.md` rows for the new dispatch/queue/settings helper; the manual pages listed in Decision 18 |

---

## 5. Phases (each builds and is verified before the next starts)

### Phase 1 — The measurement contract, reproduced from today's shipped numbers

- **Goal:** one engine-native description of "a source at a listener" — an ordered crossing list
  (acoustic tag, crossing distance, LF/HF dB loss, solid or one-sided) plus total distance and
  bounce count — that today's raycast path already feeds without changing a shipped number.
- **Changes:** `Core/vendor/soundwave/include/soundwave/AcousticPath.h` +
  `Core/vendor/soundwave/src/AcousticPath.cpp` — **NEW** (the library's first file: SPDX header,
  its own namespace, no Lux include). `Core/Source/Lux/Audio/AudioOcclusion.h/.cpp` — feed the
  contract from the existing `AudioOcclusionPath` (AudioOcclusion.h:39/:48 `LossLF`) without
  touching its maths. `AcousticMaterialSettings.h` — **NEW**: the authored LF/HF defaults that
  today arrive from the SDK (`AcousticMaterial.cpp:72-77`), overridable per project as they are now.
- **Playbook:** none new; this is a value type, no threads, no ownership.
- **Thread and lifetime:** none touched.
- **Serialization:** the authored-settings block only, once the version gate row is added.
- **Verification:** Windows `debug` + Linux `debug`; run `python tests/audio/run.py` and
  `OcclusionTests.cpp` — the same walls must produce the same dB as today, CPU-to-CPU, so this
  phase's gate is still **bit-for-bit** (the ≤ 0.5 dB tolerance appears at Phase 4, not here).
- **Done when:** the contract is what `AudioEventAcoustics` consumes, and today's shipped numbers
  reproduce through it unchanged on both platforms.

### Phase 2 — The CPU reference kernel in Soundwave: occlusion, reflections, openings

- **Goal:** the acoustic solve exists as its own library — a grid/hierarchy built from the captured
  acoustic geometry, one direct path plus specular and portal/gap candidates — and a corner/doorway
  is audible where today's single ray is not.
- **Changes:** `Core/vendor/soundwave/` — **NEW** `Grid.h/.cpp`, `Solve.h/.cpp` (candidate
  generation, the crossing walk, the budget), `Settings.h` (`ReflectionDepth`,
  `OpeningGridSize`, `MaxBounces`, absorption row); `Scene.cpp` — drive the kernel where
  `UpdateAudioOcclusion` (2338) runs, keeping the engine raycast path as the comparison mode
  (Decision 11); `AudioOcclusionSettings.h:15` — **not yet deleted**, but the kernel ignores it
  (the field drops out at Phase 6, Decision 5).
- **Playbook:** library through `Dependencies.lua` (`Dependencies.lua:189-191` is the pattern),
  `scripts\Win-GenProjects.bat` re-run, premake option text reviewed (§ Decision 3's "one change"
  is Phase 7, so the VA lines stay while VA is still the shipped *playback-adjacent* acoustics).
- **Thread and lifetime:** prep on JobSystem workers (disjoint indices), solve callable on main
  until Phase 4; workers never call `Renderer::Submit` and never touch the ECS.
- **Serialization:** new settings rows with the version gate and defaults for older files.
- **Verification:** Sponza — audible around a corner and through a doorway, **not** through a
  closed door; straight-path numbers within the stated tolerance of the comparison mode's, and
  equal to it when `ReflectionDepth = 0`, grid = 1, bounces = 1. Windows debug + Linux debug
  (Linux first run of any acoustic solve — expect real failures there).
- **Done when:** the library solves propagation the engine code cannot, and the straight-path
  regression is unchanged.

### Phase 3 — Reverb from the space's shape, not a distance falloff

- **Goal:** `ReverbSend` is derived from a time-shift histogram of propagation-path lengths per
  zone (bins = path time, weight = opening area), cached and invalidated with the geometry queue,
  and a stone room and a corridor sound different at the same event and distance.
- **Changes:** `Core/vendor/soundwave/` — **NEW** `Echogram.h/.cpp` (histogram, smoothing, window,
  decay); `Scene.cpp` / the zone system — per-zone echogram fed by the kernel's paths;
  `AcousticMaterialSettings.h` — authored defaults for window/decay/smoothing; `AudioEventInstance.h`
  — unchanged (it already takes `ReverbSend`, :121).
- **Playbook:** § 2.10:1048-1057 (zones and portal links are already authored and solved) and
  :1042 (the geometry queue already limits active frames — reuse it, do not rebuild per frame).
- **Thread and lifetime:** cached per zone, invalidated through the queue that limits active frames
  today; nothing rebuilt per frame (`AGENTS.md §` "Nothing expensive per frame").
- **Serialization:** new settings rows; zone data already serializes.
- **Verification:** stone room vs corridor driven in Play, plus the validation pass
  (`docs/AUDIO_PERFORMANCE_VALIDATION.md`) reporting the same work budget; `send-pr`-style
  bank validation warning where an event has no `ReverbSend` bus (today only `Occlusion` warns —
  § 2.10:1009; new validation is required here, § 9.6).
- **Done when:** reverb varies with the space, and a project without a reverb bus is *told* so.

### Phase 4 — The kernel runs on the GPU compute pipeline, verified against its CPU twin

- **Goal:** the shipped acoustic solve is the GPU one: a `ComputePass` per stage, the acoustic
  geometry uploaded once and tracked, the grid/hierarchy built on workers, the paths solved in
  compute shaders, and the two gains read back staged with one update of latency.
- **Changes:** `Core/vendor/soundwave/shaders/*.glsl` + `.glslh` — **NEW** (GLSL-first, the
  engine's shader language); `Core/Source/Lux/Audio/AudioAcousticKernel.h/.cpp` — **NEW** the
  seam class holding `Ref<ComputePass>` + `Ref<PipelineCompute>`, staging `StorageBuffer`s and the
  update-index ring; `Scene.cpp` — the dispatch recorded once per acoustic update through
  `Renderer::Submit` from the window at 941; barriers via `PipelineCompute::BufferMemoryBarrier`
  (PipelineCompute.cpp:196-235) the way compute passes already do (SceneRenderer.cpp:9581).
- **Playbook:** new pipeline (once, cached, invalidated); new descriptor set — grep the
  `(set, binding)` pair against `Editor/Resources/Shaders/` before it is taken; shader
  dependency registered so hot-reload invalidates; new files → regenerate projects.
- **Thread and lifetime:** dispatch recorded in the render thread's segment, never waited on from
  main; buffers retired through `Renderer::SubmitResourceFree` on the render-frame tag rule;
  scene switch drains the frame before retirement.
- **Verification:** CPU reference (kept compiled-in for tests) vs the shipped GPU path within the
  authored tolerance — **≤ 0.5 dB on `OcclusionGainLF` and `ReverbSend`, fixed reduction order in
  validation builds** (Decision 10); corner/doorway audibility re-checked on the GPU path; stone
  room ≠ corridor re-checked; Windows debug + Release, Linux debug + `debug-as`, Tracy's GPU
  context shows the dispatch inside the frame budget; the renamed budget row (`VA warning` →
  kernel) reports the same *work* budget.
- **Done when:** the numbers on screen come from the GPU, agree with the reference inside the
  tolerance, and no main-thread code ever waits on the GPU.

### Phase 5 — The whole surface: air absorption, caustics, and deeper bounces

- **Goal:** Decision 4's full reach is real: air absorption in dB/metre along the path, caustic
  spread, and bounces past two — each authored, serialized, validated and diagnostic-banded.
- **Changes:** `Core/vendor/soundwave/` — absorption medium row, caustic scatter, bounce budget;
  `AcousticMaterialSettings.h` — per-material absorption (Decision's plan's call, confirm — § 1);
  `AudioOcclusionSettings.h` — cadence/bounce/depth rows; project-settings panel
  (`ProjectSettingsWindow.cpp:928-933` is where the material knobs already show); bank-validation
  and the diagnostics overlay for each new number; **this phase cannot land until the two § 1
  open answers arrive.**
- **Verification:** a 60-metre courtyard shout must stay audible at a stated dB — the double-count
  trap (§ 9.11) is the check; bounces 1-4 each measurable in the overlay.
- **Done when:** every knob is authored, saved, validated and observable, and no distant source
  is faded twice by accident.

### Phase 6 — Feature complete: takeover, files, editor, docs

- **Goal:** a complete feature by the rules — the kernel drives Play, the mode field is gone from
  project files, and the docs stop describing an SDK that no longer colors the design.
- **Changes:** `AudioOcclusionSettings.h:15` — `AudioOcclusionSource` **removed**; the scene/project
  reader must load old audio blocks without it (Decision 5, silently); `Scene.cpp:940`/`:965`
  collapse to the kernel; editor path overlay (fresh `(set, binding)`, follows `Rendering.md §
  Editor debug geometry`), Help page, sample-project showcase; `ScriptCore` — two floats, unchanged
  (Decision 16); identity = name for authoring + UUID at runtime (Decision 17); docs in the same
  change: `Architecture-LuxEngine.md` § 2.10 rewritten (the SDK paragraphs: one listener, pooled
  worlds, the 143 MiB/world leak, join-then-mutate, node bounds, material IDs — stop being true),
  `Threading.md` and `Rendering.md` rows for the new dispatch (Decision 18), a new shared doc and
  a manual page, `README.md:446`, `docs/AUDIO_DYNAMIC_GEOMETRY.md`, `docs/AUDIO_SURFACES.md`,
  `docs/AUDIO_ZONES.md`, `docs/AUDIO_PERFORMANCE_VALIDATION.md:12`,
  `docs/AUDIO_DESKTOP_PLATFORMS.md:46-53/64-74`.
- **Verification:** `scripts/Lux-BuildTest.ps1`, `make config=debug Core Runtime` on Linux, the
  golden runs (one run, one artifact check per `Building.md`), the showcase driven in the editor,
  every new UI state exercised, `cr` before any commit.
- **Done when:** the feature works in the editor and a runtime build on both platforms, with
  old projects loading and playing the new path.

### Phase 7 — The dependency is gone from build, link, and package

- **Goal:** nothing Vercidium remains anywhere: no include, no link line, no deployed DLL, no
  option text, no test that needs it.
- **Changes:** `Dependencies.lua` — delete :21 `VASDKRoot`, :23/:27 `ValidateAudioSDK`, :40/:41/:43
  the deployed files, :185-192 the `VARay` block; `premake5.lua:32` the call itself;
  `Editor/premake5.lua:61/:119` and `Lux-Runtime/premake5.lua:57/:120` copy lines;
  `Editor/Source/RuntimeExportUtils.cpp:103/:107` library lists; the audio headers stop
  including `vaudio.h` (`AcousticMaterial.cpp:9`); `RaytracedAudioScene.h/.cpp` and its pooled
  worlds deleted with their tests; `tests/audio/run.py` and `README.md:12` updated (the
  dependency they still name); `docs/AUDIO_DESKTOP_PLATFORMS.md` § setup lines;
  `Core/vendor/VA_RAY/` deleted locally (untracked — `.gitignore:18`, so no history act).
- **Playbook:** `Building.md` — a `Dependencies.lua` edit changes generation; both generators must
  be exercised; new option text removed from `BuildOptions.OPTIONS` review.
- **Verification:** Windows Release + **Dist** (no shader compiler, no Tracy — the kernel must not
  need either); `make config=debug Core Runtime` + `debug-as` on Linux; the exported game's file
  list re-measured (feeds `NEXT_UP_BACKLOG.md:92` 4.15); `tests/audio/run.py` green with no VA SDK
  present; `send-pr` only once § 3's pending chore is settled.
- **Done when:** a clean checkout with **zero** Vercidium present builds, runs, and exports.

---

## 6. Verification protocol

Every phase: Windows `premake5 vs2022` → Debug/Release/Dist and `premake5 gmake2` → `make
config=debug Core Runtime` (a `Dependencies.lua`/`Soundwave` edit changes generation on both —
`Building.md`: premake does not regenerate itself); Debug + Debug ASAN where lifetime changes;
`scripts/Lux-BuildTest.ps1`, `scripts/Lux-GoldenRun.ps1`, `scripts/lm9-golden-run.py`,
`scripts/Validate-AudioBanks.ps1`, `python tests/audio/run.py`; `cr` before any commit; verified
by artifact timestamp, never exit code; **one run, one artifact check**.

| Phase | What is proven | The claim it retires when it passes |
|---|---|---|
| 1 | shipped occlusion numbers reproduce through the contract, CPU-to-CPU | "the contract renamed the maths and broke it" |
| 2 | corner + doorway audibility; straight paths unchanged at depth 0 / grid 1 / bounces 1 | "propagation is only a wider ray" |
| 3 | stone room ≠ corridor at equal distance; a missing reverb bus is *reported* | "reverb is still a distance falloff"; "silent project" |
| 4 | GPU within the authored ≤ 0.5 dB of the CPU reference; dispatch inside the frame budget; no main-thread GPU wait | "the GPU path was never checked against anything"; a hitch papered by a wait |
| 5 | air absorption, caustics, deeper bounces measurable and authorable | "in scope but not shipped" (Decision 4's own word: *full, now*) |
| 6 | takeover in the editor and a runtime build on Linux; old files load onto the new path | "works only in the editor"; "saved projects break" |
| 7 | clean checkout builds, runs, exports with zero Vercidium | "a leftover transitive include or a deployed DLL"; Dist still links it |

---

## 7. Ordering and parallelism

Phase 1 → **2** → 3 → **4** (GPU port, after the algorithm is right on CPU — the user's "no
mistakes" and Decision 10's tolerance both need a CPU oracle to compare to) → 5 → 6 → 7.
Phase 1's authored-defaults table can run in parallel with Phase 2's solve. Phases 5's three
features can run in parallel once Phase 4 is verified, on different files. Phase 7 sits behind
§ 3's pending chore only for *shipping*, not for building.

| Can run in parallel | Blocks |
|---|---|
| the research briefs (§ 10.5, still not captured) | Phase 2's grid choice, Phase 3's histogram window, Phase 4's shader structure |
| Phase 6's Help page and showcase drafting | nothing until the features they describe exist |

---

## 8. First action and hand-off

Decisions 1-4, 5-6, and the shape/GPU/verification set are answered, so **Phase 1 can start now**:
`/dev` implements it (Soundwave's first files plus the authored defaults), `/cr` reviews before
commit, `/send-pr` ships. Two things must happen first, and neither is code:

1. Answer the four open items in § 1 (air-absorption model, settings placement, CPU fallback, and
   whether `origin/feature/asterwave-audio` is the base or superseded — § 10.4).
2. **Commit this document.** It is staged and has never been in any branch or worktree; one index
   reset or discard deletes the only record of everything above (§ 9.9).

---

## 9. Risks

- **9.1 The greenfield rule still holds** even though the legal gate is retired. VA's own ray
  occlusion does not respond to geometry (`docs/vercidium-repro/occlusion_output.txt`), so there is
  no upstream behaviour to match, and the decompilation repo cannot inform the design — it can only
  disprove published claims. Soundwave is authored from the engine's own captures and published
  acoustic concepts. The user's own sentiment that "VA's way of doing it was so much better" sets
  *scope* (rewrite, don't extend), not design input.
- **9.2 The disclosure chore is still load-bearing**, just not a gate: § 3. Ask before the repo
  becomes public.
- **9.3 Decision 2's shape is the one the clause speaks about.** The user chose a named library;
  that is their call, recorded, and § 3 keeps it visible. This plan does not claim the shape is
  unambiguously permitted.
- **9.4 Render meshes as acoustic geometry** (Decision 9) means a low-detail render mesh blocks
  sound it should not, and a collider-less wall passes sound. The user's reason is that today's
  sample scenes are basic; the honest fix is the documented physics-collider path (Decision 9's
  future note) plus `NEXT_UP_BACKLOG.md:106` row 5.8 (per-submesh geometry) as the follow-up.
- **9.5 The GPU kernel is not free of the render-frame coupling.** A `Renderer::Submit` from the
  audio window rides the render command queue, so the update-index only advances when a frame is
  rendered. Editor minimized, paused Play, or a headless test that never renders must not
  silently stall the kernel — the plan requires a verifyable stall report, not a fallback.
- **9.6 Reverb without an authored bus is invisible**; today only `Occlusion` warns (§ 2.10:1009).
  Phase 3 names the new validation; a phase that ships reverb without it is not complete.
- **9.7 Bands are NEW, not inherited.** The old plan asserted MID/NEAR/FAR existed in
  `AcousticMaterial.h`; they do not (:81-87 are LF/HF pairs). Any band beyond LF/HF is a
  diagnostic surface per Decision 6 and needs its own authored definition.
- **9.8 The audio test suite currently requires the SDKs** (`tests/audio/README.md:12`), and
  `OcclusionTests.cpp:11`/`:16` read SDK-derived defaults. Phase 7 changes break those tests unless
  updated in the same change — that is in Phase 7's list, and skipping it is the silent failure.
- **9.9 Durability.** This document is `A` in `git status`: staged, never committed, absent from
  HEAD, every branch and all 12 worktrees (§ 8 item 2).
- **9.10 Old files must still load.** Removing `AudioOcclusionSource` (Decision 5) touches
  `AudioOcclusionSettings.cpp:27` validation and the project reader; a missing field must not fail
  a load, and the engine's audio assertions must be re-read before the change lands.
- **9.11 The double-count trap** — FMOD fades by distance and the kernel fades dB/metre along the
  path; Phase 5 cannot land without the § 1 answer, and shipping both without a decision makes
  distant sources effectively silent.

## 10. Open questions

- **10.1 Design (user, unanswered):** air-absorption model; where the new tunables live; whether a
  CPU path is required for compute-less devices and headless tests. Each is in § 1 with the plan's
  call — veto it or answer it, then Phase 5 and the Phase 4 fallback rule.
- **10.2 Mechanism:** does a `ComputePass` in this engine record on the compute queue family, and
  how is it invalidated when that family is absent? Read `Renderer.cpp:1138/:390`,
  `VulkanDevice.cpp:667-668`, and NRI's segment selection before Phase 4's dispatch text is
  written; do not assume the answer from the queue's existence.
- **10.3 Evidence:** the decompilation repo's scale figures (309 acoustic checks, 285 structures,
  561 changelog entries, `3e-5`/`5e-6`) can disprove published claims but are not design input;
  the plan does not use them.
- **10.4 The unmerged branch (user decision, blocking Phase 5+):** is `origin/feature/asterwave-audio`
  the base or superseded? It already replaced Vercidium with Asterwave (`c7bd3f87`), made Asterwave
  an opt-in pinned dependency (`4d41fb24` `Dependencies.lua:21-71`, `scripts/FetchAsterwave.py`,
  `--raytraced-audio`), added a stubbed `RaytracedAudioScene` and `AsterwaveMaterials.h`, and
  carries its own `docs/AUDIO_NATIVE_BACKEND_PLAN.md` whose stated goal is to replace **FMOD**
  (miniaudio) with "Non-goals: Replacing Asterwave". This plan replaces **acoustics** and keeps
  FMOD. The two are incompatible as written: if that branch is the base, Soundwave has to say how
  it sits against a stubbed wrapper, an already-rewritten § 2.10, and an unagreed FMOD
  replacement; if it is superseded, say so and why its dependency machine stays.
- **10.5 Prior art — not done.** The 2026-10-30 session's web searches returned junk; nothing was
  captured, so Phases 2-5's algorithm choices rest on their own verification, not literature.
  Re-run the research before Phase 2 (grid/BVH choice), Phase 3 (echogram window), Phase 4
  (compute-solve structure), and Phase 5 (air absorption, caustics).
- **10.6 Backlog notes:** `NEXT_UP_BACKLOG.md:91` (4.14 → § 3's pending chore), :106 (5.8 per-
  submesh acoustic materials → Decision 9's follow-up), § 2.10:556-557 (Linux audio unverified →
  Phases 2/4/6's Linux runs) — each needs a note pointing at this plan; this plan does not edit
  those rows.
