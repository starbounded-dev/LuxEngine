# LuxEngine — Threading & Concurrency Model

Putting code on the wrong thread here produces Vulkan validation errors, torn ECS reads, or a
corrupted render command queue. This doc lists every thread context, what is allowed on it, and how
to move between them.

---

## The single most important fact

**LuxEngine runs a real render thread by default, including in the editor.**

`ApplicationSpecification::CoreThreadingPolicy` defaults to `ThreadingPolicy::MultiThreaded`, and
`Editor/Source/LuxEditorApp.cpp` reads the user setting `Core.ThreadingPolicy` from `App.lsettings`
with these defaults:

| Platform | Editor default | Why |
|---|---|---|
| Windows | **`Multi`** | Real `std::thread` render thread |
| Linux | **`Single`** | Render-thread races with Wayland/Vulkan swapchain management are unresolved |

The runtime (`Lux-Runtime`) also defaults to `Multi`, overridable per project.

So on Windows, main and render are **different threads**, and cross-thread reasoning is required.
Do not assume "editor means single-threaded" — that is true of some other Hazel-derived engines and
is **false here**. The user can force `Single` from the Application Settings panel, so correct code
must work under both policies.

`ThreadingPolicy` also gates the `JobSystem`: `Single` initializes it with zero workers, which makes
every `Submit`/`ParallelFor` run inline. One setting therefore disables *all* worker parallelism,
not just the render thread.

---

## Quick reference

| Context | Allowed | Forbidden |
|---|---|---|
| **Main (application) thread** | ImGui, GLFW/OS window + input, scene/ECS mutation, asset registry mutation, filling the render command queue via `Renderer::Submit` | Direct nvrhi command-list recording; long blocking work (file I/O, network, subprocess, asset import) |
| **Render thread** | nvrhi command lists, GPU submission, swapchain present, `RT_*` functions, resource release queue | ImGui *building* (see below), ECS access, `Application::Get()` state mutation |
| **JobSystem workers** | Data-parallel compute over disjoint indices (transform/cull fan-out) | ImGui, nvrhi, ECS mutation, anything order-dependent |
| **Asset worker** (`EditorAssetSystem` / `RuntimeAssetSystem`) | File I/O, decode, import; creating GPU resources *via* `Renderer::Submit` | Touching the asset registry directly; ImGui; direct nvrhi calls |
| **Jolt's internal pool** | Physics jobs Jolt schedules itself | Anything engine-side |
| **Simulation thread** | (scaffolded, currently unwired — see below) | — |

Detection helpers: `Application::IsMainThread()` / `Application::GetMainThreadID()`, and
`RenderThread::IsCurrentThreadRT()`. Use them in `LUX_CORE_ASSERT` to pin a contract down.

---

## The frame loop

`Application::Run()` (`Core/Source/Lux/Core/Application.cpp`), per iteration:

1. `m_RenderThread.BlockUntilRenderComplete()` — wait for the previous frame's GPU submission.
2. `ProcessEvents()` — poll OS events **while both threads are idle**.
3. `RenderImGui()` + `m_ImGuiLayer->End()` — build the UI. Dear ImGui's GLFW backend performs native
   window operations that must run on the thread owning the window, so the UI is *built and
   snapshotted* here, on the main thread, while the render thread is idle.
4. `m_RenderThread.NextFrame()` then `Kick()` — hand the previous frame's queue to the render thread
   and start it.
5. `Renderer::BeginFrame()`, `Renderer::ExecuteBackgroundThreadSubmits()`, layer `OnUpdate`,
   `DiscordSocial::Update()`, `m_ImGuiLayer->SubmitDrawData()`, `Renderer::EndFrame()`.
6. `m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % Renderer::GetConfig().FramesInFlight`.

Two consequences worth internalising:

- **ImGui is built on the main thread; only immutable GPU draw work crosses to the render thread.**
  Never call `ImGui::` from inside a `Renderer::Submit` lambda.
- **`ExecuteBackgroundThreadSubmits()` runs before layer updates**, so GPU resources a background
  asset load created already exist before any draw that might use them this frame.

---

## `Renderer::Submit` — three paths, picked automatically

`Renderer::Submit(lambda)` (`Renderer/Renderer.h`) is thread-aware, and the branch it takes matters:

```cpp
if (Application::IsMainThread())      → allocate directly in the render command queue (lock-free)
else if (RenderThread::IsCurrentThreadRT()) → run the lambda inline, immediately
else                                   → SubmitBackgroundThreadWork(...)  (deferred, thread-safe)
```

Why each branch exists:

- **Main thread** is the *sole producer* of the submission queue, so it can fill it without a lock.
  This is the common case.
- **Already on the render thread** (e.g. GPU work triggered from ImGui rendering, which itself runs
  as a render command): running inline is required. Queuing from here would race the application
  thread filling the same single-producer queue and corrupt it.
- **A background thread** (asset worker creating GPU resources for a streamed texture or mesh) must
  not write the single-producer queue at all. The work goes to a thread-safe queue drained by
  `Renderer::ExecuteBackgroundThreadSubmits()` on the main thread, once per frame.

**Do not "optimise" this into a single unconditional queue write.** Each branch is load-bearing, and
the failure mode of getting it wrong is a corrupted command buffer, not a clean crash.

`Renderer::SubmitResourceFree` mirrors the same logic against the per-frame-index resource release
queue (`GetRenderResourceReleaseQueue(index)`), so a resource freed mid-frame is destroyed only once
the GPU is done with that frame index.

### The `RT_` prefix

A function named `RT_*` (`RT_GetCurrentFrameIndex`, `RT_BindMaterialDescriptorSet`,
`RT_BeginGPUPerfMarker`) may only run on the render thread — i.e. from inside a `Submit` lambda or
from render-thread code. Calling one from the main thread reads the wrong frame index at best.
Non-`RT_` counterparts exist where both are meaningful (`GetCurrentFrameIndex` vs
`RT_GetCurrentFrameIndex`); pick by which thread you are on, not by which compiles.

---

## Render thread

`Core/Source/Lux/Core/RenderThread.h`, platform implementations in
`Core/Platform/{Windows,Linux}/*RenderThread.cpp`.

A three-state handshake (`Idle` / `Busy` / `Kick`) with `Wait`, `WaitAndSet`, `Set`. The application
side drives it through `NextFrame()`, `Kick()`, `BlockUntilRenderComplete()`, and `Pump()`.

Under `ThreadingPolicy::SingleThreaded` no thread is spawned; the same calls collapse to synchronous
work on the caller. That is why `Pump()` exists — `Application` calls it to render a single frame
outside the main loop (e.g. during a resize).

`Renderer::WaitAndRender(RenderThread*)` is the render thread's body: it swaps the double-buffered
command queues (`Renderer::SwapQueues`) and executes the submission queue.

---

## JobSystem

`Core/Source/Lux/Core/JobSystem.h`. A small fork-join pool for data-parallel engine work,
deliberately separate from the render thread (which owns GPU submission) and from Jolt's internal
physics pool.

Initialized in the `Application` constructor:

```cpp
uint32_t jobWorkers = 0;
if (policy == ThreadingPolicy::MultiThreaded)
    jobWorkers = max(1, hardware_concurrency() > 2 ? hardware_concurrency() - 2 : 1);
JobSystem::Init(jobWorkers);
```

Two cores are reserved for the main and render threads, leaving headroom for Jolt.

- `JobSystem::Submit(fn)` — fire-and-forget; runs inline when there are no workers.
- `JobSystem::ParallelFor(count, fn, minChunk)` — splits `[0, count)` across the workers **and the
  calling thread**, then blocks until every element is done. Runs fully inline when single-threaded
  or when `count <= minChunk`.

`fn` must be safe to run concurrently across disjoint indices — no shared writes without
synchronisation. The current production use is `Scene::SyncRenderScene`'s transform/bounds
computation (`Scene.cpp`), which writes only per-item output slots.

Because `ParallelFor` also executes on the calling thread, it is **not** a place to block: a job that
waits on the main thread deadlocks.

A job body must not name a `thread_local` from the dispatching function. Lambdas never capture
`thread_local` (or `static`) variables, even with `[&]`, so on a worker that name resolves to *that
worker's* instance, which is usually empty. Bind a local reference first and capture that
(`auto& items = s_Scratch; [&items](size_t i) { ... }`). The bug only appears past `minChunk`, when
work first reaches a worker.

---

## Asset loading

`Asset/AssetSystem/EditorAssetSystem.h` (and its runtime counterpart) owns a single worker `Thread`
with a load queue (mutex + condvar) and a finished queue.

The contract:

- The worker does file I/O, decode, and import.
- It never touches the asset registry. Results are pushed onto `m_FinishedQueue`.
- The main thread calls `AssetManager::SyncWithAssetThread()` → `SyncLoadedAssets(...)` to drain the
  finished queue and commit into the registry.
- GPU resources created during a background load go through `Renderer::Submit`, which routes them via
  `SubmitBackgroundThreadWork` (see above).

`AssetManager::GetAsset<T>(handle)` is synchronous and will load on the calling thread if needed.
`GetAssetAsync(handle)` returns an `AsyncAssetResult` immediately; pump it with
`SyncWithAssetThread()`.

**Never mutate the registry from a worker**, and never assume an async result is ready without
checking — that is the shape of the outstanding asset-upload race noted in project history.

---

## Simulation thread — scaffolded, not wired

`Core/Source/Lux/Core/SimulationThread.h` implements a Kick/Idle handshake mirroring the render
thread, with the intended contract documented in the header: the simulation thread becomes the sole
writer of the scene ECS, and the main thread may only read render state from a captured
`FrameRenderPacket` between `BlockUntilComplete()` and `Kick()`.

**As of now nothing constructs one.** `ApplicationSpecification::EnableSimulationThread` exists,
defaults to `false`, is only honoured under `MultiThreaded`, and has no consumer in `Core`,
`Editor`, or `Lux-Runtime`. Treat it as reserved scaffolding.

The prerequisite work *is* real and in place, though: `Scene::BuildRenderPacketEditor` /
`BuildRenderPacketRuntime` capture all renderer-relevant ECS state into a `FrameRenderPacket`, and
`Scene::SubmitRenderPacket` replays it without touching the registry. If you are adding renderer
state, **add it to the packet** — bypassing it by reading the ECS during submission is what would
break the simulation-thread split later. See `Renderer/FrameRenderPacket.h`.

Note the const-ness convention there: `Build*` are non-const (EnTT owning groups reorder the
registry), `SubmitRenderPacket` is const.

---

## Scene / ECS

`entt::registry` is **not** thread-safe. Scene mutation — creating/destroying entities, adding or
removing components — is main-thread only.

Deferred destruction exists for a reason: `Scene::SubmitToDestroyEntity` queues into
`m_PostUpdateQueue` rather than destroying immediately, because destroying an entity mid-iteration
invalidates views. Use it from inside any iteration.

Identify entities by `UUID` across frames, serialization, or scene copies. `entt::entity` handles are
not stable.

---

## Events

`Application` has a two-stage event queue guarded by `m_EventQueueMutex`:

- `QueueEvent(fn)` / `DispatchEvent<TEvent>(args...)` are safe to call from any thread.
- Queued events are **not** processed until `SyncEvents()` marks them ready and `ProcessEvents()`
  runs them on the main thread.

The header explains why: an asset thread dispatching `AssetReloaded` must not have that event
processed until the asset thread has synced its assets back to the main thread. Use
`DispatchEvent<T, /*DispatchImmediately=*/true>` only from the main thread.

---

## FMOD callbacks

Studio callbacks copy stopped, marker, and beat data into bounded mutex-protected queues in
`AudioEventInstance`. Callback userdata is a never-reused token, not a pointer to an engine owner;
destruction removes the token's state before releasing the SDK instance. No Scene, ECS, Coral,
ImGui, or scene playback mutation runs in these callbacks. Programmer-sound callbacks are the
SDK-required exception for sound ownership: CREATE resolves a copied audio-table key using the
callback event's Studio/Core systems and calls Core createSound; DESTROY releases that SDK sound,
even if the wrapper mailbox has already been removed. No bank-owned pointers escape CREATE.
The notification mutex is not held across programmer creation/release. Playback status and sound
length are copied into the mailbox; callback errors are reported on the main thread. `Scene::OnUpdateRuntime` drains music timeline
mailboxes and the audio scripting bridge drains script notifications on the main thread before
script OnUpdate. Only immutable copied payloads cross the callback boundary. Bank generation checks
protect main-thread calls from stale SDK handles; tokens isolate late callback delivery.

Accessibility observes these same mailboxes on the main thread; it does not add SDK callbacks.
Weak source tracking, subtitle/cue dispatch, preference I/O and FMOD mixer configuration all run
on the main thread. It completes source mutations before calling game listeners. Mixer DSPs are
detached before banks unload; ImGui consumes presentation snapshots on the main thread.

---

## Shader compilation

The `(set, binding)` reflection registries in `VulkanShaderCompiler.cpp` are **process-global
statics**, guarded by `s_ShaderBuffersMutex`. Reflection is written to be correct if shaders are ever
compiled off the main thread. If you add a new reflected resource kind, take that lock — the
consequences of not doing so are silent cross-shader binding corruption. See
`.claude/docs/Rendering.md`.

---

## Synchronisation primitives

- `std::atomic<T>` for single flags/counters. `Lux::AtomicFlag` / `Lux::Flag` (`Core/Base.h`) wrap the
  dirty-flag pattern (`SetDirty()` / `CheckAndResetIfDirty()`) — use them instead of hand-rolling.
  On x86_64, `std::atomic<T>` for a `T` wider than 16 bytes (e.g. `PipelineStatistics`, seven
  `uint64_t` fields, in `RenderCommandBuffer.h`) is not lock-free and falls back to libatomic's
  generic compare-and-swap loop — Linux executables must keep libatomic in the link or it fails with
  `undefined reference to '__atomic_load'/'__atomic_store'`. `links { "atomic" }` alone is **not**
  enough under LTO: LTO's initial as-needed scan runs before any `__atomic_*` reference is emitted,
  so the library is dropped. `Editor/premake5.lua` / `Lux-Runtime/premake5.lua`'s `filter
  "system:linux"` block therefore force it with
  `linkoptions { "-Wl,--no-as-needed,-latomic,--as-needed" }`. MSVC doesn't need the equivalent
  because it links the runtime support in statically.
- `std::mutex` + `std::scoped_lock` for compound state.
- `Lux::Thread` (named, joinable) and `Lux::ThreadSignal` (`Core/Thread.h`) for engine-owned threads —
  named threads show up in Tracy and in the debugger, so prefer them over a bare `std::thread`.
- `LUX_PROFILE_THREAD("Name")` at the top of any new thread body.

Hold locks for the minimum time. If you are holding a lock across file I/O or a GPU submit,
restructure: copy out under the lock, do the slow work unlocked, re-acquire to write back.

---

## When you can't tell which thread you're on

Walk up the call graph until you hit one of:

- `Application::Run` / `OnUpdate` / `OnEvent` / `RenderImGui` → **main thread**
- inside a `Renderer::Submit` lambda, or any `RT_*` function → **render thread**
- `Renderer::WaitAndRender` / `RenderThreadFunc` → **render thread**
- `EditorAssetSystem::WorkerThread` → **asset worker**
- a `JobSystem::ParallelFor` / `Submit` callback → **a job worker or the calling thread**

Still unsure? Add `LUX_CORE_ASSERT(Application::IsMainThread(), "...")` (or
`RenderThread::IsCurrentThreadRT()`) and run a Debug build — it tells you on the first frame, and
costs nothing in Release.

## Acoustic geometry and portals

`Scene::SyncAudioGeometry` and its `AudioGeometrySystem` queue are main-thread owned. Runtime
joins `RaytracedAudioScene::WaitForResults()` before adding, removing, retagging or transforming VA
primitives or changing world bounds. The next `OnUpdate()` launches workers only after these edits
and listener/source updates finish. SDK workers receive no ECS pointers. C# portal/motion setters
edit component data on the main thread; they never mutate VA directly. Pause defers queue work.
Portal gizmos are captured as immutable `FrameRenderPacket::AudioZoneLines`, so the render thread
reads neither portal components nor mutable VA geometry. Teardown drains VA before destroying its
primitives/world and clears the scene's queue.

`RaytracedAudioScene::GetResult` and `GetAmbience` read SDK buffers that VA's workers write, so they
are valid only in that window. Code that runs outside it — ImGui panels and overlays, C# scripts —
reads the copies the scene records inside it: `Scene::GetLastAudioAmbience()` and
`Scene::GetAudioSourceOcclusion()`.

Engine occlusion (`AudioOcclusion`) runs on the main thread in the same window, after the VA join
and before `OnUpdate()`. Its Jolt narrow-phase queries run between physics steps (stepping is also
main-thread), so they need no locking beyond Jolt's own body locks, and nothing is handed to SDK
or job threads.

## Audio budgets and validation

`AudioSourcePlayback` and `AudioPerformance` are main-thread owned. Source updates and C# controls
only read/mutate the active scene there. Culled VA emitters are removed after joining the previous
VA batch. `AudioPerformance::Update` samples SDK meters/counts at 4 Hz after Studio update; its bus
groups and its own meter DSPs are detached and released before bank unload. The SDK mixer owns sample processing. ImGui
reads cached values and never traverses the mixer itself. Explicit project validation loads scene,
prefab and table assets on the main thread and inspects banks with a separate NOSOUND FMOD system.
It runs only on demand or during export, never per frame, and passes no ECS pointers to SDK threads.

`Application` calls `AudioEngine::SetApplicationFocused` and `Update` on the main thread even
while minimized. GLFW/ImGui focus queries remain on that thread. Focus mute only changes the Core
master output group's mute; it never changes Studio bus state, event pause flags or scene state.
SDK mixer/streaming work and timeline callbacks continue while unfocused. No platform callback
directly invokes audio, scripts or ECS. Profile selection and bank rebuild/reload remain main-thread
operations; console suspend/resume integration is deferred.
