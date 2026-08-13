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
