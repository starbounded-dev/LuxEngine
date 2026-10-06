# LuxEngine NRI Migration Plan

This plan moves LuxEngine's renderer backend from **NVRHI** (StudioCherno's `hazel` fork at
`Core/vendor/nvrhi`) to **NVIDIA NRI** (NVIDIA Render Interface), and shrinks
`Core/Source/Lux/Platform/Vulkan/` to the small amount of code that really has to talk to Vulkan
directly. It is written to be executed by an autonomous agent over many sessions. Every phase
builds, runs, and is verified on its own, and stopping after any phase leaves the engine shippable.

This is a planning document written on 2026-10-04 against `dev` at `8a1fe83d`. It is **not** the
architecture reference. For what is actually built, read `.claude/docs/Architecture-LuxEngine.md`
(§2.3 Renderer) and `.claude/docs/Rendering.md`. Phases update those docs as they land.

---

## How to execute this plan (read first, every session)

1. **Re-read the Goal card below and the section of the phase you are on.** Conversations get
   summarized; this file does not. If this file and your memory disagree, this file wins. Also read
   the Execution log (Appendix C).
2. **One phase at a time, in order.** Start each phase with `/dev <phase title>`. Do not start a
   phase until the previous phase's exit criteria are met and logged in Appendix C.
3. **Branch and commits.** Run `git branch --show-current` and confirm it prints `dev`. Never commit
   on a detached HEAD. Commit each verified sub-step with the repo's `Area: summary` style, e.g.
   `Renderer: ...`, `Build: ...`, `RHI: ...`. Stage only files you changed: the sample project's
   `AssetRegistry.lzr`, `*.lmat` and `imgui.ini` drift on their own and must stay out of commits.
   **Never push**; the user pushes. End commit messages with the attribution line the harness gives.
4. **Build on Linux.** Run `make config=<cfg> Core` and then `make config=<cfg> Editor`; Core is
   **not** rebuilt by the Editor target. Verify real compile lines, not exit codes: grep the log for
   your changed `.cpp`, for `Linking Core` / `Linking Editor`, and for `error:`. Adding or removing
   files requires regenerating projects (`scripts/Linux-Build.sh` handles premake). See
   `.claude/docs/Building.md`.
5. **Verify by building; runs are the user's.** The agent does **not** launch the editor, the
   smoke test, `golden_run.py` or perf runs — the user asked for this explicitly (2026-10-05). The
   agent's check is the build (step 4) plus the phase's grep/`nm` exit checks. Every run-based
   check in a phase (smoke, goldens, graceful close, validation counts, perf) becomes a 🧑 user
   checkpoint: write the exact command into Appendix C and move on. Do not block the next phase
   on it unless the phase says the next one depends on its numbers.
6. **User checkpoints** are marked 🧑. They need the user: a Windows build or run on the RTX PC,
   GitHub forking or pushing, or a visual judgement. Stop at them, write down exactly what you need
   in Appendix C, and ask.
7. **Stuck rule.** If the same failure survives three distinct fix attempts, stop. Record the
   evidence (log excerpt, validation message, what you tried) in Appendix C and ask the user. Do
   not paper over it: no silencing validation, no `WaitIdle` "fixes", no disabling a pass.
8. **Run `/cr` before each commit.** At the end of each phase, update the shared docs listed in
   that phase in the same commit.

---

## Goal card

- **Goal:** Replace NVRHI with NRI as LuxEngine's only rendering interface, keeping Vulkan as the
  backend. Clean the remaining raw-Vulkan and dead code out of `Platform/Vulkan/`, so that folder
  holds only what must call Vulkan directly: device bring-up, Aftermath, the Tracy GPU context, and
  the SPIR-V shader toolchain.
- **Success criteria (verified in Phase 16):**
  1. `grep -rn "nvrhi" Core/Source Editor/Source Lux-Runtime` returns nothing.
     `Core/vendor/nvrhi` and its premake block are gone.
  2. Golden images for the Part 4 scene set match the Phase 0 NVRHI baseline within tolerance.
     This holds in Debug and Release, on Linux (RADV, Renoir laptop) and on Windows (RTX 4070 Ti) 🧑.
  3. No new Vulkan validation errors or synchronization-validation hazards against the Phase 0
     baseline. Zero NRI-validation errors in Debug.
  4. Render-thread CPU time is no worse than baseline. GPU frame time is within ±3% under the
     `/profile` protocol.
  5. The editor and `Lux-Runtime` work under both threading policies. Exported games work, and so
     does a game exported **before** the migration (old `ShaderPack.lsp`).
  6. `Platform/Vulkan/` contains only: the Vulkan device bring-up, the Tracy Vulkan context,
     `Debug/` (Aftermath), and `ShaderCompiler/`. Each other remaining raw `vk*` call has a
     one-line justification in `Rendering.md`.
- **Non-goals:**
  - a D3D12 or D3D11 backend
  - upscalers
  - ray tracing
  - new render features
  - the bindless plan's phases 1–6
  - changing render output on purpose
- **Constraints:**
  - Phases are shippable.
  - `.luxproj`, `.luxscene`, `.lmat`, the shader cache, and `ShaderPack.lsp` keep loading.
  - Linux defaults to the single-threaded policy; Windows defaults to multi-threaded.
  - Standing rules apply: no temporal techniques, no resumption of the deleted RT/GI/terrain work,
    `LUX_HAS_DX11`/`LUX_HAS_DX12` scaffolding stays, and no new installs for game makers.
- **The user's words:** "we want as less as possible vulkan platform code, so … clean the remaining
  vulkan code … in the platform folder … you can ofc keep swapchain, devices, descriptor set etc."
  The plan must be "extremely specific" and usable by an agent working "for hours maybe days".

---

## Decisions this plan is built on

| # | Decision | Choice | Who | Consequence |
|---|---|---|---|---|
| D1 | Migration staging | **Side-by-side.** NRI wraps the `VkDevice` Lux already creates for NVRHI (`nriCreateDeviceFromVKDevice`). Subsystems move one at a time; NRI records into NVRHI's `VkCommandBuffer` until command buffers move | User, 2026-10-04 | Temporary interop code (Part 2.2), deleted in Phases 12–15. Every phase is verifiable in the running editor |
| D2 | Order versus `docs/BINDLESS_PLAN.md` | **NRI first, bindless after.** Bindless phases 1–6 are paused | User | Bindless gets re-planned on NRI descriptor sets (`VARIABLE_SIZED_ARRAY`, `ALLOW_UPDATE_AFTER_SET`, `MUTABLE`) after Phase 16. Its Phase 0 baseline is folded into this plan's Phase 0 |
| D3 | Nsight Aftermath | **Re-wire it** on the device Lux creates | User | Lux keeps creating `VkInstance`/`VkDevice` itself, because Aftermath needs `VkDeviceDiagnosticsConfigCreateInfoNV` in the device `pNext`. NRI always wraps (never `nriCreateDevice`). A slim `VulkanDevice` stays in `Platform/Vulkan/` |
| D4 | NRI source | **StudioCherno/NRI `hazel` branch**, forked to `starbounded-dev/NRI`. Upstream `NVIDIA-RTX/NRI` `main` is merged in and each hazel patch's status is re-checked | User | Phase 6 is a merge-and-evaluate step with a fallback (upstream plus cherry-picks) if the merge proves unmanageable. Lux owns a fork it can patch |
| D5 | Barrier model | **A Lux `ResourceStateTracker` with resting-state semantics**, the same as NVRHI's `keepInitialState`. Bindings and framebuffers drive the required states, and the render graph declares access kinds to batch barriers at pass entry | Technical | Correctness doesn't depend on the graph being complete. Out-of-graph work (environment maps, mip generation, ImGui, uploads, thumbnails) stays correct. Built and verified on NVRHI first (Phase 4), with NVRHI's tracker as the oracle |
| D6 | Barrier emission during interop | **NVRHI emits the barriers** (`setTextureState` + `commitBarriers`, with automatic barriers off) until NVRHI command lists are gone (Phase 13). Then NRI's `CmdBarrier` takes over | Technical | NVRHI's internal state stays consistent while both libraries record into one command buffer |
| D7 | GPU object lifetime | **Frame-retired deletion queue** keyed by a monotonic render frame number, retired when the GPU completes that frame. Every NRI `Destroy*` runs on the render thread | Technical | Replaces NVRHI's per-command-list reference tracking. Also fixes today's bug where `SubmitResourceFree` lambdas only run at shutdown (Part 0, L13) |
| D8 | Frame slots | **Decouple the "frame in flight" slot from the swapchain image index.** Slot = `RT_FrameNumber % FramesInFlight`, guarded by a CPU wait on frame `N − FramesInFlight` | Technical | Per-frame buffers and descriptor sets can be rewritten safely in place. Fixes slot aliasing when back buffers outnumber frames in flight, and the non-monotonic MAILBOX index. Done in Phase 3 while NVRHI is still the safety net |
| D9 | Descriptor sets | One NRI `DescriptorPool` per `DescriptorSetManager`, `Material` and bindless table, sized from reflection × FramesInFlight. Sets are allocated at bake and updated in place per frame slot | Technical | Mirrors today's per-frame binding sets. Pools are freed through the deletion queue on rebake or reload |
| D10 | Bindless set 4 | One `TEXTURE` range with `VARIABLE_SIZED_ARRAY` + `PARTIALLY_BOUND`, one set per frame in flight. **No** update-after-set yet | Technical | Behaviour stays identical to today's per-frame NVRHI descriptor tables. Update-after-set is left to the re-planned bindless work (D2) |
| D11 | Push constants | **One root constant** per pipeline layout: offset 0, size = max(offset + size) over the reflected ranges, stages = their union | Technical | Matches NVRHI, which emits a single `VkPushConstantRange` (`vulkan-resource-bindings.cpp:1064-1102`) |
| D12 | Uploads | **A Lux `UploadRing`**: per-frame-slot host-upload ring with in-order `CmdCopyBuffer` / `CmdUploadBufferToTexture` at the call site. Not `NRIStreamer` | Technical | Keeps NVRHI's `writeBuffer` ordering inside a command list, which `StorageBuffer::RT_SetData` relies on. The Streamer batches copies, which would change that ordering |
| D13 | ImGui | **Port `ImGuiRenderer`**, not `NRIImgui` | Technical | Keeps the per-texture `ForceOpaque`/`IsGrayscale` flags, the shared texture registry and the multi-viewport path. Keeps `NRI_ENABLE_IMGUI_EXTENSION` off |
| D14 | Swapchain | **The NRI `SwapChain` extension** for the main window, ImGui platform windows, and the runtime | Technical | Deletes about 640 lines of raw-Vulkan swapchain and surface code. Lux's present modes map to NRI flags (Part 2.6) |
| D15 | Wide lines | **Small fork patch "LUX-1"**: the NRI VK pipeline gets `VK_DYNAMIC_STATE_LINE_WIDTH` when `wideLines` is enabled, defaulting to 1.0 on bind. Lux calls `vkCmdSetLineWidth` on the native command buffer for line pipelines | Technical | NRI has no line-width state, but Lux draws 2 px and 4 px lines (`Renderer2D.cpp:137`, `EditorLayer.cpp:506`, `RuntimeLayer.cpp:69`). Quad-expanded lines are the portable follow-up |
| D16 | Where NRI-only code lives | New `Core/Source/Lux/Renderer/RHI/`. NRI-backed classes now in `Platform/Vulkan/` move out in Phase 16 | Technical | `Platform/Vulkan/` ends up as raw-Vulkan code only (user goal) |
| D17 | Serialized formats | **Byte-compatible.** Lux `ShaderStage` keeps `nvrhi::ShaderType`'s `uint16_t` values, and the reflection's `VkDescriptorBufferInfo` field becomes a 24-byte `LegacyDescriptorBufferInfo` | Technical | The shader cache, `ShaderPack.lsp` v1 and old exports keep loading, with no forced re-export |

---

## Part 0 — Where we are (verified 2026-10-04 by reading source; ✅ = built and in use, ⚠️ = built with caveats, ❌ = missing/broken/dead)

| # | Capability | State | Evidence |
|---|---|---|---|
| L1 | Rendering interface | ✅ NVRHI over Vulkan. Lux owns the `VkInstance`/`VkDevice`/queues and passes them to `nvrhi::vulkan::createDevice` | `Platform/Vulkan/VulkanDeviceManager.cpp:664-739` (`CreateDevice`); `Core/vendor/nvrhi` (`StudioCherno/nvrhi`, branch `hazel`) |
| L2 | NVRHI footprint | ⚠️ 63 files reference `nvrhi`: 38 in `Renderer/`, 15 in `Platform/Vulkan/`, 3 in `ImGui/`, 2 in `Core/`, 1 in `Serialization/`, 3 in `Editor/Source`. Vocabulary leaks: `nvrhi::ShaderType` ×142, `Format` ×86, `ResourceStates` ×81, `TextureSubresourceSet` ×35, `TextureDimension` ×33 | `grep -rn nvrhi::`. Public headers that expose NVRHI: `Application.h:19,167`, `Image.h:11,95,115-119,206-212,495-547`, `RenderGraph.h:76`, `Renderer.h:30,141,154,241-242,273`, `Shader.h:12,139-141`, `ShaderPackFile.h:10,37`, `Texture.h`, buffer headers, `Pipeline.h`, `Mesh.h:250,304-308` |
| L3 | Command recording sites | ✅ Centralised. `setGraphicsState` ×2 and `setComputeState` ×2 go through `RenderCommandBuffer::RT_CommitGraphicsState/ComputeState`. `drawIndexed` ×8, `drawIndexedIndirect` ×1, `dispatchMesh` ×1, `writeBuffer` ×8, `writeTexture` ×4, `clearBufferUInt` ×4, `copyTexture` ×3, `setTextureState` ×10, `commitBarriers` ×13 | `RenderCommandBuffer.cpp:363-381`; `Renderer.cpp` (`BeginRenderPass` 1038, `DispatchCompute` 1210, `RenderQuad` 1705, `RenderGeometry` 1754, `SubmitFullscreenQuad*` 1881/1935, `ClearImage` 1822, `CopyImage` 1838); `SceneRenderer.cpp` (`RT_DrawStaticMesh` 8635, `RT_DrawStaticMeshMeshlets` 7275, `ClusterLightCullingPass` 7503, `PreConvolutionCompute` 7983, `BloomCompute` 8268) |
| L4 | Barriers | ✅ NVRHI automatic barriers do almost everything. Resources use `keepInitialState` (resting states). Manual helpers: `PipelineCompute::ImageMemoryBarrier`/`BufferMemoryBarrier` (34 call sites), the mip transition lambdas, and the `Texture2D::GenerateMips` transitions | `Image.cpp:198-214`; `VertexBuffer.cpp:23-24`; `UniformBuffer.cpp:19-20`; `StorageBuffer.cpp:18-19`; `PipelineCompute.cpp:105-259`; `SceneRenderer.cpp:7996-8048, 8324-8387`; `Texture.cpp:670-731` |
| L5 | Render graph | ⚠️ Texture-only, with `Reads`/`Writes` and no access kinds. Buffers are not modelled: cluster passes carry `UntrackedResources`. Aliasing shares one `Image2D` (`SetTransientAliasSource`), not memory | `RenderGraph.h:22-99`; `SceneRenderer.cpp:3819-3880`; `Image.cpp:72-115` |
| L6 | Render passes | ✅ Dynamic rendering inside NVRHI (`vulkan-graphics.cpp:488-510`). Lux's `BeginRenderPass`/`EndRenderPass` are balanced in every file (SceneRenderer 26/26, Renderer2D 5/5, RuntimeLayer 1/1, Renderer 1/1). `EndRenderPass` only closes a marker today, because NVRHI ends passes implicitly | `Renderer.cpp:1038-1141` |
| L7 | Descriptors | ✅ `DescriptorSetManager` (sets 0–3) builds per-frame NVRHI binding sets from reflection. `Material` reuses it. Bindless set 4 is a `BindlessTextureTable` with one NVRHI descriptor table per frame in flight | `DescriptorSetManager.{h,cpp}` (`Bake` 431, `BakeSet` 469, `InvalidateAndUpdate` 648); `VulkanShader::CreateDescriptors` (`VulkanShader.cpp:134-352`); `BindlessTextureTable.cpp:24-151`; `RenderPass::GetBindingSets` (`RenderPass.cpp:134-157`) |
| L8 | Push constants | ⚠️ Reflection writes one range per stage with `Offset = 0`. NVRHI uses only the first item's size with set 0's union visibility | `VulkanShaderCompiler.cpp:~1020-1025`; `VulkanShader.cpp:158-168`; `nvrhi/src/vulkan/vulkan-resource-bindings.cpp:1064-1095` |
| L9 | Shaders | ✅ GLSL-first, compiled to SPIR-V by shaderc. HLSL goes through DXC `-spirv` (`-fvk-invert-y` for vertex stages) with no register shifts. SPIR-V is kept in `VulkanShader::m_ShaderData` | `VulkanShaderCompiler.cpp:408-423, 482-513`; `VulkanShader.cpp:110-130` |
| L10 | Combined image samplers | ⚠️ NRI cannot express them. Found: `EdgeDetection.glsl:35-36` (shader loaded at `Renderer.cpp:557` but no pass uses it), `SSR.glsl:31` (inside `#if HBAO_REFLECTION_OCCLUSION`, compiled out), `ImGui.glsl:45` (unused; ImGui uses `ImGui.hlsl`) | `grep "uniform sampler2D" Editor/Resources/Shaders` |
| L11 | Serialized reflection | ⚠️ Raw `nvrhi::ShaderType` (`uint16_t`) and a raw `VkDescriptorBufferInfo` are written with `WriteRaw`. `ShaderPackFile::ShaderModuleInfo::Stage` is `nvrhi::ShaderType`. Stage strings in the shader-cache YAML come from `nvrhi::utils::ShaderStageToString` | `VulkanShaderResource.h:76-131`; `ShaderPackFile.h:31-40, 60-65` (header `Version = 1`); `VulkanShaderCache.cpp:70,139` |
| L12 | Swapchain | ✅ Hand-written raw Vulkan: surface from GLFW, present-mode selection, acquire/present semaphores, NVRHI event queries for frame pacing, NVRHI `createHandleForNativeTexture` for back buffers. A second instance exists per ImGui platform window | `VulkanSwapChain.cpp` (558 lines); `Window.cpp:306-312, 485-495`; `ImGuiLayer.cpp:244-311`; `RuntimeLayer.cpp:325-335` |
| L13 | GPU lifetime | ✅ *Fixed 2026-10-05 ahead of Phase 3 (see Appendix C): release slots now drain every frame behind a graphics-queue event query.* Original finding: ❌ **The release queue is never drained during a session.** `SubmitResourceFree` allocates into `s_ResourceFreeQueue[frameIndex]`, but those queues are only executed in `Renderer::Shutdown` (`Renderer.cpp:779-783`). It "works" because NVRHI reference-counts every handle and keeps in-flight resources alive. Lux code freely drops handles mid-frame (`Image2D::RT_Invalidate` swaps handles, `StorageBuffer::Invalidate` reassigns) | `Renderer.h:157-185`; `grep GetRenderResourceReleaseQueue`; NVRHI `doc/ProgrammingGuide.md:3` |
| L14 | Frame index | ✅ *Fixed in P3: now `RT_GetFrameNumber() % FramesInFlight`, guarded by a GPU wait in `RT_BeginFrame`.* Original finding: ⚠️ `Renderer::RT_GetCurrentFrameIndex()` returned the **swapchain back-buffer index** (`Renderer.cpp:394-397`). Per-frame arrays have `FramesInFlight` (3) entries and are indexed `% size`. CPU run-ahead is NVRHI event queries with `maxFramesInFlight = 2` | `Window.cpp:117`; `VulkanSwapChain.cpp:463-493`; `RenderCommandBuffer.h:96-110` comment |
| L15 | Uploads | ✅ One shared NVRHI upload list (`Renderer::RecordResourceUpload`/`FlushResourceUploads`). Async transfer is off by default because of barrier legality on the transfer queue | `Renderer.cpp:424-1024` |
| L16 | Queries and markers | ✅ NVRHI timer queries (named and frame-level), raw-Vulkan pipeline-statistics pools (**leaked**: `RenderCommandBuffer.cpp:96` "not destroying these cleanly"), Tracy GPU zones via `TracyVkCtx` owned by `VulkanDeviceManager` | `RenderCommandBuffer.cpp:36-486`; `VulkanDeviceManager.cpp:741-799` |
| L17 | Aftermath | ❌ Not active. Only the dead `VulkanDevice.cpp` path enables it (`VulkanDevice.cpp:264-300`). The checkpoint helper `Utils::SetVulkanCheckpoint` always returns early because `VK_NV_device_diagnostic_checkpoints` is never enabled on the live device | `VulkanDiagnostics.cpp:17-38`; `VulkanDeviceManager.h:189-208`; `.claude/skills/shader-debug/SKILL.md:190-194` |
| L18 | Dead raw-Vulkan code | ❌ Compiled but unused: `VulkanContext`, `VulkanDevice`, `VulkanRenderCommandBuffer`, `VulkanImGuiLayer`, `VulkanAPI`, `Vulkan.cpp` helpers, `VulkanDiagnostics` (inert), `RendererContext`. Also `#if OLD/#if 0/#if DEAL` blocks in `Texture.cpp`, `Image.cpp`, `Framebuffer.cpp`, `Renderer.cpp:1578-1704`, `Window.cpp:314-326`. `Window.cpp:314` puts the context path under `#if OLD`. The 8 `vkCmdPipelineBarrier` calls in `Texture.cpp` are all inside dead blocks | `grep -n "^#if" Renderer/*.cpp` |
| L19 | VMA | ❌ Lux compiles VMA (`Core/premake5.lua` → `vendor/VulkanMemoryAllocator/**`, `vk_mem_alloc.cpp:3` defines `VMA_IMPLEMENTATION`), but `VulkanAllocator::Init` is never called (`VulkanContext.cpp:267` is commented out). Only `GetStats()`'s `VK_EXT_memory_budget` query is live. **NRI-VK also compiles VMA** (`NRI/Source/VK/MemoryAllocatorVK.h:20`), so keeping Lux's copy would mean duplicate symbols | `VulkanAllocator.cpp:22-118, 309-372`; `Renderer.cpp:2195-2199` |
| L20 | Donut boilerplate | ⚠️ `Renderer/DeviceManager.{h,cpp}` (568 lines). Only these are called: `Create`, `CreateDevice`, `InitSurfaceCapabilities`, `GetDevice`, `GetRendererString`, `GetDeviceParams`, the vsync/buffer-count/present-mode setters, `IsTransferQueueAvailable`, `IsVulkanDeviceExtensionEnabled`, `SetDPIScale`, `SetWindowContext`, `Shutdown`, `GetFrameIndex`. `RunMessageLoop`, `AnimateRenderPresent`, `IRenderPass` and the input callbacks are dead | `grep "->Method"` over `Core/Editor/Runtime` |
| L21 | Platform folder size | `Platform/Vulkan/` = 9135 lines, including `ShaderCompiler/` and `Debug/` | `wc -l` |
| L22 | Device features | ✅ Vulkan ≥1.3 required; sync2, dynamic rendering, descriptor indexing, partially bound, variable count, timeline semaphores, BDA; mesh shader, VRS and RT extensions when present | `VulkanDeviceManager.cpp:187-196, 410-607` |
| L23 | Line width | ⚠️ Lux uses 2 px and 4 px lines through NVRHI `dynamicLineWidth` and `graphicsState.lineWidth` | `Pipeline.cpp:251, 274-278`; `Renderer.cpp:1106-1108`; `Renderer2D.cpp:137, 554, 1500-1506` |
| L24 | Topologies | ⚠️ `TriangleFan` is unused but mapped. `LineStrip` was missing from `Utils::GetNVRHIPrimitiveType` and would assert (*mapped 2026-10-05*) | `Pipeline.cpp:37-50` |
| L25 | `PipelineCompute::Execute` | ❌ Dead (no callers), but `RT_CreatePipeline` still creates a `RenderCommandBuffer` (3 command lists) for **every** compute pipeline | `PipelineCompute.cpp:160, 261-275` |
| L26 | NRI facts (v181, `main` d0e1cbf, 2026-10-02) | ✅ Verified in headers and source. MIT license. Explicit barriers only. `nriCreateDeviceFromVKDevice` wraps the device; `CreateCommandBufferVK` wraps a `VkCommandBuffer` without owning it, and its destructor won't free it (`CommandBufferVK.hpp:434-453`). `CreateTextureVK`/`CreateBufferVK`/`CreateFenceVK` exist. `registerSpace` = VK set, gaps are auto-filled (`PipelineLayoutVK.hpp:100-215`). The default viewport flips Y exactly like NVRHI (`CommandBufferVK.hpp:1425-1429`). There is no combined image sampler and no line width. `Create*` is thread-safe; `Destroy*` and `Cmd*` are not (`NRI.h:28-36`) | scratch clone of `NVIDIA-RTX/NRI` |
| L27 | Hazel's NRI fork | ✅ `StudioCherno/NRI` `hazel` is 17 commits ahead of and 433 behind upstream, at NRI v179. Its patches: premake script, vendored VMA, X11 `Window` clash, Vulkan 1.3 push-descriptor crash, swapchain extent clamp (upstream still returns `INVALID_ARGUMENT`, `SwapChainVK.hpp:300-306`), `AcquireNextTexture` failure crash, D32S8 readback stride, NGX/DLSS bits | `gh api repos/StudioCherno/NRI/compare/NVIDIA-RTX:main...hazel` |

**Runtime verification of the above:** none of it has been verified at runtime in this session. It
all comes from reading source and headers. Phase 0 is where runtime facts (validation baseline,
determinism, timings) are established.

---

## Part 1 — Goals and non-goals

See the Goal card. In addition:

- **Image identity is the bar, not "looks fine".** The golden tool (Phase 0) is the arbiter. A
  difference above tolerance is a bug until proven intended.
- **"Work, good, fast" ordering.** Phases first reach parity. Only Phase 16 measures performance,
  and only regressions are fixed. Optimisations go to the follow-up list.
- **Out of scope:**
  - **NRIUpscaler.** DLSS-SR/RR and FSR are **temporal** and are ruled out by the standing
    no-temporal rule; only NIS (spatial) could be a follow-up.
  - **D3D12 through NRI.** That would be a separate decision. The scaffolding is preserved.
  - **NRIDescriptorHeap.** It needs `VK_EXT_descriptor_heap`, which the bindless plan already
    lists as a non-goal.
  - **Ray tracing.**

---

## Part 2 — Design

### 2.1 End state

```
Renderer facade / SceneRenderer / RenderGraph / Renderer2D / ImGuiRenderer   (Lux types only)
        │
        ├── Renderer/RHI/   (NEW; NRI-backed, API-agnostic)
        │     RHIDevice            nri::Device + interface tables + queues + timelines
        │     RHITypes / RHIFormats  Lux enums (ShaderStage, TextureDimension, GPUQueue,
        │                          ResourceState, TextureSubresourceRange, …) ↔ NRI mapping
        │     ResourceStateTracker resting states, per-command-buffer tracking, barrier batching
        │     GPUDeletionQueue     frame-retired destruction (render thread)
        │     UploadRing           per-frame host-upload ring, in-order copies
        │     RHISwapChain         NRISwapChain wrapper (main window, ImGui viewports, runtime)
        │     RHIShader (moved)    pipeline layout from reflection, SPIR-V blobs
        │     DescriptorSetManager (moved)
        │
        └── Platform/Vulkan/   (raw Vulkan only)
              VulkanDevice        instance, debug callback, physical-device pick, VkDevice creation
                                  (features NRI needs + Aftermath pNext), queue families
              VulkanGPUProfiler   TracyVkCtx (needs raw VkDevice/VkQueue/VkCommandBuffer)
              Debug/              Nsight Aftermath (crash tracker, shader DB, checkpoints)
              ShaderCompiler/     shaderc / DXC / SPIRV-Cross → SPIR-V + reflection
        │
        └── Core/vendor/NRI   (starbounded-dev/NRI, branch lux = StudioCherno hazel + upstream main)
```

`Application::GetGraphicsDevice()` (`nvrhi::DeviceHandle`) disappears. Renderer-internal code uses
`RHIDevice::Get()` and `RHIDevice::API()`. NRI headers are included only by renderer `.cpp` files
and `Renderer/RHI/*.h`. They are never included by `Application.h`, `Scene/*`, `Editor/Source/*`,
or the PCH.

### 2.2 Interop model during the migration (Phases 6–14)

Both libraries drive the **same** `VkDevice`, `VkQueue`s and, inside a frame, the **same**
`VkCommandBuffer`. These rules keep that safe. Each is enforced in code (assertion or helper), not
by convention.

- **I1 — One device.**
  - `VulkanDeviceManager` creates the instance, device and queues.
  - NVRHI wraps them, as today.
  - `RHIDevice::Init` wraps them with `nriCreateDeviceFromVKDevice`, passing the exact enabled
    extension list and queue families.
  - Destruction order is: NRI device → NVRHI device → `vkDestroyDevice`.
- **I2 — One queue lock.** Every `vkQueueSubmit`/`QueuePresent` from either library holds
  `RenderCommandBuffer::LockQueue()`. NRI and NVRHI both submit to the same `VkQueue` objects.
- **I3 — Command-buffer handoff.** At `RenderCommandBuffer::RT_Begin`, after NVRHI's `open()`, the
  command buffer creates a non-owning NRI wrapper:
  `WrapperVK.CreateCommandBufferVK({ VkCommandBuffer(commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer)), queueType })`.
  The wrapper is destroyed at `RT_End`. It does not free the `VkCommandBuffer`
  (`CommandBufferVK::~CommandBufferVK`). NVRHI recycles native command buffers, so a wrapper never
  outlives one `open()`/`close()`.
- **I4 — Recording segments.** NRI recording happens only between
  `RenderCommandBuffer::RT_BeginNRISegment()` and `RT_EndNRISegment()` (**NEW**).
  - `RT_BeginNRISegment` does three things in order:
    1. Commits pending tracker barriers through NVRHI.
    2. Calls `commandList->clearState()`, which ends any NVRHI render pass and drops its cached
       pipeline and bindings (`vulkan-commandlist.cpp:98-113`).
    3. Marks the NRI wrapper "layout unknown". The first NRI command after that must be
       `CmdSetPipelineLayout`.
  - `RT_EndNRISegment` requires that no NRI rendering is still open.
  - A Debug assertion fires if NVRHI recording methods run inside a segment.
- **I5 — Barriers.** Until Phase 13 every barrier is emitted through NVRHI: `setTextureState` /
  `setBufferState` / `commitBarriers` on a list with `setEnableAutomaticBarriers(false)`. This
  keeps NVRHI's internal belief correct even for resources used by NRI draws. No NRI `CmdBarrier`
  is used before Phase 13.
- **I6 — Resources.**
  - From Phase 8, NRI **owns** textures and buffers.
  - NVRHI gets non-owning wrappers through `createHandleForNativeTexture` /
    `createHandleForNativeBuffer`, using the same `nvrhi::TextureDesc`/`BufferDesc` values as today,
    including `initialState`/`keepInitialState`.
  - Native objects are destroyed only through `GPUDeletionQueue` after the frame retires. By then
    NVRHI's command lists that referenced the wrapper have retired too.
- **I7 — CPU writes.**
  - NVRHI cannot map memory it did not allocate.
  - From Phase 8, every `mapBuffer`/`unmapBuffer` (`UniformBuffer::RT_SetData`,
    `StorageBuffer::RT_SetData` with `GPUOnly == false`, `VertexBuffer::SetData`) becomes NRI
    `MapBuffer`/`UnmapBuffer`.
  - GPU-side `writeBuffer`/`writeTexture` on wrapped resources keep working (NVRHI stages them)
    until Phase 13.
- **I8 — Toggles.**
  - Every switch of a recording path ships behind a runtime setting so golden A/B comparisons are
    one restart apart: `Renderer.ExplicitBarriers` (P4), `Renderer.NRICompute` (P9),
    `Renderer.NRIGraphics` (P10), `Renderer.NRIImGui` (P11).
  - They live with the existing `Renderer.AsyncTransferQueue` style setting (find its read/write
    site with `grep -rn "AsyncTransferQueue" Core/Source Editor/Source`).
  - Phase 12 deletes all toggles and the NVRHI branches they guard.

### 2.3 Resource state model (Phases 4–5, emitter switch in 13)

- **Lux `ResourceState`** (`Renderer/RHI/RHITypes.h`, P2) uses the same bit values as
  `nvrhi::ResourceStates` (`nvrhi.h:349-377`) while NVRHI exists, so conversion is a
  `static_cast`. It is checked by `static_assert` in `Renderer/RHI/NVRHIInterop.cpp` (**NEW**,
  deleted in P15).
- **Resting state.**
  - Every resource has one, fixed at creation. Values are copied verbatim from today's
    `initialState`:
    - `ImageUsage::Texture` → `ShaderResource`
    - `Attachment` → `RenderTarget`, or `DepthWrite` for depth formats
    - `Storage` → `UnorderedAccess`
    - `VertexBuffer` → `VertexBuffer`
    - `IndexBuffer` → `IndexBuffer`
    - `UniformBuffer` → `ConstantBuffer`
    - `StorageBuffer` → `UnorderedAccess`
    - meshlet buffers → `ShaderResource` (`Mesh.cpp:336`)
    - ImGui VB/IB → `VertexBuffer`/`IndexBuffer`
    - ImGui-owned textures → `ShaderResource`
    - swapchain → `Present`
  - Material textures are ordinary `ShaderResource` resting-state resources. *(Corrected in P1:
    the `setPermanentTextureState` call this line used to cite sat inside a dead `#if 0` block,
    now deleted. Today every `Texture2D` goes through `Image2D::RT_Invalidate`, which sets
    `initialState = ShaderResource` + `keepInitialState = true`, `Image.cpp:193-194`.)* There is
    no Permanent state: `Texture2D::GenerateMips` transitions them, so they must stay tracked.
- **Tracking.**
  - Each `RenderCommandBuffer` owns a tracker for the lifetime of one open command buffer.
  - The tracker records the current state per resource, at subresource granularity
    (mip × layer) when a non-whole range was ever touched.
  - `RT_End` restores every touched resource to its resting state. That is NVRHI's
    `keepInitialState` contract (`ProgrammingGuide.md:47`).
  - The next command buffer therefore always starts from resting states, on any queue.
- **API** (`ResourceStateTracker.h`, **NEW**):
  `Require(const Image2D&, TextureSubresourceRange, ResourceState)`,
  `Require(BufferRef, ResourceState)`, `RequireUAVBarrier(resource)`, `Commit()`,
  `RestoreRestingStates()`, `GetState(…)` (for the Renderer Debugger) and `GetStats()` (barriers
  emitted per frame).
- **Where requirements come from** (all must be covered; Phase 4 lists the exact call sites):
  1. framebuffer attachments
  2. resources in bound descriptor sets
  3. vertex, index and indirect buffers
  4. copy, clear and upload sources and destinations
  5. explicit intra-pass transitions (mip chains)
  6. ImGui textures
  7. the swapchain image
- **UAV→UAV.** When a resource is `Require`d as `UnorderedAccess` and was written since the last
  barrier, the tracker emits a UAV barrier: `nvrhi::utils::TextureUavBarrier`/`BufferUavBarrier`
  in the NVRHI era, an NRI memory-only barrier after that. This replicates NVRHI's automatic UAV
  barriers (`ProgrammingGuide.md:50`).
- **The render graph** (P5) adds `PassDesc::Accesses`. Before a pass executes, its declared
  accesses are `Require`d and committed as one batch. Debug builds report requirements made inside
  a pass that were not declared (`UndeclaredAccess`).
- **Emitter.** `IBarrierEmitter` (**NEW**) has two implementations:
  - `NVRHIBarrierEmitter` (P4–P12)
  - `NRIBarrierEmitter` (P13+), using the mapping in §2.10

### 2.4 GPU lifetime and frame pacing (Phase 3, timeline in 13)

- **Frame counter.** `Renderer::RT_GetFrameNumber()` (**NEW**) is a monotonic `uint64_t`. It is
  incremented by `Renderer::RT_EndFrame()` (**NEW**), which runs right after `Window::Present()` in
  the `Application::Run` present lambda (`Application.cpp:294-298`).
- **Frame slot.** `Renderer::RT_GetCurrentFrameIndex()` returns
  `RT_GetFrameNumber() % FramesInFlight`, changed from the swapchain back-buffer index.
  `Renderer::RT_BeginFrame()` (**NEW**, in the `Window::BeginFrame` lambda at
  `Application.cpp:256-260`) blocks until frame `N − FramesInFlight` has completed on the GPU:
  - NVRHI era: the event query recorded for that frame in `VulkanSwapChain::Present`.
  - NRI era: the graphics-queue timeline value.

  Code that needs the back-buffer index calls `Window::GetSwapChain().GetCurrentBackBufferIndex()`
  explicitly. Today that is `ImGuiRenderer::GetOrCreatePipeline` and the swapchain framebuffer
  lookup.
- **Deletion.**
  - `Renderer::SubmitResourceFree(fn)` enqueues `fn` into `GPUDeletionQueue` with
    `RetireFrame = current RT frame number`. The main thread defers through `Submit`, as today.
  - `Renderer::RT_RetireFrame(F)` runs every entry with `RetireFrame ≤ F` on the render thread.
    It is called when frame F is known complete.
  - `Renderer::Shutdown` waits for idle, then drains everything.
  - `s_ResourceFreeQueue` and `GetRenderResourceReleaseQueue` are deleted.
- **Rule (enforced by review and by a Debug assertion in `GPUDeletionQueue::Enqueue` that checks
  the calling thread):** NRI `Destroy*` is called **only** inside deletion-queue lambdas, on the
  render thread. The exceptions are device shutdown and swapchain recreation, both of which happen
  with the GPU idle.

### 2.5 Descriptors and pipeline layouts (Phase 9)

- **Pipeline layout per shader** (`VulkanShader::RT_CreatePipelineLayout`, **NEW**):
  - One `nri::DescriptorSetDesc` per reflected set `s`, with `registerSpace = s`.
  - One range per binding, sorted by binding: `baseRegisterIndex = binding`,
    `descriptorNum = max(1, ArraySize)`, `flags = ARRAY` when `ArraySize > 1`,
    `shaderStages = ToNRIStages(stage)`.
  - Type map:
    - `UniformBuffers` → `CONSTANT_BUFFER`
    - `StorageBuffers` read-only → `STRUCTURED_BUFFER`; writable → `STORAGE_STRUCTURED_BUFFER`
      (both are `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`, `ConversionVK.h:100-101`)
    - `SeparateTextures` → `TEXTURE`
    - `SeparateSamplers` → `SAMPLER`
    - `StorageImages` → `STORAGE_TEXTURE`
    - `ImageSamplers` (combined) → **reflection error**: unsupported (L10)
  - Set 4 uses `BindlessTextureTable`'s shared desc.
  - One root constant (D11).
  - `PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS`, with `vkBindingOffsets = {0,0,0,0}` at device
    creation because reflected bindings are final SPIR-V numbers.
  - Recreated on hot reload; the old layout is freed through the deletion queue.
  - **Set index vs register space:** `AllocateDescriptorSets(pool, layout, setIndex, …)` takes the
    *position in `descriptorSets`*, not the set number. Keep a `setNumber → setIndex` table on the
    shader.
- **Pools.**
  - `DescriptorSetManager` owns one `nri::DescriptorPool`. Its per-type counts are the reflection
    counts of sets `StartSet…EndSet` × `FramesInFlight`, and `descriptorSetMaxNum` = sets ×
    FramesInFlight.
  - Sets are allocated in `Bake`. Contents are written with `UpdateDescriptorRanges`, for frame
    slot `f` only, from `InvalidateAndUpdate` on the render thread. This is safe because of D8.
  - `OnShaderReloaded` frees the pool through the deletion queue and re-bakes.
- **Views.**
  - `Image2D::GetView(const TextureViewKey&)` (**NEW**) caches `nri::Descriptor*` keyed by
    {type SRV/UAV/COLOR/DEPTH, dimension, format, mip range, layer range}. Views are destroyed with
    the image through the deletion queue.
  - Buffers cache `CONSTANT_BUFFER` / `BYTE_ADDRESS` / `STORAGE_BYTE_ADDRESS` views. Today's raw
    views become `BufferDesc::byteAddress = true`.
  - `Sampler` holds an NRI sampler descriptor.
- **Bindless (D10).** `BindlessTextureTable::Init` creates a static "bindless-only" pipeline
  layout: sets 0–3 empty, set 4 = { range 0, `TEXTURE`, `descriptorNum = capacity`,
  `VARIABLE_SIZED_ARRAY | PARTIALLY_BOUND`, `ALL` stages }. Each table allocates `FramesInFlight`
  sets with `variableDescriptorNum = capacity`. Structurally identical set descs are layout
  compatible. `RT_WriteTable` uses `UpdateDescriptorRanges(set, 0, slot, &srv, 1)`. Capacity comes
  from NRI `DeviceDesc` limits; that replaces the raw `vkGetPhysicalDeviceProperties` at
  `BindlessTextureTable.cpp:28-33`.

### 2.6 Swapchain (Phase 14)

- `RHISwapChain` (**NEW**) wraps `NRISwapChain`.
- The `nri::Window` comes from per-platform translation units (`Core/Platform/Windows/WindowsNativeWindow.cpp`,
  `Core/Platform/Linux/LinuxNativeWindow.cpp`, **NEW**), following the Conventions "separate
  translation units" rule:
  - Windows: `glfwGetWin32Window`.
  - Linux: `glfwGetPlatform() == GLFW_PLATFORM_WAYLAND` → `glfwGetWaylandDisplay()` +
    `glfwGetWaylandWindow()`; otherwise `glfwGetX11Display()` + `glfwGetX11Window()`.
  - The X11/Wayland headers stay in those TUs only, because of X11 macros such as `None`, `Bool`,
    `Window` and `Always`.
- **Present modes:** `vsync` → `VSYNC`; `!vsync && preferImmediate` → `ALLOW_TEARING` (IMMEDIATE);
  `!vsync && !preferImmediate` → no flag (MAILBOX). This follows `SwapChainVK.hpp:225-265`. Never
  combine `ALLOW_TEARING` with `VSYNC`, which would give FIFO_RELAXED.
- **Size:** Lux clamps to the GLFW framebuffer size and skips creation at 0×0. NRI errors outside
  the surface caps; the hazel clamp patch (L27) is a second line of defence.
- **Frame count:** `textureNum = SwapChainBufferCount` and `queuedFrameNum = 2`, today's
  `maxFramesInFlight`.

### 2.7 Uploads and readback (Phases 8 and 13)

- **Phase 8:** NVRHI's upload list keeps working on wrapped resources. CPU-mapped writes move to
  NRI (I7).
- **Phase 13:** `UploadRing` (**NEW**) holds one `HOST_UPLOAD` buffer per frame slot (start
  64 MiB, grows). `RT_WriteBuffer(cmd, dst, data, size, offset)` and
  `RT_WriteTexture(cmd, dst, mip, layer, data, rowPitch)` copy into the ring, require
  `CopyDest` through the tracker, and record `CmdCopyBuffer` / `CmdUploadBufferToTexture`
  immediately, in command order. Allocations larger than the ring get a dedicated staging buffer
  freed through the deletion queue.
- `Renderer::RecordResourceUpload` changes its callback parameter from `nvrhi::ICommandList*` to
  `UploadContext&` (**NEW**) with `WriteBuffer`/`WriteTexture`.
- Async transfer stays default-off. If enabled, copy-queue command buffers do **not** restore
  resting states. They record a pending "acquire" transition that the next graphics command buffer
  applies.
- **Readback:** `Image2D::CopyToHostBuffer`, used by thumbnails, `TextureRuntimeSerializer`
  export and the golden tool, becomes a `HOST_READBACK` buffer + `CmdReadbackTextureToBuffer` +
  submit + fence `Wait` + `MapBuffer`, honouring the row-pitch alignment from `DeviceDesc`.

### 2.8 Threads

| Work | Thread (both policies) | Rule |
|---|---|---|
| NRI `Create*` (textures, buffers, views, pipelines, layouts, pools) | any; today main thread (`Image2D::Invalidate` → `RT_Invalidate` on main) or render thread | NRI: thread-safe (`NRI.h:34`) |
| NRI `Destroy*` | render thread, inside `GPUDeletionQueue` lambdas | NRI: not thread-safe. The Debug assert `RenderThread::IsCurrentThreadRT() \|\| Application::IsMainThread() && policy == SingleThreaded` |
| `Cmd*`, `UpdateDescriptorRanges` for in-use sets, tracker | render thread (`RT_*`, `Submit` lambdas) | as today's "nvrhi command lists on render thread" |
| `QueueSubmit`, `QueuePresent`, `AcquireNextTexture` | render thread under `LockQueue` | single queue lock (I2) |
| Swapchain create/recreate | main thread in `Window::ProcessEvents`, both threads idle | as today (`Window.cpp:544-578`) |
| Asset worker | creates resources only through `Renderer::Submit` | unchanged |

Under `SingleThreaded` the render thread is the main thread, and every rule above still holds.

### 2.9 Serialization compatibility (D17)

- `ShaderStage : uint16_t` uses `nvrhi::ShaderType`'s exact values (`nvrhi.h:770-794`).
- `LegacyDescriptorBufferInfo { uint64_t Buffer, Offset, Range; }` is
  `static_assert(sizeof == 24 && == sizeof(VkDescriptorBufferInfo))`.
- `ShaderPackFile::ShaderModuleInfo` gets
  `static_assert(sizeof(ShaderModuleInfo) == <value measured in P2 before the change>)`.
- Stage strings are copied verbatim from `nvrhi::utils::ShaderStageToString` /
  `ShaderStageFromString`. Find them with `grep -rn "ShaderStageToString" Core/vendor/nvrhi/src`.
- `ShaderPack` header `Version` stays `1`. Nothing else serialized references NVRHI.

### 2.10 API mapping reference

**NVRHI → NRI calls**

| NVRHI (today) | NRI / Lux (target) |
|---|---|
| `createTexture` | `CreateCommittedTexture(device, MemoryLocation::DEVICE, 0, TextureDesc)` |
| `createBuffer` (CPU write) | `CreateCommittedBuffer(…, HOST_UPLOAD, …)` + `MapBuffer`/`UnmapBuffer` |
| `createBuffer` (GPU only) | `CreateCommittedBuffer(…, DEVICE, …)` |
| implicit views | `CreateTextureView` / `CreateBufferView` (cached, §2.5) |
| `createSampler` | `CreateSampler` → `nri::Descriptor*` |
| `createShader` | none. The SPIR-V blob goes in `ShaderDesc` at pipeline creation |
| `createInputLayout` | `VertexInputDesc` (location = running index, matching `vulkan-shader.cpp:174-176`) |
| `createBindingLayout` | `DescriptorSetDesc` inside `PipelineLayoutDesc` |
| `createBindlessLayout`, `createDescriptorTable`, `resizeDescriptorTable` | `VARIABLE_SIZED_ARRAY` range + `AllocateDescriptorSets(…, variableDescriptorNum)` |
| `createBindingSet` / `writeDescriptorTable` | `AllocateDescriptorSets` + `UpdateDescriptorRanges` |
| `createGraphicsPipeline(desc, framebuffer)` | `CreateGraphicsPipeline` (formats in `OutputMergerDesc`) |
| `createMeshletPipeline` | `CreateGraphicsPipeline` with `TASK_SHADER`/`MESH_SHADER` stages |
| `createComputePipeline` | `CreateComputePipeline` |
| `createFramebuffer` | none. `RenderingDesc` with attachment views |
| `createCommandList` / `open` / `close` | `CreateCommandAllocator` + `CreateCommandBuffer` / `BeginCommandBuffer` / `EndCommandBuffer` |
| `executeCommandList` | `QueueSubmit` (signals the queue timeline fence) |
| `queueWaitForCommandList` | `FenceSubmitDesc` wait on another queue's timeline fence |
| `queueWaitForSemaphore` / `queueSignalSemaphore` | `FenceSubmitDesc` with a `CreateFenceVK`-wrapped `VkSemaphore` (binary semaphores ignore the value) |
| event queries | the timeline `Fence` + `Wait` / `GetFenceValue` |
| `createTimerQuery`, `beginTimerQuery`/`endTimerQuery`, `pollTimerQuery` | `QueryPool(TIMESTAMP)` + `CmdEndQuery` ×2 + `CmdCopyQueries` → readback buffer, read when the slot retires (`timestampFrequencyHz`) |
| raw Vulkan pipeline-stat pools | `QueryPool(PIPELINE_STATISTICS)` (fixes the L16 leak) |
| `setGraphicsState` | `CmdBeginRendering` (in `BeginRenderPass`) + `CmdSetPipelineLayout` + `CmdSetPipeline` + `CmdSetDescriptorSet`… + `CmdSetVertexBuffers` + `CmdSetIndexBuffer` + `CmdSetViewports` + `CmdSetScissors` (+ `CmdSetShadingRate`) through a redundant-state cache |
| `setComputeState` | `CmdSetPipelineLayout(COMPUTE)` + `CmdSetPipeline` + `CmdSetDescriptorSet` |
| `setMeshletState` + `dispatchMesh` | as graphics + `MeshShaderInterface::CmdDrawMeshTasks` |
| `setPushConstants` | `CmdSetRootConstants({0, data, size, 0})` |
| `drawIndexed` / `drawIndexedIndirect` | `CmdDrawIndexed` / `CmdDrawIndexedIndirect(buffer, offset, 1, 20, nullptr, 0)` |
| `dispatch` | `CmdDispatch` |
| `writeBuffer` / `writeTexture` | `UploadRing` + `CmdCopyBuffer` / `CmdUploadBufferToTexture` |
| `copyTexture` | `CmdCopyTexture` |
| `clearTextureFloat` (UAV image) | `CmdClearStorage` (STORAGE view) |
| `clearBufferUInt(…, 0)` | `CmdZeroBuffer` |
| `nvrhi::utils::ClearColorAttachment` / `ClearDepthStencilAttachment` | `AttachmentDesc.loadOp = CLEAR` (or `CmdClearAttachments` inside rendering) |
| `setTextureState` / `setBufferState` / `commitBarriers` | `ResourceStateTracker` → `CmdBarrier` |
| `beginMarker` / `endMarker` | `CmdBeginAnnotation` / `CmdEndAnnotation` |
| `createStagingTexture` + `mapStagingTexture` | `HOST_READBACK` buffer + `CmdReadbackTextureToBuffer` + `MapBuffer` |
| `runGarbageCollection` | `GPUDeletionQueue::RT_Retire` |
| `queryFeatureSupport(Meshlets / VariableRateShading / RayQuery)` | `GetDeviceDesc().features.meshShader` / `tiers.shadingRate` / `tiers.rayTracing` (or `features.rayTracing`; verify the field) |
| `createHandleForNativeTexture` (swapchain) | `WrapperVK.CreateTextureVK`, then `NRISwapChain` textures |

**`ResourceState` → NRI `AccessLayoutStage` (emitter, P13).** Start conservative; narrowing stages
is a follow-up.

| Lux / NVRHI state | access | layout | stages |
|---|---|---|---|
| `Unknown`/`Common` | `NONE` | `UNDEFINED` | `NONE` (as "before" only) |
| `ShaderResource` | `SHADER_RESOURCE` | `SHADER_RESOURCE` | `ALL_SHADERS` |
| `UnorderedAccess` | `SHADER_RESOURCE_STORAGE` | `SHADER_RESOURCE_STORAGE` | `ALL_SHADERS \| CLEAR_STORAGE` |
| `RenderTarget` | `COLOR_ATTACHMENT` | `COLOR_ATTACHMENT` | `COLOR_ATTACHMENT` |
| `DepthWrite` | `DEPTH_STENCIL_ATTACHMENT` | `DEPTH_STENCIL_ATTACHMENT` | `DEPTH_STENCIL_ATTACHMENT` |
| `DepthRead` (depth test + sampled) | `DEPTH_STENCIL_ATTACHMENT_READ \| SHADER_RESOURCE` | `DEPTH_STENCIL_READONLY`, or `DEPTH_READONLY_STENCIL_ATTACHMENT` for D32S8. **Verify with validation in P13** (open question Q4) | `DEPTH_STENCIL_ATTACHMENT \| ALL_SHADERS` |
| `CopySource` / `CopyDest` | `COPY_SOURCE` / `COPY_DESTINATION` | `COPY_SOURCE` / `COPY_DESTINATION` | `COPY` |
| `IndirectArgument` | `ARGUMENT_BUFFER` | (buffer) | `INDIRECT` |
| `VertexBuffer` / `IndexBuffer` | `VERTEX_BUFFER` / `INDEX_BUFFER` | (buffer) | `VERTEX_SHADER` / `INDEX_INPUT` (per `NRIDescs.h` AccessBits table) |
| `ConstantBuffer` | `CONSTANT_BUFFER` | (buffer) | `ALL_SHADERS` |
| `Present` | `NONE` | `PRESENT` | `NONE` |

**Formats** (`Renderer/RHI/RHIFormats.cpp`, **NEW**; replaces `Utils::NVRHIFormat`, `Image.h:254-286`)

| `ImageFormat` | `nri::Format` |
|---|---|
| RED8UN | R8_UNORM |
| RED8UI | R8_UINT |
| RED16UI | R16_UINT |
| RED32UI | R32_UINT |
| RG32UI | RG32_UINT |
| RED32F | R32_SFLOAT |
| RG8 | RG8_UNORM |
| RG16F | RG16_SFLOAT |
| RG32F | RG32_SFLOAT |
| RGBA | RGBA8_UNORM |
| SRGBA | RGBA8_SRGB |
| RGBA16F | RGBA16_SFLOAT |
| RGBA32F | RGBA32_SFLOAT |
| B10R11G11UF | R11_G11_B10_UFLOAT |
| BC1 / BC1_SRGB | BC1_RGBA_UNORM / BC1_RGBA_SRGB |
| BC3 / BC3_SRGB | BC3_RGBA_UNORM / BC3_RGBA_SRGB |
| BC5 / BC5_SNORM | BC5_RG_UNORM / BC5_RG_SNORM |
| BC7 / BC7_SRGB | BC7_RGBA_UNORM / BC7_RGBA_SRGB |
| DEPTH32FSTENCIL8UINT | D32_SFLOAT_S8_UINT |
| DEPTH32F | D32_SFLOAT |
| DEPTH24STENCIL8 | D24_UNORM_S8_UINT |
| RGB, SRGB, None | UNKNOWN (unsupported today too) |

**Vertex formats** (`ShaderDataType`, `Pipeline.cpp:18-35`): Float→R32_SFLOAT, Float2→RG32_SFLOAT,
Float3→RGB32_SFLOAT, Float4→RGBA32_SFLOAT, Int→R32_SINT, Int2→RG32_SINT, Int3→RGB32_SINT,
Int4→RGBA32_SINT, Bool→RGBA32_SFLOAT (as today).

**Topology:** Points→POINT_LIST, Lines→LINE_LIST, LineStrip→LINE_STRIP (fixes the missing case,
L24), Triangles→TRIANGLE_LIST, TriangleStrip→TRIANGLE_STRIP, TriangleFan→`LUX_CORE_VERIFY(false,
"unsupported")` (unused, L24).

**Compare op:** Never/NotEqual/Less/LessOrEqual/Greater/GreaterOrEqual/Equal/Always → the same-named
`CompareOp`; `None` → `NONE`.

### 2.11 Engine fit

| Concern | Answer |
|---|---|
| System | Renderer owns everything here (`Renderer/RHI/`, `Platform/Vulkan/`). No Scene, Physics, Script or Asset changes beyond type renames (`Scene`/`Asset` code never names `nvrhi`, verified with `grep -rln nvrhi Core/Source/Lux/{Scene,Asset}` → empty) |
| Thread | §2.8. Every new `RT_*` is render-thread only and runs inline under SingleThreaded |
| Ownership | `Ref<T>` for engine objects. Raw NRI pointers are owned by exactly one Lux object and destroyed only through `GPUDeletionQueue`. Scene teardown (`Renderer.cpp`/`SceneRenderer` destructors) frees through the queue, and in-flight frames finish first by construction |
| Renderer data flow | Unchanged. `FrameRenderPacket` still feeds `SceneRenderer` |
| Renderer invariants | (set, binding) global namespace unchanged. Pipelines and layouts stay cached in `Init`. New `PassDesc` fields go after `DebugName` and are folded into `ComputeStructureHash()` (P5) |
| Serialization | Byte-compatible (§2.9). No `.luxproj`/`.luxscene`/`.lmat` change |
| Editor | Renderer Debugger shows tracker state instead of NVRHI's. RenderStats and pass timings unchanged. One new debug menu entry (golden capture is env-driven, so no UI) |
| Scripting | none |
| Runtime and Dist | `Lux-Runtime` uses the same renderer. Dist: no shader compiler (`Core/premake5.lua` Dist `removefiles`), so pipelines come from `ShaderPack.lsp` SPIR-V. NRI validation is compiled in but off outside Debug. Aftermath and Tracy are out of Dist, as today |
| Linux | NRI needs `NRI_ENABLE_XLIB_SUPPORT=1` **and** `NRI_ENABLE_WAYLAND_SUPPORT=1`; the hazel premake omits the latter. X11/Wayland dev headers are already required by GLFW (`Core/vendor/GLFW/premake5.lua:37-85`). Linux defaults to SingleThreaded; MultiThreaded under Wayland has known pre-existing races (`Threading.md`), so test MT under X11 or Windows |
| Build | New dependency `Core/vendor/NRI`. Removes `Core/vendor/nvrhi`, `scripts/compat/`, `Core/vendor/VulkanMemoryAllocator/`. New files under `Renderer/RHI/` and `Core/Platform/*/` mean regenerating projects |
| Docs | Each phase lists its doc edits. Phases 15–16 do a full sweep of `Rendering.md`, `Threading.md`, `Building.md`, `Conventions.md`, `Architecture-LuxEngine.md`, `shader-debug`, `send-pr`, `plan-le` skill text and their `.agents/skills/*` adapters |
| Product principle | NRI (MIT) is built from source in-tree. NVRHI (MIT) and RTXMU go away. No install for game makers. Record the Editor/Runtime binary size and startup time in P0 and P16 |

---

## Part 3 — Phases

Each phase lists **Changes, Playbook, Thread and lifetime, Verification, Docs, Exit criteria,
Rollback**. "Build" always means Debug and Release of Core, Editor and Lux-Runtime on Linux. 🧑
marks a Windows or visual checkpoint.

---

### Phase 0 — Baseline you can compare against (no renderer behaviour change)

**Goal:** a reproducible NVRHI baseline: golden images, validation counts, performance numbers and
footprint. Every later phase is judged against it.

**Changes**
- `Editor/Source/Tools/GoldenCapture.{h,cpp}` — **NEW**. *(As built: compiled in every config
  and inert unless `LUX_GOLDEN_DIR` is set; no `LUX_DIST` guard was needed because the editor is
  not shipped in Dist.)* Driven by environment variables:
  - `LUX_GOLDEN_DIR` (output directory). Unset means the tool is inert.
  - `LUX_GOLDEN_SCENE` (project-relative `.luxscene`).
  - `LUX_GOLDEN_FRAME` (default 300).
  - `LUX_GOLDEN_EXIT=1` (close afterwards).
  - `LUX_GOLDEN_PERF_FRAMES` (default 300; frames averaged after capture).

  Behaviour:
  - Hooked from `EditorLayer::OnUpdate`. On the first frame it opens the scene. It forces a fixed
    camera: the scene's primary `CameraComponent` entity if present, otherwise a hard-coded editor
    camera pose stored in the tool. Edit mode only, so no physics or scripts run.
  - For the capture it forces **manual exposure** (override `PostProcessSettings` exposure mode)
    and a fixed timestep, so auto-exposure adaptation does not make runs non-deterministic.
    *(As built: it does not override exposure; it logs a warning when the scene uses automatic
    exposure. If the two baseline runs disagree, pin exposure first.)*
  - At frame N it reads back `SceneRenderer::GetFinalPassImage()` with `Image2D::CopyToHostBuffer`
    inside a `Renderer::Submit` lambda. It writes `<dir>/<scene>.lximg`: a header followed by
    raw texels. *(As built: 32-byte header `'LXIM'`, version, width, height, channels, component
    type, `ImageFormat`, bytes per pixel — see `GoldenCapture.cpp` `GoldenImageHeader`.)*
    Raw output means no `stb_image_write` dependency. Note that its implementation lives in
    `AssimpMeshImporter.cpp:34`, which Dist removes.
  - It then averages `RenderCommandBuffer` frame GPU time, `Application` main and render thread
    work times, and the per-pass timings over the perf window into `<dir>/<scene>.perf.json`.
  - With `LUX_GOLDEN_EXIT=1` it dispatches `DispatchEvent<WindowCloseEvent, true>` on the main
    thread, the same graceful-close method recorded in the build/verify memory.
- `tests/rendering/golden_compare.py` — **NEW**, pure Python 3 (zlib only; numpy not assumed).
  - Compares two `.lximg` files: max absolute difference, mean, and the percentage of pixels with
    any channel over `--tolerance` (default 2/255 for 8-bit; 0.002 for float formats).
  - Writes a diff PNG (zlib-encoded) for inspection.
  - Exits 1 when the percentage exceeds `--max-bad-percent` (default 0.05).
  - Supports RGBA8, RGBA16F and RGBA32F.
- `tests/rendering/golden_run.py` — **NEW**.
  - Runs `scripts/Linux-Run.sh <cfg>` with `LUX_SKIP_BUILD=1` and the environment above, for each
    scene in the set: `FMODDemo`, `Benchmark`, `Physics`, `LaptopStart`, `SkyDiver` from
    `Editor/LuxSampleProject/Assets/Scenes/`.
  - Writes to `bin/golden/<label>/`, which is gitignored, so images are never committed.
  - `--compare <labelA> <labelB>` runs `golden_compare.py` over all pairs.
- Feature sweep: `golden_run.py --features` re-runs FMODDemo with `SceneRendererOptions` toggled one
  at a time. The toggles: SSR, GTAO, Bloom, DOF, SMAA, JumpFlood/selection, wireframe, physics
  colliders, debug categories, VRS where supported, and mesh shaders where supported. The tool
  needs a way to set them: pass `LUX_GOLDEN_OPTIONS=SSR=1,GTAO=0,…` and apply it to the viewport
  `SceneRenderer`'s options in `GoldenCapture`.

**Baseline to record in Appendix C, tagged `nvrhi-baseline`:**
- `golden_run.py --label nvrhi-baseline` and a second run `nvrhi-baseline-2`, then
  `--compare nvrhi-baseline nvrhi-baseline-2`. **It must pass.** If it does not, the capture is
  non-deterministic. Find the source (time-based animation, particles, auto exposure, a temporal
  pass like GTAO or SSR history) and pin it in capture mode before going on. Do not loosen the
  tolerance to hide it.
- Debug run with validation: count `Vulkan validation error` and `warning` lines per scene. Also
  run once with `VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`
  (sync validation) and save the unique messages to `bin/golden/nvrhi-baseline/validation.txt`.
- `RenderGraph::RunValidationSelfTests` result: add a `LUX_GOLDEN_SELFTEST=1` path to the tool.
- Perf: the per-scene `.perf.json` in Release, with present mode IMMEDIATE and the frame limit
  off. Runs are unfocused because they are script-launched, so the numbers are only comparable to
  other script-launched runs (`Rendering.md § Present mode`).
- Footprint: `wc -l` of `Core/Source/Lux/Platform/Vulkan/**`, the sizes of
  `bin/Release-linux-x86_64/Editor/Editor` and `Lux-Runtime`, and time-to-first-frame (log
  timestamp of the first `Renderer2D` line minus process start).
- 🧑 Windows: the user runs the same scripts on the RTX PC, or at minimum one FMODDemo golden plus
  perf, with label `nvrhi-baseline-win`.
- **Runtime export baseline:** export the sample project with the NVRHI build to
  `bin/golden/nvrhi-export/` and keep it. Phase 16 runs this old export with the new runtime,
  which checks old `ShaderPack.lsp` compatibility.

**Playbook:** none applies. The tool is not a panel; it adds one source folder.

**Thread and lifetime:** readback runs inside a `Renderer::Submit` lambda (render thread). File
writes there are acceptable for a debug-only tool and are documented. The close event is
dispatched on the main thread.

**Verification:** Build plus regenerate (new files). The capture works in Debug and Release. Two
baseline runs compare clean.

**Docs:** `Rendering.md` gets a new section, "Golden image capture": the environment variables,
the scripts, and the rule "a renderer-critical change runs `golden_run.py --compare`".
`Building.md` notes that `bin/golden` is not committed.

**Exit criteria:** deterministic baseline images for 5 scenes plus the feature sweep, validation
counts, perf JSON and footprint numbers, all recorded in Appendix C.

**Rollback:** delete the tool and scripts. Nothing else depends on them.

---

### Phase 1 — Delete dead Vulkan code, VMA and Donut leftovers

**Goal:** `Platform/Vulkan/` holds only live code. Lux's VMA is gone, which prevents duplicate VMA
symbols once NRI-VK links. Rendering is unchanged.

**Changes.** For every item, grep for live callers **after** removing the dead `#if` blocks, before
deleting.
- Delete `Platform/Vulkan/VulkanContext.{h,cpp}`, `VulkanDevice.{h,cpp}`,
  `VulkanRenderCommandBuffer.{h,cpp}`, `VulkanImGuiLayer.{h,cpp}`, `VulkanAPI.{h,cpp}`,
  `VulkanDiagnostics.{h,cpp}`.
  - **Before deleting `VulkanDevice.cpp`**, record in Appendix C the commit hash that still contains
    it. Phase 7 rebuilds the Aftermath enable sequence from `VulkanDevice.cpp:255-305` through
    `git show <hash>:Core/Source/Lux/Platform/Vulkan/VulkanDevice.cpp`.
- `Platform/Vulkan/Vulkan.{h,cpp}`: delete them once their remaining symbols (`VK_CHECK_RESULT`,
  `VKUtils::SetDebugUtilsObjectName`, `Utils::VulkanLoadDebugUtilsExtensions`,
  `RetrieveDiagnosticCheckpoints`, `VKResultToString`) have no live users. Today's users are all
  inside dead blocks, e.g. `Framebuffer.cpp:538-576` under `#if OLD`.
- Remove these dead preprocessor blocks and the raw Vulkan in them (verify each guard is never
  defined: `grep -rn "define OLD\|define DEAL\|define INVESTIGATE\|define WENEEDTODEALWITHTHIS\|define MEM_INFO"`):
  - `Texture.cpp` blocks at 286, 360, 430, 455-584, 737-908, 949, 1017-1180, 1317-1405, 1411-1498,
    1505-1599
  - `Image.cpp` 35-41, 320-347, 495-578
  - `Framebuffer.cpp` 34-51, 168-233, 359-382, 447-458, 467-577
  - `Renderer.cpp` 1578-1704 (`RT_BindMeshBuffers`/`RenderMesh` `#if 0`) and the commented
    declaration at `Renderer.h:312`
  - `Window.cpp` 314-326

  Leave non-Vulkan `#if TODO` blocks such as `Window.cpp:242` alone, for scope discipline.
- Remove `RendererContext.{h,cpp}`, `Window::m_RendererContext` and `GetRenderContext()`
  (`Window.h:10,81,120`), and `Renderer::GetContext()` (`Renderer.h:74`). Keep `RendererAPI.h`,
  which is API scaffolding.
- `VulkanAllocator.{h,cpp}`: delete them. Move the two live helpers
  (`GetNativeDeviceLocalMemoryBudget`, `QueryDeviceLocalMemoryBudget`, `VulkanAllocator.cpp:22-118`)
  into an anonymous namespace in `Renderer.cpp`, used by `Renderer::GetGPUMemoryStats`
  (`Renderer.cpp:2195-2199`). Allocation counts stay 0, as they already are in practice.
  Phase 8 replaces this with `NRIHelper::QueryVideoMemoryInfo`.
- `VulkanShaderResource.h`: drop `#include "VulkanAllocator.h"` and the `VmaAllocation MemoryAlloc`
  member. It is not serialized (`Serialize`/`Deserialize` at 113-131 don't touch it).
- `VulkanShader.h`: drop `#include "VulkanMemoryAllocator/vk_mem_alloc.h"`. Delete the dead members
  `m_PipelineShaderStageCreateInfos`, `GetPipelineShaderStageCreateInfos`, `m_DescriptorSet`,
  `GetDescriptorSet` ×2, `ShaderMaterialDescriptorSet` and `m_TypeCounts`/`HasDescriptorSet`, if
  grep shows no live users (no `VkShaderModule` is ever created on the NVRHI path). Simplify
  `Release()` and `~VulkanShader()` to match.
- `Core/vendor/VulkanMemoryAllocator/`: delete the folder, and remove `"vendor/VulkanMemoryAllocator/**.h/.cpp"`
  from `Core/premake5.lua` `files`.
- `PipelineCompute`: delete the dead `Execute`, `Begin`/`RT_Begin`/`End`/`SetPushConstants` stubs
  (if unused) and `m_CommandList`. Today every compute pipeline creates a 3-list `RenderCommandBuffer`
  (`PipelineCompute.cpp:160`, L25).
- `RenderCommandBuffer.cpp`: drop `#include VulkanDiagnostics.h` and the `Utils::SetVulkanCheckpoint`
  call (`:8, :354`). It is inert (L17), and Phase 7 reintroduces checkpoints properly.
- `Renderer/DeviceManager.{h,cpp}`: delete `RunMessageLoop`, `AnimateRenderPresent`, `IRenderPass`
  and `m_vRenderPasses`, `PipelineCallbacks m_callbacks`, the keyboard/mouse/window-pos callbacks,
  `SetInformativeWindowTitle`, average-frame-time members, `CreateHeadlessDevice` and
  `EnumerateAdapters` if grep shows them unused. Also delete the matching `VulkanDeviceManager`
  overrides. Keep the `LUX_HAS_DX11/DX12` includes (scaffolding).
- `Renderer2D.cpp:16` and `Renderer.cpp:20,22`: drop the includes of the deleted headers.

**Playbook:** none.

**Thread and lifetime:** no change.

**Verification**
- Build Debug, Debug-AS (compiles), Release and Dist, and regenerate projects (files removed).
- Link check: `nm -C bin/Release-linux-x86_64/Editor/Editor | grep -c vmaCreateAllocator` should be
  0 now (no VMA until NRI).
- Smoke test plus graceful close. Run `golden_run.py --label p1` and `--compare nvrhi-baseline p1`;
  it must pass. The Renderer Debugger memory stats still show a budget.
- 🧑 Windows build only.

**Docs**
- `Rendering.md` § "Architecture in three layers": fix the stale claim that renderer types are
  abstract with Vulkan implementations in `Platform/Vulkan/`; they are concrete classes over NVRHI.
- `Architecture-LuxEngine.md` directory map: VMA removed, `Platform/Vulkan` contents.
- `Building.md`: `Core/premake5.lua` files list.
- `shader-debug` skill §E.5: `VulkanDevice.cpp` is gone; Aftermath returns in P7.

**Exit criteria**
- `grep -rn "VulkanContext\|\bVulkanDevice\b\|VulkanRenderCommandBuffer\|VulkanImGuiLayer\|vk_mem_alloc\|VmaAllocation\|RendererContext" Core/Source Editor/Source Lux-Runtime`
  returns nothing.
- Goldens pass.
- `wc -l Platform/Vulkan/**` recorded.

**Rollback:** `git revert` the phase commits. Purely subtractive.

---

### Phase 2 — Lux RHI vocabulary; NVRHI types out of non-renderer code and public spec structs

**Goal:** engine, editor and serialization code names only Lux types, and the serialized formats
stay byte-identical. NVRHI handles remain inside renderer implementation files.

**Changes**
- `Core/Source/Lux/Renderer/RHI/RHITypes.h` — **NEW**:
  - `enum class ShaderStage : uint16_t`, with all `nvrhi::ShaderType` values (`nvrhi.h:770-794`).
    Add bit operators via a local macro, in the style of `NVRHI_ENUM_CLASS_FLAG_OPERATORS`.
  - `enum class TextureDimension : uint8_t`, identical values to `nvrhi::TextureDimension`.
  - `struct TextureSubresourceRange { uint32_t BaseMip = 0, MipCount = ~0u, BaseLayer = 0, LayerCount = ~0u; }`,
    plus `constexpr TextureSubresourceRange AllSubresources{}`.
  - `enum class GPUQueue : uint8_t { Graphics, Compute, Copy, Count }`, the same order as
    `nvrhi::CommandQueue`.
  - `enum class ResourceState : uint32_t`, identical values to `nvrhi::ResourceStates`.
  - `struct DrawIndexedIndirectCommand { uint32_t IndexCount, InstanceCount, FirstIndex; int32_t VertexOffset; uint32_t FirstInstance; }`,
    with `static_assert(sizeof == 20)`.
  - `struct LegacyDescriptorBufferInfo { uint64_t Buffer, Offset, Range; }`, with
    `static_assert(sizeof == 24)`.
- `Core/Source/Lux/Renderer/RHI/RHITypes.cpp` — **NEW**: `ShaderStageToString`/`ShaderStageFromString`.
  The strings are copied verbatim from NVRHI's `nvrhi::utils` implementation; open it and copy.
- `Core/Source/Lux/Renderer/RHI/NVRHIInterop.{h,cpp}` — **NEW**, temporary until P15:
  `ToNVRHI(ShaderStage)`, `ToNVRHI(TextureDimension)`, `ToNVRHI(TextureSubresourceRange)`,
  `ToNVRHI(GPUQueue)`, `ToNVRHI(ResourceState)` and the reverse conversions. It also holds
  `static_assert`s proving value identity and
  `static_assert(sizeof(VkDescriptorBufferInfo) == sizeof(LegacyDescriptorBufferInfo))`.
- Reflection and serialization:
  - `VulkanShaderResource.h`: `nvrhi::ShaderType ShaderStage` → `ShaderStage` (×4 structs);
    `VkDescriptorBufferInfo Descriptor` → `LegacyDescriptorBufferInfo Descriptor` (×2). Remove
    `#include "vulkan/vulkan.h"` and `nvrhi.h`.
  - `ShaderPackFile.h`: `nvrhi::ShaderType Stage` → `ShaderStage`. Add the `static_assert` on
    `sizeof(ShaderModuleInfo)` (measure first).
  - `ShaderPreprocessor.h`, `VulkanShaderCompiler.{h,cpp}`, `VulkanShaderCache.{h,cpp}` (YAML
    strings through the new functions), `VulkanShaderUtils.h`, `ShaderPack.cpp`: `ShaderStage`
    everywhere.
- `Shader.h`: move `GetHandle()`/`GetHandle(type)`/`GetHandles()` off the abstract `Shader` onto
  `VulkanShader` only. Callers (`Pipeline.cpp`, `PipelineCompute.cpp`, `ImGuiRenderer.cpp`) cast
  with `.As<VulkanShader>()`, as `Pipeline.cpp:96` already does. Then remove `#include "nvrhi/nvrhi.h"`
  from `Shader.h`.
- `Image.h`:
  - `ImageSpecification::Dimension`, `ImageViewSpecification::Dimension` → `TextureDimension`.
  - `SamplerSpecification::AddressMode` (`nvrhi::SamplerAddressMode`) → a Lux
    `SamplerAddressMode` enum with the values actually used (grep).
  - `GetLayerImageView`/`GetMipImageView` return `TextureSubresourceRange`.
  - Keep `ImageInfo` (backend handles) but move it under a clearly marked "renderer backend" section.
  - `nvrhi::TextureHandle GetHandle()` stays until P8.
- `RenderGraph.h`: `TextureDesc::Dimension` → `TextureDimension`. `ComputeStructureHash` already
  folds it; check that the value is unchanged.
- `Renderer.h`:
  - `ClearImage(…, const glm::vec4& clearColor, TextureSubresourceRange)`
  - `QueueWaitForCommandList(GPUQueue, GPUQueue, uint64_t)`
  - `ConsumePendingUpload(GPUQueue, uint64_t&)`
  - `RecordResourceUpload` and `RT_BindMaterialDescriptorSet` keep their NVRHI signatures for now.
    They are renderer-internal and change in P13 and P9.
- `RenderCommandBuffer.h`: `Create(…, GPUQueue queue = GPUQueue::Graphics)`, and `GetQueue()`
  returns `GPUQueue`. The NVRHI members stay private.
- `Application.h`: replace `#include "nvrhi/nvrhi.h"` with `namespace nvrhi { class IDevice; }`.
  `GetGraphicsDevice()` returns `nvrhi::IDevice*`; callers' `nvrhi::DeviceHandle x = …` still
  compiles. This takes nvrhi out of the PCH-wide include graph.
- `SceneRenderer.{h,cpp}`:
  - `nvrhi::DrawIndexedIndirectArguments` → `DrawIndexedIndirectCommand` (`:1257, 6094, 6383, 6765, 8534-8538`, `.h:1163, 1619`)
  - `nvrhi::TextureDimension` → `TextureDimension` (`:1308, 1404, 4572`, `.h:490`)
  - `nvrhi::ResourceStates CurrentState` → `ResourceState` (`.h:502`)
  - `nvrhi::Color` → `glm::vec4` (`:7421`)
  - `nvrhi::CommandQueue` → `GPUQueue` (`:1210, 6934`)
- `Editor/Source/Panels/RendererDebuggerPanel.cpp:133-183`: switch to the Lux enums.
- `Editor/Source/EditorLayer.cpp:166,172`, `Panels/MaterialEditor/MaterialEditorPanel.cpp:57`: call
  a new `ImGuiRenderer::CreateFrameTexture(Ref<Image2D>, TextureSubresourceRange = AllSubresources, bool forceOpaque = false, bool isGrayscale = false)`
  overload. `ImGuiTextureInfo` gains `Ref<Image2D> Image` as its keep-alive; P11 drops the raw
  `nvrhi::ITexture*`. Keep the old overload private for ImGui-owned textures until P11.

**Playbook:** none. This is a vocabulary change, with no component, asset or pass changes.

**Thread and lifetime:** no change.

**Verification**
- Build all configs and regenerate (new files).
- **Shader cache:** start the editor with the existing `Editor/Resources/Cache/Shader/` intact. The
  log must show no stage recompiles beyond the baseline warm start (count `Compiling` lines against
  P0).
- **ShaderPack:** run `bin/golden/nvrhi-export/` (the P0 export) with **the P2 Lux-Runtime binary**,
  copied over the export's runtime binary. It loads and renders.
- Goldens pass.

**Docs:** `Conventions.md` gets a new subsection: "Renderer vocabulary lives in `Renderer/RHI/RHITypes.h`; don't name backend types outside renderer `.cpp`s." `Architecture-LuxEngine.md` §2.3: the RHI types header.

**Exit criteria:**
- `grep -rln "nvrhi" Editor/Source Lux-Runtime Core/Source/Lux/{Scene,Asset,Serialization,Project,Scripting,Audio,Physics,Physics2D,Editor,Core}`
  returns nothing.
- `Image.h`'s spec structs, `RenderGraph.h`, `Shader.h` and `VulkanShaderResource.h` don't name nvrhi.
- The P0 export runs.
- Goldens pass.

**Rollback:** revert. The binary formats are unchanged, so no data migration is involved.

---

### Phase 3 — Real GPU lifetime and frame slots (on NVRHI)

**Goal:** `SubmitResourceFree` actually runs once the GPU finishes the frame, and per-frame slots
are a monotonic frame counter guarded by a GPU wait (D7, D8). This fixes L13 and L14 while NVRHI's
reference counting is still there as a safety net.

> **Already done (2026-10-05, see Appendix C):** the L13 part. `s_ResourceFreeQueue` is now a
> render-thread slot ring advanced by `Renderer::RT_ReleaseRetiredResources()` (called in the
> `BeginFrame` lambda after `m_Window->BeginFrame()`); each slot is closed behind a graphics-queue
> event query and drained when it comes round and the event has signalled.
> `GetRenderResourceReleaseQueue(index)` is gone, replaced by `RT_GetResourceReleaseQueue()`.
> The only remaining `SubmitResourceFree` caller is `~BindlessTextureTable` (the `VulkanShader`,
> `Texture` and `Framebuffer` ones were in dead code deleted in P1); its lambda was audited and is
> safe mid-session. What remains of this phase: the monotonic frame number, L14's
> `RT_GetCurrentFrameIndex()`, and the NRI-ready `GPUDeletionQueue` that will replace the ring.

**Changes**
- `Renderer/RHI/GPUDeletionQueue.{h,cpp}` — **NEW**:
  - `Enqueue(uint64_t retireFrame, std::function<void()>)`, which may use the existing
    `RenderCommandQueue` allocator pattern for small lambdas.
  - `RT_Retire(uint64_t completedFrame)`.
  - `DrainAll()`.
  - Stats: pending count and bytes.
- `Renderer.h/.cpp`:
  - Add `RT_GetFrameNumber()`, `RT_BeginFrame()`, `RT_EndFrame()` and `RT_RetireFrame(uint64_t)`.
  - Rewrite `SubmitResourceFree` (`Renderer.h:157-185`) to enqueue with `RT_GetFrameNumber()`.
    Keep the three-way thread branch.
  - Delete `s_ResourceFreeQueue`, `GetRenderResourceReleaseQueue` and the loop at
    `Renderer.cpp:779-783`. `Shutdown` calls `waitForIdle` and then `DrainAll`.
  - Change `RT_GetCurrentFrameIndex()` (`Renderer.cpp:394-397`) to `RT_GetFrameNumber() % FramesInFlight`.
- `Application.cpp:256-260, 294-298`: call `Renderer::RT_BeginFrame()` inside the `BeginFrame`
  lambda, before `m_Window->BeginFrame()`. Call `Renderer::RT_EndFrame()` after `m_Window->Present()`
  and before `runGarbageCollection()`.
- `VulkanSwapChain.cpp:463-493`: tag each frame's event query with its frame number
  (`std::queue<std::pair<uint64_t, nvrhi::EventQueryHandle>>`). When the wait loop finishes a
  query, call `Renderer::RT_RetireFrame(frame)`.

  `RT_BeginFrame` must also guarantee that frame `N − FramesInFlight` has completed. Implement it
  as "wait the queued event query for that frame if still pending" through a small API on
  `VulkanSwapChain`: `RT_WaitForFrame(uint64_t)`. NVRHI's `maxFramesInFlight = 2` wait in
  `Present` stays as it is.
- Per-frame consumers that assumed "slot = back-buffer index":
  - `ImGuiRenderer::GetOrCreatePipeline` (`ImGuiRenderer.cpp:476-495`) already calls
    `swapchain->GetCurrentBackBufferIndex()`. Leave it.
  - `Framebuffer::GetHandle` for swapchain targets (`Framebuffer.cpp`) uses
    `GetCurrentFramebuffer()`. Leave it.
  - `RenderCommandBuffer` command lists, `UniformBufferSet`/`StorageBufferSet::RT_Get`,
    `DescriptorSetManager` per-frame sets and `BindlessTextureTable` per-frame tables now index
    by slot. Check each for a hard-coded `3`: `nvrhi::static_vector<…, 3>` in
    `RenderCommandBuffer.h:90-125` and `DescriptorSetManager.h:279-281` must be ≥ `FramesInFlight`.
  - `Renderer::Init` (`Renderer.cpp:497-498`) clamps FramesInFlight to the back-buffer count; that
    clamp is no longer needed. Keep it for now and note it.
- **Audit every `SubmitResourceFree` lambda**: the two in `VulkanShader.cpp`, plus those in
  `Texture.cpp`, `Framebuffer.cpp` and `BindlessTextureTable.cpp`. They have never run mid-session
  before. Confirm each captures by value, holds no raw pointer to a main-thread object, and is safe
  to run while other frames record.

**Playbook:** none.

**Thread and lifetime:**
- Retire runs on the render thread. Under SingleThreaded that is the main thread.
- `RT_BeginFrame`'s wait is the existing pacing wait, moved earlier. It is not a new sync point in
  the frame body (`Rendering.md § Performance rules`).

**Verification**
- Debug with validation: resize the viewport continuously for 30 s, toggle SSR, GTAO and Bloom 20×
  each, hot-reload shaders 10×, and switch scenes 10×. Expect zero validation errors.
- `GPUDeletionQueue` pending count stays bounded: it drops to ~0 two frames after the activity
  stops. Log it once per second in Debug.
- GPU memory (`VK_EXT_memory_budget` usage) returns to its pre-activity level within ±5%.
- Goldens pass.
- Present-mode check: run 60 s each in MAILBOX and IMMEDIATE with no stutter regression against P0
  perf JSON (±3%).
- 🧑 Windows MultiThreaded run, same checks.

**Docs**
- `Rendering.md` Invariant 3: rewrite the mechanism (frame-number retire, drained every frame, NRI
  `Destroy*` only there).
- `Threading.md`: the frame loop gets `RT_BeginFrame`/`RT_EndFrame`; `RT_GetCurrentFrameIndex` is
  now the frame slot; `GetCurrentBackBufferIndex` is the swapchain index.

**Exit criteria:** the deletion queue drains during the session, all checks above pass, and the
goldens pass.

**Rollback:** revert. NVRHI still keeps everything alive, so a revert cannot cause use-after-free.

---

### Phase 4 — Lux resource-state tracker, verified against NVRHI

**Goal:** Lux knows, for every GPU access, which state each resource must be in. It drives NVRHI's
barriers with automatic barriers **off** and reproduces today's images with zero validation or
sync hazards. This is the hardest correctness work, done while NVRHI is the oracle.

**Changes**
- `Renderer/RHI/ResourceStateTracker.{h,cpp}` — **NEW** (§2.3). Also `IBarrierEmitter` and
  `NVRHIBarrierEmitter` (**NEW**, `Renderer/RHI/NVRHIBarrierEmitter.cpp`, deleted in P15). The
  NVRHI emitter calls `setTextureState(handle, ToNVRHI(range), ToNVRHI(state))`, `setBufferState`,
  `nvrhi::utils::TextureUavBarrier`/`BufferUavBarrier` and `commitBarriers()`.
- **Resting states.** Store them on the resource:
  - `Image2D::m_RestingState` / `GetRestingState()`, set in `RT_Invalidate` beside today's
    `initialState` (material textures included; there is no Permanent flag — see §2.3)
  - `VertexBuffer`, `IndexBuffer`, `UniformBuffer`, `StorageBuffer`, `MeshSource` meshlet buffers
  - ImGui buffers and textures
  - swapchain images
- `RenderCommandBuffer`:
  - owns `ResourceStateTracker m_Tracker`
  - `RT_Begin` calls `GetActive()->setEnableAutomaticBarriers(!Renderer::ExplicitBarriersEnabled())`
  - `RT_End` calls `m_Tracker.RestoreRestingStates(); m_Tracker.Commit();` before `close()`
  - exposes `RT_GetTracker()`
- **Settings toggle** `Renderer.ExplicitBarriers` (default **off** during the phase, **on** at
  exit). Read it once per frame on the render thread.
- **Requirement sites.** Every one must be converted:
  1. `Renderer::BeginRenderPass` (`Renderer.cpp:1038-1117`):
     - each color attachment → `RenderTarget`
     - depth → `DepthWrite` (or `DepthRead` when the pipeline has `DepthWrite = false`)
     - then the pass's bound sets (see 7)
  2. `Renderer::BeginComputePass` / `DispatchCompute` (`:1176-1239`): pass sets and the material set.
  3. `Renderer::RT_BindMaterialDescriptorSet` (`:1789-1820`): the material's set resources.
  4. Geometry:
     - `RenderQuad` (`:1705`), `RenderGeometry` (`:1754`), `SubmitFullscreenQuad` (`:1881`) and
       `SubmitFullscreenQuadWithOverrides` (`:1935`): VB → `VertexBuffer`, IB → `IndexBuffer`
     - `SceneRenderer::RT_DrawStaticMesh` (`:8635-8754`): VB, IB, and the indirect args buffer →
       `IndirectArgument` when `useIndirect`
     - `RT_DrawStaticMeshMeshlets` (`:7275-7325`): meshlet buffers → `ShaderResource`
     - `Renderer2D` and `DebugRenderer` paths: confirm by reading `Renderer2D.cpp` that they go
       through the functions above; convert any direct site
  5. Copies and clears:
     - `ClearImage` (`:1822`) → `CopyDest` (NVRHI's `clearTextureFloat` is a transfer op)
     - `CopyImage` (`:1838`) → src `CopySource`, dst `CopyDest`
     - `Texture2D::CreateFromSRGB` copy (`Texture.cpp:123-143`)
     - `clearBufferUInt` (`SceneRenderer.cpp:7524-7537`) → `CopyDest`
     - `StorageBuffer::RT_SetData` GPUOnly `writeBuffer` (`StorageBuffer.cpp:62-66`) → `CopyDest`
     - `VertexBuffer::RT_SetData` (`VertexBuffer.cpp:91`)
     - ImGui `UpdateGeometry` writes (`ImGuiRenderer.cpp:530-531`)
     - `Image2D::CopyToHostBuffer` source → `CopySource` (`Image.cpp:451-463`)
  6. Explicit transitions:
     - `PipelineCompute::ImageMemoryBarrier`/`BufferMemoryBarrier` (`PipelineCompute.cpp:181-259`)
       become `tracker.Require(…, MapAccessFlagsToResourceState(to)) + Commit()`, with the 34 call
       sites unchanged
     - `SceneRenderer` `transitionMip` (`:7996-8048`) and `transitionBloomMip` (`:8324-8387`) →
       tracker with per-mip ranges
     - `Texture2D::GenerateMips` (`Texture.cpp:670-731`) → tracker: whole image `UnorderedAccess`,
       then a `RequireUAVBarrier` per mip step (`:719-720` today relies on NVRHI's automatic UAV
       barrier), then restore
     - the environment mip filter (`Renderer.cpp:1497-1501`) → `RequireUAVBarrier(envFiltered)`
  7. `DescriptorSetManager`: during `BakeSet`/`InvalidateAndUpdate`, record per (frame, set) a
     `std::vector<BoundResourceUse>` = {resource, range, `ResourceState`}. The mapping is
     `Texture_SRV` → `ShaderResource`, `Texture_UAV` → `UnorderedAccess`, `ConstantBuffer` →
     `ConstantBuffer`, `RawBuffer_SRV` → `ShaderResource` and `RawBuffer_UAV` → `UnorderedAccess`.
     Expose `GetResourceUses(frame, set)`. `RenderPass::GetBindingSets`, `ComputePass` and
     `Material::GetBindingSet` callers then `Require` them. Bindless tables add no requirements:
     material textures are back in their resting `ShaderResource` state at every command-buffer
     boundary, the same guarantee NVRHI's untracked descriptor tables rely on
     (`ProgrammingGuide.md:120`).
  8. ImGui (`ImGuiRenderer::Render`, `:545-682`):
     - every drawn `ImGuiTextureInfo` → `ShaderResource`; viewport images rest in `RenderTarget`,
       which replaces `beginTrackingTextureState` at `:304`
     - target framebuffer image → `RenderTarget`
  9. Swapchain back buffer: `RenderTarget` for the ImGui/runtime swapchain pass, `Present` at rest.
- **Debug cross-check.** In Debug, with explicit barriers on, after every `Commit` compare the
  tracker's state for each touched resource with NVRHI's `getTextureSubresourceState`/`getBufferState`.
  Log the first mismatch per resource and pass as `LUX_CORE_ERROR_TAG("Renderer", "Tracker/NVRHI state mismatch …")`.
- Renderer Debugger: show `tracker.GetState()` in place of NVRHI states (`SceneRenderer.h:502`
  snapshot) and the barrier count per frame.

**Playbook:** none.

**Thread and lifetime:** the tracker runs on the render thread only. `BoundResourceUse` holds raw
pointers valid for the set's lifetime; the sets are freed through the deletion queue.

**Verification:** run each of these with `Renderer.ExplicitBarriers` on and off.
- Goldens for all scenes and the feature sweep: `--compare nvrhi-baseline p4-explicit` must pass.
- Debug validation and sync validation: error and hazard counts ≤ baseline, and **zero new unique
  messages**.
- Debug cross-check: zero mismatches over the full sweep.
- Environment map creation: open a scene with an HDR environment, and switch the Preetham sky
  parameters.
- Thumbnail generation: open the Content Browser over the sample assets.
- Material Editor preview.
- Viewport resize during play.
- Perf: render-thread CPU time with explicit barriers vs automatic (expect ±5%).
- 🧑 Windows (NVIDIA) with explicit barriers: goldens and validation. NVIDIA and RADV forgive
  different mistakes.

**Docs**
- `Rendering.md`: new section "Resource states", covering resting states, the tracker API, where
  requirements come from, and the rule "never write NVRHI or NRI barriers by hand; ask the tracker".
- The "Validation errors are bugs" bullets about `IndirectArgument`/`GPUOnly` are restated in
  tracker terms.

**Exit criteria:**
- Explicit barriers default **on**.
- All verification passes.
- The toggle still exists, to be removed in P12.

**Rollback:** flip the toggle off, which is instant, or revert.

---

### Phase 5 — Render graph access kinds, buffers and pass-entry batching

**Goal:** the graph declares *how* each pass touches each resource, including SSBOs, so barriers
for a pass are batched before it runs. Debug reports any access a pass makes without declaring it.

**Changes** (`RenderGraph.{h,cpp}`, `SceneRenderer.cpp:3690-4120`)
- `enum class AccessKind : uint8_t`:
  - `SampledRead`
  - `StorageRead`
  - `StorageWrite`
  - `StorageReadWrite`
  - `ColorWrite`
  - `ColorReadWrite` (load + store)
  - `DepthWrite`
  - `DepthReadOnly`
  - `CopySource`
  - `CopyDest`
  - `IndirectArgs`
  - `UniformRead`

  Plus `struct ResourceAccess { ResourceHandle Resource; AccessKind Kind; TextureSubresourceRange Range = AllSubresources; }`.
- `PassDesc` gets `std::vector<ResourceAccess> Accesses;` placed **after `DebugName`**
  (`Rendering.md § Structure`). It is folded into `ComputeStructureHash()`.
- Graph buffers: `ResourceHandle AddExternalBuffer(const BufferDesc&)`, where
  `BufferDesc { std::string Name; Ref<StorageBufferSet> Set; Ref<StorageBuffer> Buffer; }`. Buffer
  handles share the handle space with textures through a type tag. Lifetimes and culling include
  buffers, but buffers never alias.
- `Compile()` derives `Reads`/`Writes` from `Accesses` when `Accesses` is non-empty, so callers
  don't declare twice. Keep the positional self-tests valid.
- `Execute()`, for each surviving pass:
  1. submits one lambda that `Require`s every declared access and calls `Commit()`, with the
     per-pass arrays prebuilt in `CompileResult` so nothing allocates per frame;
  2. runs the pass;
  3. in Debug, submits a lambda that collects requirements made during the pass. The tracker tags
     requirements with the current graph pass through `RenderCommandBuffer::RT_SetCurrentGraphPass`.
     Undeclared ones are reported as `DiagnosticCode::UndeclaredAccess` (Info) in the debug snapshot.
- `SceneRenderer` graph build:
  - Give every `addPass` its `Accesses`. Add an `addPass` overload taking
    `std::initializer_list<ResourceAccess>`.
  - Declare the SSBOs: cluster AABBs and light grids and lists, visible object indexes, indirect
    draw commands, luminance histogram and exposure state.
  - Remove `PassFlags::UntrackedResources` from Cluster Build and Cluster Light Culling
    (`:3876-3880`) and the `SideEffect` workaround on Auto Exposure (`:4046-4051`) where buffer
    declarations now express the dependency. Keep the flag itself for future use.
  - Async compute (`EnableAsyncCompute`, `:3872`) still runs cluster work outside the graph. Leave
    a comment that a cross-queue graph edge is a follow-up.
- `RunValidationSelfTests`: add tests for buffer lifetime and culling, access-derived Reads/Writes,
  and hash sensitivity to `Accesses`.

**Playbook:** "Add a new render pass" (`Architecture § Part 4`, `Rendering.md § Adding a pass`):
- pipelines stay in `Init`
- accurate access declarations
- fold into `ComputeStructureHash()`
- feature-gated passes unchanged

**Thread and lifetime:** graph building and execution happen on the main thread and submit
lambdas, as today. Requirement lambdas run on the render thread.

**Verification**
- `RunValidationSelfTests` passes.
- Zero `UndeclaredAccess` diagnostics in the five scenes with the full feature sweep.
- Barrier count per frame ≤ the P4 count (the Renderer Debugger shows it).
- Goldens pass.
- Graph debug snapshot (Renderer Debugger) shows buffers.

**Docs:** `Rendering.md § The RenderGraph`: access kinds, buffers, `UndeclaredAccess`, and the
updated `addPass` steps.

**Exit criteria:** all of the above.

**Rollback:** revert. P4 correctness doesn't depend on P5.

---

### Phase 6 — Vendor NRI and wrap the device

**Goal:** NRI builds in-tree from `starbounded-dev/NRI` (branch `lux` = hazel + upstream main). An NRI
device wraps Lux's `VkDevice`, and a self-test proves NRI and NVRHI can share it. Rendering is
unchanged.

**Steps**
1. 🧑 **User:** fork `StudioCherno/NRI` to `starbounded-dev/NRI` on GitHub; the fork includes the `hazel`
   branch. The agent must not create forks or push.
2. **Agent, local:** clone the fork into `Core/vendor/NRI`.
   ```
   git checkout -b lux origin/hazel
   git remote add upstream https://github.com/NVIDIA-RTX/NRI
   git fetch upstream --tags
   git merge upstream/main
   ```
   Target upstream `main` (v181 or newer). Resolve conflicts in hazel's 16 changed files (L27). For
   each hazel patch, record a verdict in Appendix C with evidence:

   | Patch | Verdict rule |
   |---|---|
   | `premake5.lua` | keep; update file lists to upstream (new `Source/VK/*`: DescriptorHeap, Micromap, Video*, TransferContext …; new `Source/Shared/*`). Add `configurations:Debug-AS` to the Debug filter. Add `NRI_ENABLE_WAYLAND_SUPPORT=1` on Linux. Keep `NRI_ENABLE_IMGUI_EXTENSION` undefined. Keep NGX gated on the SDK being present |
   | `External/VMA/vk_mem_alloc.h` | keep; bump to the commit upstream CMake pins (`3aa921224c154a0d2c43912bc88e1c42ce1f7607`) |
   | X11 `Window` clash | keep if upstream still declares `NriStruct(Window)` and includes X11 headers in a way that collides (it declares `Window` at `NRISwapChain.h`) |
   | Vulkan 1.3 push-descriptor crash | keep if upstream `DeviceVK` still assumes push descriptors are core |
   | Swapchain extent clamp | keep (upstream still errors) |
   | `AcquireNextTexture` failure handling | keep unless upstream now handles the same failure |
   | D32S8 readback stride | keep unless upstream fixed it |
   | NGX / DLSS-RR changes | keep only if the merge is clean (inert without the SDK; upscalers are out of scope) |

   **Fallback,** if conflicts can't be resolved in reasonable time or the merged tree fails to
   build: branch `lux` from upstream `v180`/`main` and cherry-pick the premake, VMA, X11, clamp,
   acquire and readback patches. Log the decision.
3. 🧑 **User:** push branch `lux` to `starbounded-dev/NRI`.
4. **Superproject:**
   - `.gitmodules` gets `[submodule "Core/vendor/NRI"] path = Core/vendor/NRI`,
     `url = https://github.com/starbounded-dev/NRI`, `branch = lux`. Commit the submodule pointer
     (pointing at the pushed commit).
   - `premake5.lua`: under `group "Dependencies/Renderer"`, add `include "Core/vendor/NRI"` beside
     the NVRHI include (`:186`). `outputdir` and `VULKAN_SDK` are already workspace globals.
   - `Dependencies.lua`: `NRI = { LibName = { "NRI", "NRI-VK", "NRI-Validation", "NRI-NONE", "NRI-Shared" }, IncludeDir = "%{wks.location}/Core/vendor/NRI/Include" }`.
     The order matters for GNU ld static archives: dependents before dependencies. Check that
     `ProcessDependencies()` preserves it.
   - `Core/premake5.lua`: `defines { "NRI_STATIC_LIBRARY=1" }`. Without it `NRI_API` becomes
     dllimport on Windows.

**Code changes**
- `Renderer/RHI/RHIDevice.{h,cpp}` — **NEW**:
  - `RHIDevice::Init(const RHIDeviceCreateInfo&)` calls `nriCreateDeviceFromVKDevice`.
    `DeviceCreationVKDesc` fields:
    - `vkInstance`, `vkPhysicalDevice`, `vkDevice`
    - `queueFamilies` = graphics (1), compute (1 if `enableComputeQueue`), copy (1 if a dedicated
      transfer family)
    - `minorVersion` = the device API minor version
    - `vkExtensions` = the enabled instance and device lists (`VulkanDeviceManager::GetEnabledVulkan*Extensions`)
    - `vkBindingOffsets` = {0,0,0,0}
    - `enableNRIValidation` = Debug
    - `callbackInterface.MessageCallback` → `LUX_CORE_{INFO,WARN,ERROR}_TAG("Renderer", "[NRI] {}", …)`
  - Fetch `CoreInterface`, `HelperInterface`, `WrapperVKInterface`, `SwapChainInterface` and
    `MeshShaderInterface` (when `features.meshShader`) through `nriGetInterface`. Then `GetQueue`
    for each type.
  - `Shutdown()` calls `nriDestroyDevice`.
  - Accessors: `Get()`, `API()`, `GetQueue(GPUQueue)`, `GetDesc()`.
- `VulkanDeviceManager.h/.cpp`: add accessors for `m_VulkanInstance`, `m_VulkanPhysicalDevice`,
  `m_VulkanDevice`, `m_QueueFamilyIndices`, the API version, and whether the transfer queue exists.
- `Window.cpp`: call `RHIDevice::Init` right after `m_DeviceManager->CreateDevice` succeeds
  (`:285-293`). Call `RHIDevice::Shutdown()` in `Window::Shutdown` after the swapchain is destroyed
  and before `m_DeviceManager->Shutdown()` (`:499-519`).
- **Feature parity for the wrapped device.**
  - Read NRI `Source/VK/DeviceVK.hpp`'s create-from-VK path and `FillDesc` to list the features
    and extensions NRI *assumes* are enabled.
  - Compare with `VulkanDeviceManager::createDevice` (`VulkanDeviceManager.cpp:410-607`).
  - Enable what is missing and supported. Likely candidates: `VK_KHR_push_descriptor` (hazel fix),
    `extendedDynamicState`, `maintenance5`/`maintenance6`, `shaderDrawParameters`, `hostQueryReset`,
    `samplerFilterMinmax`, and the descriptor-indexing `…UpdateAfterBind`/`UpdateUnusedWhilePending`
    bits.
  - Log NRI's `DeviceDesc` at startup: tiers (`bindless`, `resourceBinding`, `shadingRate`) and
    features (`meshShader`, `enhancedBarriers`, `timestamp`, `pipelineStatistics`,
    `viewportOriginBottomLeft`, `rootConstantsOffset`).
- Debug self-test `RHIDevice::RunSelfTest()` (**NEW**, called once after `Renderer::Init` in Debug):
  1. Create a 64 KiB `DEVICE` buffer, a 4×4 RGBA8 texture, and an SRV view.
  2. Allocate an NRI command allocator and buffer on the graphics queue.
  3. Record `CmdBarrier` (texture `UNDEFINED`→`SHADER_RESOURCE`) and `CmdZeroBuffer`.
  4. `QueueSubmit` with a timeline fence under `LockQueue`, then `Wait`.
  5. Destroy everything.
  6. Second test: wrap the next NVRHI frame's command buffer (I3), record one `CmdZeroBuffer`
     inside an NRI segment (I4), and expect a clean frame.

  Log `[RHI] NRI self-test passed`.

**Playbook:** "Add a new dependency" (`Dependencies.lua` entry, regenerate) and "Add a build
toggle". No toggle is needed: NRI is mandatory.

**Thread and lifetime:**
- The NRI device lives from `Window::Init` to `Window::Shutdown`. That is before
  `Renderer::Init`/after `Renderer::Shutdown`.
- The self-test runs on the main thread during init, while the render thread is idle (as Tracy's
  init does today, `VulkanDeviceManager.cpp:770-772`).

**Verification**
- Build all configs.
- `nm` shows VMA symbols once.
- Linux link succeeds without extra system libraries. If X11/Wayland link errors appear, add the
  libraries in `Dependencies.lua` Linux.
- Debug log shows the `DeviceDesc` and the passing self-test, with zero NRI-validation and VK
  validation errors.
- Goldens pass.
- 🧑 Windows build and run: NVIDIA `DeviceDesc` logged, self-test passes.

**Docs**
- `Building.md`: the projects table (Dependencies/Renderer adds `NRI`, `NRI-VK`, `NRI-Validation`,
  `NRI-NONE`, `NRI-Shared`), the vendored-submodule note (fork `starbounded-dev/NRI` branch `lux` with
  its own `premake5.lua`), and Wayland/X11 defines.
- `Architecture-LuxEngine.md` §2.3 and the directory map.
- `.gitmodules` is documented in Building.

**Exit criteria:** as verified, and the hazel-patch verdict table is filled in.

**Rollback:** remove the `RHIDevice` calls. The submodule can stay.

---

### Phase 7 — Aftermath re-wired on the Lux-created device (D3)

**Goal:** on NVIDIA, a device loss in Debug and Release produces an Aftermath GPU crash dump with
checkpoint markers and shader mapping. On other vendors Aftermath is silently skipped.

**Changes**
- `VulkanDeviceManager::createDevice`, guarded by `#if LUX_HAS_AFTERMATH`
  (`(!LUX_DIST && !LUX_DISABLE_AFTERMATH)`, as in the old `VulkanDevice.cpp:11`). Rebuild the
  sequence from git history (Phase 1 note):
  1. `GpuCrashTracker::Initialize()` (`Debug/NsightAftermathGpuCrashTracker.{h,cpp}`) before
     `vkCreateDevice`.
  2. When the physical device supports both `VK_NV_device_diagnostic_checkpoints` and
     `VK_NV_device_diagnostics_config`, add them to the enabled device extensions **and** chain
     `VkDeviceDiagnosticsConfigCreateInfoNV` (shader debug info, resource tracking, automatic
     checkpoints) into `deviceDesc.pNext`. Use the existing `deviceCreateInfoCallback` hook
     (`VulkanDeviceManager.cpp:580-581`) or prepend directly.
  3. The extensions flow into NVRHI's and NRI's enabled lists automatically.
- `Platform/Vulkan/Debug/AftermathCheckpoints.{h,cpp}` — **NEW**: `SetCheckpoint(VkCommandBuffer, std::string_view)`
  with stable ring storage (the old `VulkanDiagnostics.cpp:14-38` pattern). It is a no-op unless
  the extension is enabled. It gets `vkCmdSetCheckpointNV` once through `vkGetDeviceProcAddr`.
- `RenderCommandBuffer::RT_BeginMarker`: call `SetCheckpoint` on the native command buffer
  (NVRHI `getNativeObject` now; NRI `GetCommandBufferNativeObject` after P13).
- Shader database: when a shader's SPIR-V is loaded or compiled (`VulkanShader` load path), register
  the binaries with the Aftermath shader database (`Debug/NsightAftermathShaderDatabase.{h,cpp}`;
  use its existing add/lookup API). Prefer the debug-info binary when
  `CompileOrGetVulkanBinaries` produced one.
- Device-lost path: where submit or present returns `VK_ERROR_DEVICE_LOST`, call
  `GpuCrashTracker::WaitForDump()` (or the existing equivalent), log the dump path, then fail with
  `LUX_CORE_FATAL`. NVRHI reports it through its error callback (`DefaultMessageCallback::message`);
  in the NRI era it comes from `Result::DEVICE_LOST`, plus `nriReportDeviceLostInfo` (P13).
- `--no-aftermath` and Dist keep removing `Debug/**.cpp` (`Core/premake5.lua`). Every reference in
  shared code stays behind `LUX_HAS_AFTERMATH`.

**Playbook:** none. The build toggle already exists.

**Thread and lifetime:** initialised on the main thread before the device. Checkpoints are written
on the render thread. The dump wait runs on whichever thread observed the loss.

**Verification**
- Linux AMD: the log says "Aftermath unavailable (extensions not supported)", and goldens pass.
- `--no-aftermath` build compiles.
- 🧑 Windows NVIDIA:
  - The log shows Aftermath initialised.
  - Optional forced-hang test: a Debug-only console variable dispatches an infinite-loop compute
    shader. Gate it so it can never ship. The test must produce a `.nv-gpudmp` with Lux marker
    names.

**Docs:** `shader-debug` skill §E.5 rewritten ("Aftermath is active on NVIDIA in Debug and
Release"). `Rendering.md` Aftermath bullet.

**Exit criteria:** as verified.

**Rollback:** remove the guarded block. No other phase depends on it.

---

### Phase 8 — Textures, buffers, views and memory on NRI (NVRHI sees wrappers)

**Goal:** NRI allocates and owns every GPU texture and buffer. NVRHI keeps rendering through
non-owning wrappers (I6). CPU-mapped writes use NRI (I7). Swapchain images get NRI wrappers.
Output is unchanged.

**Changes**
- `Renderer/RHI/RHIFormats.{h,cpp}` — **NEW**: `ToNRIFormat(ImageFormat)` (table in §2.10),
  `ToNRITextureType(TextureDimension)`, `ToNRIUsage(ImageUsage, bool uav, bool attachment)`, and
  buffer usage helpers.
- `Image2D::RT_Invalidate` (`Image.cpp:136-292`):
  - Build an `nri::TextureDesc` from the spec:
    - `type`: 2D for 2D, 2DArray and Cube, with `layerNum = 6 × layers` for cubes; 3D for 3D
    - `usage` = `SHADER_RESOURCE` always (NVRHI's default), plus `COLOR_ATTACHMENT`/`DEPTH_STENCIL_ATTACHMENT`
      for attachments, plus `SHADER_RESOURCE_STORAGE` when today's `isUAV` logic says so
      (`:216-239`)
    - `mipNum`, `sampleNum`
    - `sharingMode = CONCURRENT` (NRI's lazy default)
  - `CreateCommittedTexture(device, DEVICE, 0, desc, &tex)`. Keep the `LUX_CORE_VERIFY` message for
    failure.
  - Wrap it for NVRHI:
    `device->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(VkImage(NRI.GetTextureNativeObject(*tex))), sameNvrhiDescAsToday)`.
  - Store `m_Info.RHITexture` (`nri::Texture*`).
  - On replacement or `Release`, `SubmitResourceFree` captures the old `nri::Texture*`, the view
    cache and the NVRHI wrapper by value and destroys them on the render thread.
  - Check whether NRI's VK image usage includes the TRANSFER_SRC/DST that NVRHI's clears, copies
    and `writeTexture` need (`TextureVK` create code). If not, add `COPY` usage through the fork, or
    request it the NRI way. Validation will say.
- `Image2D::GetView(const TextureViewKey&)` (**NEW**) and the cache (§2.5). Unused until P9, but
  exercised in Debug by `RHIDevice::RunSelfTest` against a real `Image2D`.
- Buffers:
  - `VertexBuffer` (both constructors), `IndexBuffer`, `UniformBuffer`, `StorageBuffer`
    (`Invalidate`/`Resize`, `StorageBuffer.cpp:25-33`), and `Mesh.cpp:328-345` meshlet buffers use
    `CreateCommittedBuffer`:
    - location: `DEVICE` for GPU-only, `HOST_UPLOAD` for `CpuAccessMode::Write`
    - `usage` from today's flags (vertex, index, constant, `SHADER_RESOURCE`/`_STORAGE` for raw
      views, `ARGUMENT` for `DrawIndirect`)
    - `byteAddress = true` where raw views were allowed
  - Wrap them with `createHandleForNativeBuffer(nvrhi::ObjectTypes::VK_Buffer, …, sameNvrhiDesc)`.
  - Mapped writes go through NRI (I7):
    - `UniformBuffer::RT_SetData` (`UniformBuffer.cpp:50-59`)
    - `StorageBuffer::RT_SetData` non-GPUOnly branch (`:68-71`)
    - `VertexBuffer::SetData` (`:47-65`)
- `VulkanSwapChain::Create` (`:222-243`): for each swapchain `VkImage`, also
  `WrapperVK.CreateTextureVK(TextureVKDesc{ vkImage, vkFormat, VK_IMAGE_TYPE_2D, usage, w, h, 1, 1, 1, 1 })`
  plus a `COLOR_ATTACHMENT` view. Destroy them in `Destroy()` before `destroySwapchainKHR`; the GPU
  is idle there.
- `Renderer::GetGPUMemoryStats`: use `NRI Helper.QueryVideoMemoryInfo(device, MemoryLocation::DEVICE, info)`
  → `Used = usageSize`, `TotalAvailable = budgetSize`. Delete the raw Vulkan budget helpers moved
  there in P1.
- `Image2D` live-image registry (`Image.cpp:18-19`): key it by `nri::Texture*`.

**Playbook:** none.

**Thread and lifetime:** creation on any thread (§2.8); destruction only through the deletion
queue (render thread). NVRHI wrappers are released in the same lambda before the NRI destroy.

**Verification**
- Goldens: all scenes and the sweep.
- Texture streaming: open FMODDemo cold with the asset cache deleted.
- Hot-reload a texture asset.
- Resize, MSAA framebuffers if any, cube maps (environment), 3D textures (grep
  `TextureDimension::Texture3D` users), readback (thumbnails).
- GPU memory panel numbers are plausible and stable.
- Validation and NRI validation clean.
- 🧑 Windows.

**Docs:** `Rendering.md`: resources are NRI-owned; the I6/I7 interop note (temporary).

**Exit criteria:** as verified, and no `device->createTexture`/`createBuffer` calls remain
(`grep -rn "createTexture\|createBuffer(" Core/Source` → only `createHandleForNative*`).

**Rollback:** revert. Phase 7 and earlier are unaffected.

---

### Phase 9 — Descriptor sets, pipeline layouts and compute on NRI

**Goal:**
- Every shader has an NRI pipeline layout.
- `DescriptorSetManager`, `Material`, `BindlessTextureTable` and the meshlet sets produce NRI
  descriptor sets alongside the NVRHI binding sets.
- **Compute** dispatches are recorded with NRI inside NVRHI command buffers (I3/I4), behind
  `Renderer.NRICompute`.

**Changes**
- **Combined samplers (L10).**
  - Remove the unused `EdgeDetection.glsl` load (`Renderer.cpp:557`), after grepping that no code
    calls `Get("EdgeDetection")`. Convert its two declarations to `texture2D` plus the existing
    `r_*` samplers, or delete the file if unused.
  - Convert `SSR.glsl:31` (HBAO branch) the same way.
  - Delete `ImGui.glsl` if unused (ImGui uses `ImGui.hlsl`).
  - In `VulkanShaderCompiler::Reflect`, a non-empty `resources.sampled_images` logs
    `LUX_CORE_ERROR_TAG("Renderer", "Combined image samplers are not supported (NRI): {} in {}", …)`.
  - Clear `Editor/Resources/Cache/Shader/` after the shader edits (`Rendering.md`).
- **Samplers:** `Sampler` and the per-image sampler (`Image.cpp:253-265`) also create NRI
  `CreateSampler` descriptors. Map filters (linear/nearest, as today's `minFilter = magFilter = mipFilter = !IsIntegerBased`),
  address modes, `mipBias`, and anisotropy if set. Do the same for the default samplers in
  `Renderer.cpp:2201-2235`.
- **Buffer views:** CONSTANT_BUFFER for `UniformBuffer`; BYTE_ADDRESS / STORAGE_BYTE_ADDRESS for
  `StorageBuffer`/meshlet buffers. Cache them on the buffer and free them with it.
- `VulkanShader::RT_CreatePipelineLayout()` (**NEW**, called at the end of `CreateDescriptors`)
  builds the §2.5 layout and stores `nri::PipelineLayout*` plus the `setNumber → setIndex` table.
  On reload the old one is freed through the deletion queue.
- `DescriptorSetManager` (`DescriptorSetManager.{h,cpp}`):
  - Add the NRI pool, per-frame `nri::DescriptorSet*` arrays, `RT_GetNRISets(frame)`, and the
    write path:
    - `Bake`/`BakeSet` allocate and write.
    - `InvalidateAndUpdate` rewrites the changed ranges for the current slot only (D8 makes this
      safe).
    - `OnShaderReloaded` rebuilds.
  - Descriptors per input:
    - `UniformBuffer`/`Set` → CBV
    - `StorageBuffer`/`Set` → SRV or UAV byte-address view, matching the reflected read-only flag
    - `Texture2D`/`TextureCube`/`Image2D`/`ImageView` → `Image2D::GetView`, with SRV or UAV from
      `input.IsWriteable` and the dimension from the reflected type (cube for `ImageSampler3D`,
      as `:583` asserts today)
    - `Sampler` → the sampler descriptor
  - Keep NVRHI binding sets unchanged in parallel.
- `Material::GetBindingSet` gets an NRI sibling. `IsDescriptorSetCompatible` (`Material.cpp:15-44, 360-369`)
  compares Lux reflection, not NVRHI layout descs, so it works for both.
- `BindlessTextureTable`: NRI sets per §2.5 / D10; `RT_WriteTable` writes NRI and NVRHI both; the
  capacity comes from `DeviceDesc`.
- `MeshSource::RT_GetOrCreateMeshletBindingSet` (`Mesh.cpp:548-590`) gets an NRI sibling set
  (per-mesh pool, freed with the mesh).
- **Compute on NRI:**
  - `PipelineCompute::RT_CreatePipeline` also creates the NRI compute pipeline:
    `ShaderDesc{ COMPUTE_SHADER, spirv.data(), spirv.size()*4, "main" }` from
    `VulkanShader::m_ShaderData`; the layout is the shader's.
  - `Renderer::BeginComputePass`/`DispatchCompute`, when `Renderer.NRICompute` is on:
    1. tracker requirements and `Commit` (through NVRHI, I5)
    2. `cmd->RT_BeginNRISegment()`
    3. `CmdSetPipelineLayout(COMPUTE)`, `CmdSetPipeline`
    4. `CmdSetDescriptorSet` for pass sets, the material set and bindless set 4
    5. `CmdSetRootConstants`, `CmdDispatch`
    6. `RT_EndNRISegment()`
  - The per-mip loops (bloom, pre-convolution, mip gen, env filter) need nothing special: each
    dispatch is its own segment.
- `RenderCommandBuffer`: the NRI wrapper lifecycle (I3) and `RT_BeginNRISegment`/`RT_EndNRISegment`
  (I4) with their Debug assertions.

**Playbook:** none. Shader edits follow `Rendering.md § Shaders` (clear the cache).

**Thread and lifetime:**
- Layouts and pools are created on the render thread (`RT_*`) or at init. Pools are freed through
  the deletion queue.
- Descriptor writes for in-use sets happen only on the render thread.

**Verification**
- With `Renderer.NRICompute` on: goldens for all scenes and the sweep, covering every compute pass:
  HZB, mesh culling, cluster build/cull (sync **and** async compute), GTAO + denoise, pre-integration,
  pre-convolution, SSR, bloom, luminance histogram/average, SMAA, environment maps, Preetham sky,
  mip generation, irradiance.
- Validation and NRI validation: zero descriptor-type or layout-compatibility errors.
- Hot-reload a compute shader.
- Toggle on vs off golden equality.
- Perf: render-thread time ±5%.
- 🧑 Windows.

**Docs:** `Rendering.md` Invariant 2 (descriptor sets: NRI pools per manager); the bindless section
(NRI sets; D10).

**Exit criteria:** `Renderer.NRICompute` defaults **on**; everything above passes.

**Rollback:** toggle off.

---

### Phase 10 — Graphics pipelines and draws on NRI

**Goal:** every render pass, draw, mesh-shader draw and VRS call is recorded with NRI, behind
`Renderer.NRIGraphics`. NVRHI only opens, closes and submits command lists, emits barriers, and
handles markers, queries, uploads and ImGui.

**Changes**
- **Fork patch LUX-1 (D15)** in `Core/vendor/NRI` on branch `lux`, committed in the submodule:
  - `PipelineVK`: add `VK_DYNAMIC_STATE_LINE_WIDTH` to graphics pipelines when the device enabled
    `wideLines`.
  - `CommandBufferVK::SetPipeline`: `vkCmdSetLineWidth(1.0f)` for graphics pipelines when that
    dynamic state exists.
  - 🧑 The user pushes the submodule commit.

  Lux side: after `CmdSetPipeline` for a pipeline with `IsDynamicLineWidth()`, call
  `vkCmdSetLineWidth(VkCommandBuffer(NRI.GetCommandBufferNativeObject(cmd)), spec.LineWidth)`. This
  is the one sanctioned raw call in `Renderer/`, documented in `Rendering.md`.
- `Pipeline::RT_Invalidate` (`Pipeline.cpp:90-272`) also builds an NRI `GraphicsPipelineDesc`:
  - Layout: the shader's.
  - `vertexInput`: attributes in the same order as today's NVRHI array, with
    `vk.location = running index`, the format from §2.10, `offset` and `streamIndex = bufferIndex`.
    Streams: `{ bindingSlot = bufferIndex, PER_INSTANCE if layout.IsInstanced(), stride = layout.GetStride() }`.
    Mesh pipelines have no vertex input.
  - `inputAssembly.topology` from §2.10.
  - `rasterization`: `fillMode` (`Wireframe` → `WIREFRAME`), `cullMode` (`BACK`/`NONE`),
    `frontCounterClockwise = true`, `shadingRate = SupportsVariableRateShading()`.
  - `multisample` when the framebuffer has samples > 1.
  - `outputMerger`:
    - One `ColorAttachmentDesc` per color attachment, or 1 for swapchain targets: format from the
      framebuffer images, `blendEnabled`, color/alpha blend descs with factors and ops from
      today's switch (`:183-247`), and `colorWriteMask = RGBA`.
    - `depth { compareOp, write }` from the spec.
    - `depthStencilFormat`.
  - `shaders`: VS+PS, or TASK?+MESH+PS.

  The NVRHI pipeline is still created during this phase.
- `Framebuffer::RT_Invalidate` (`Framebuffer.cpp:234-…`): build `m_ColorViews[i]` and `m_DepthView`
  through `Image2D::GetView` (COLOR_ATTACHMENT / DEPTH_STENCIL_ATTACHMENT, honouring
  `ExistingImageLayer`). `HasStaleAttachments` compares `nri::Texture*` as well. Swapchain targets
  use the P8 swapchain views for the current back buffer.
- `Renderer::BeginRenderPass` (NRI path):
  1. Tracker attachments and bindings, `Commit`.
  2. `RT_BeginNRISegment`.
  3. `CmdBeginRendering`. Color `loadOp = CLEAR` when today's logic clears (`:1053-1063`: per-attachment
     `AttachmentLoadOp`, `explicitClear`, `ClearColorOnLoad`), otherwise `LOAD`; `clearValue` from
     `GetClearValues()`; `storeOp = STORE`. Depth uses `ClearDepthOnLoad`/`explicitClear` and
     `DepthClearValue`.
  4. Viewport `{0, 0, w, h, 0, 1, originBottomLeft=false}`: the same flip as NVRHI (L26), so the
     Y convention is unchanged. Full scissor.
  5. `CmdSetPipelineLayout(GRAPHICS)`, `CmdSetPipeline`, descriptor sets (pass sets plus bindless
     4), `CmdSetShadingRate(1×1, OVERRIDE, OVERRIDE)` when VRS is supported, and line width.
- `Renderer::EndRenderPass` → `CmdEndRendering` + `RT_EndNRISegment`. `SetViewport`/`SetScissor`/`SetFragmentShadingRate`
  issue NRI commands directly.
- Draw sites through a **redundant-state cache**. New `RenderCommandBuffer::RT_SetVertexBuffers`,
  `RT_SetIndexBuffer`, `RT_SetDescriptorSet(set, nri::DescriptorSet*)`, `RT_SetRootConstants` and
  `RT_SetPipeline` skip calls whose arguments match the last bound ones. The cache resets at
  segment begin. The sites:
  - `RenderQuad`, `RenderGeometry`, `SubmitFullscreenQuad`, `SubmitFullscreenQuadWithOverrides`
  - `RT_DrawStaticMesh` (indexed and indirect)
  - `RT_DrawStaticMeshMeshlets` (NRI `MeshShader.CmdDrawMeshTasks`, set 0 = the mesh's NRI meshlet set)
  - `SceneRenderer.cpp:7247` depth clear → `CmdClearAttachments` inside rendering, or a `loadOp`
    on that pass
  - `Renderer2D` and `RuntimeLayer` swapchain pass (through the same functions)
- **Inside-rendering audit:**
  - Debug `m_InsideRendering` flag on `RenderCommandBuffer`.
  - `LUX_CORE_ASSERT` with the pass name if any of these happen between `CmdBeginRendering` and
    `CmdEndRendering`: tracker `Commit`, copy, clear-storage, dispatch, timer-query begin/end
    (NVRHI), or upload.
  - Run every scene and the sweep. Fix each hit by ending and restarting the pass, or by moving the
    command before the pass. NVRHI used to split passes implicitly, which is why such code exists.

**Playbook:** none. Pipelines are still created once in `Init`.

**Thread and lifetime:** render thread for recording. Pipelines are created on the render thread
(`Pipeline::Invalidate` submits `RT_Invalidate`). Old pipelines are freed through the deletion
queue on reload.

**Verification**
- With `Renderer.NRIGraphics` on, goldens for all scenes and the sweep: shadows (directional array
  layers, spot), pre-depth (cutout), G-buffer + emission additive, deferred lighting, skybox,
  transparent forward, wireframe + colliders + debug categories (2 px and 4 px lines visibly
  wide), grid, selection + jump flood, composite, DOF, SMAA, 2D overlay text (MSDF), and the
  runtime player.
- Material preview and thumbnails.
- VRS on/off on the RTX (🧑).
- Mesh-shader pre-depth on the RTX (🧑). Renoir has no mesh shaders, so the path must be cleanly
  skipped.
- Zero inside-rendering asserts.
- Validation and NRI validation clean.
- Perf: render-thread time ≤ P0 + 5%. The redundant-state cache should make it better.

**Docs:** `Rendering.md`: render passes are explicit (`BeginRenderPass` begins rendering; nothing
non-rendering may happen inside); the line-width exception; the Y convention unchanged (now via
NRI's default viewport).

**Exit criteria:** the toggle defaults **on**; everything above passes.

**Rollback:** toggle off.

---

### Phase 11 — ImGui renderer on NRI

**Goal:** the editor UI, including multi-viewport windows, is drawn with NRI, behind
`Renderer.NRIImGui`.

**Changes** (`ImGui/ImGuiRenderer.{h,cpp}`)
- Pipeline:
  - `ImGui.hlsl` SPIR-V (`Renderer.cpp:519`).
  - Layout: set 0 = { binding 0 `TEXTURE`, binding 1 `SAMPLER` } (`ImGui.hlsl:53-54`), plus root
    constants (`ImGui.hlsl:9,44`).
  - Vertex input: pos `RG32_SFLOAT`, uv `RG32_SFLOAT`, col `RGBA8_UNORM`, as in `ImDrawVert`.
  - Blend: standard ImGui, as in `m_BasePSODesc`.
  - One pipeline per target format. That replaces the per-back-buffer cache at `:476-495`.
- Descriptor sets:
  - Persistent textures: a persistent pool, cached by `ImGuiTextureInfo`.
  - Frame textures: a per-frame-slot pool reset with `ResetDescriptorPool` when the slot starts. D8
    makes that safe.
- Geometry: per-slot `HOST_UPLOAD` VB/IB, grown with 5000-element headroom as today and freed
  through the deletion queue. Map and memcpy, replacing `writeBuffer` (`:530-531`).
- `ImGuiTextureInfo`: drop the raw `nvrhi::ITexture*`. Use `Ref<Image2D>` plus range (introduced
  in P2). `ImGuiTextureRegistry::ImGuiOwnedTextures` (`.h:138`) becomes `Ref<Image2D>`, and the
  font atlas and ImGui-managed textures are created as `Image2D` (`ImageUsage::Texture`).
  `ImGuiDrawDataSnapshot::m_TextureKeepAlives` (`.h:180`) becomes `std::vector<Ref<Image2D>>`.
- `Render()`:
  1. The tracker requires `ShaderResource` for each drawn texture and `RenderTarget` for the target.
  2. NRI rendering into the target view, with `loadOp = CLEAR` (magenta) when `clearTarget`.
  3. Per command: scissor, root constants (`Flags`) and descriptor set, then `CmdDrawIndexed`.
  4. Wait on the acquire semaphore as today (`RT_Wait`), until P14 changes it.

**Playbook:** none. This is not a panel change.

**Thread and lifetime:** UI is built on the main thread and the snapshot is rendered on the render
thread, as today. Textures are kept alive by `Ref` in the snapshot.

**Verification**
- Editor UI visually identical: 🧑 Windows; on Linux X11, the user looks at it. Wayland has no
  screenshots, so on Wayland it is a smoke test plus validation.
- Viewport image, icons, thumbnails, grayscale/opaque flags (asset browser icons).
- Drag a panel out to create a platform window. Close it. Resize it.
- Golden scene images unchanged, since they don't include UI.
- Validation and NRI validation clean.

**Docs:** `Architecture-LuxEngine.md` §2.9 ImGui rendering note.

**Exit criteria:** the toggle defaults **on**.

**Rollback:** toggle off.

---

### Phase 12 — Collapse the toggles; delete NVRHI binding, pipeline and wrapper code

**Goal:** one recording path: NRI for draws, dispatches, ImGui and descriptors; NVRHI only for
command lists, submission, barrier emission, queries, markers, uploads and swapchain sync. This
phase shrinks the surface before the submission switch.

**Changes**
- Remove the `Renderer.ExplicitBarriers`, `Renderer.NRICompute`, `Renderer.NRIGraphics` and
  `Renderer.NRIImGui` settings, and their NVRHI branches.
- Delete NVRHI pipelines (`Pipeline::m_Handle`/`m_MeshletHandle`, `PipelineCompute::m_Handle`),
  binding layouts (`VulkanShader::m_DescriptorSetLayouts`), shader handles
  (`VulkanShader::m_ShaderHandles`, `LoadAndCreateShaders`), NVRHI binding sets in
  `DescriptorSetManager`/`Material`/`BindlessTextureTable`/`MeshSource`, NVRHI framebuffers
  (`Framebuffer::m_Handle`/`m_FramebufferDesc`; `GetHandle`), NVRHI samplers, the ImGui NVRHI path,
  and `nvrhi::GraphicsState`/`ComputeState` in `RenderCommandBuffer`.
- NVRHI resource wrappers (`createHandleForNativeTexture/Buffer`) are still needed: NVRHI's barrier
  emitter, `writeBuffer`/`writeTexture` and `copyTexture` take NVRHI handles. They are removed in
  P13.
- The Debug tracker/NVRHI cross-check stays until P13.

**Verification:** build all configs; goldens all scenes; validation; perf (should improve: no
duplicate pipeline creation, so startup time also drops; record it).

**Docs:** remove the toggle mentions.

**Exit criteria:** `grep -rn "GraphicsState\|ComputeState\|BindingSet\|BindingLayout\|createGraphicsPipeline\|createComputePipeline\|createFramebuffer" Core/Source`
returns only NVRHI-free comments.

**Rollback:** revert (P11 is the last toggle-capable state).

---

### Phase 13 — Command buffers, submission, barriers, queries, uploads and readback on NRI

**Goal:** NRI owns every command buffer and queue submission. Barriers come from the tracker
through NRI `CmdBarrier`, with uploads, queries, markers and Tracy in NRI terms. NVRHI is left only
in the swapchain's semaphore/event helpers, which P14 removes.

**Changes**
- `RenderCommandBuffer` (`RenderCommandBuffer.{h,cpp}`):
  - Per frame slot, per queue: an `nri::CommandAllocator` + `nri::CommandBuffer`.
  - `RT_Begin`: `ResetCommandAllocator`, `BeginCommandBuffer`, `CmdResetQueries` for this slot's
    pools, the frame timestamp, and the pipeline-stats query begin.
  - `RT_End`: close open Tracy zones (today's rule, `:244-258`), `TracyVkCollect` on the native
    command buffer, `CmdCopyQueries` to readback buffers, `RestoreRestingStates` + `Commit`,
    `EndCommandBuffer`.
  - `RT_Submit(waitSemaphore)` → `QueueSubmit` under `LockQueue`:
    - waits: an optional wrapped acquire semaphore (`CreateFenceVK`, value 0), plus pending
      cross-queue timeline waits
    - signals: the queue timeline fence `++value`
    - `m_LastExecutionInstance = value`
  - Remove `RT_Wait`'s NVRHI semaphore path; it becomes a pending wait.
  - Delete the NVRHI members and `GetActive()`. Callers use the typed `RT_*` methods added in P10
    plus `RT_GetNRI()` for the few remaining raw uses.
- `Renderer/RHI/RHIQueueTimeline.{h,cpp}` — **NEW**: one timeline `nri::Fence` per `GPUQueue`,
  `Signal`/`PendingWait`/`Wait(value)`/`GetCompletedValue`.
  - `Renderer::QueueWaitForCommandList` (`Renderer.cpp:323-329`) adds a pending wait.
  - `ConsumePendingUpload` keeps its logic (`:1011-1024`) using timeline values.
  - `RT_BeginFrame`/`RT_RetireFrame` use the graphics timeline value recorded at each frame's end.
    That replaces the NVRHI event queries in `VulkanSwapChain`.
- Barrier emitter → `NRIBarrierEmitter` (§2.10 mapping). Delete `NVRHIBarrierEmitter` and the Debug
  cross-check.
- `Renderer/RHI/UploadRing.{h,cpp}`, `UploadContext` — **NEW** (§2.7):
  - `RecordResourceUpload(record)` takes `UploadContext&`. Its callers: `Image2D::SetData`
    (`Image.cpp:397-419`), `VertexBuffer` (`:31-34`), `IndexBuffer`, `TextureCube` (`Texture.cpp:1004`),
    `Texture2D` paths, `Mesh.cpp:344` meshlets, and `Texture2D::SetData` (`Texture.cpp:427-445`,
    which today creates its own command list per call: fold it into the batch).
  - `StorageBuffer::RT_SetData` GPUOnly and `VertexBuffer::RT_SetData` use `UploadRing::RT_WriteBuffer`
    on the given command buffer.
  - `FlushResourceUploads` submits the batch on graphics (or copy, if enabled) through NRI.
- `CopyImage`, `ClearImage`, `clearBufferUInt`, `Texture2D::CreateFromSRGB` → `CmdCopyTexture`,
  `CmdClearStorage`, `CmdZeroBuffer` (§2.10).
- Readback (§2.7): `Image2D::CopyToHostBuffer`, `Texture2D/TextureCube::CopyToHostBuffer`.
- Queries:
  - Named timers: one `QueryPool(TIMESTAMP)` per slot (capacity 2048, today's `maxTimerQueries`,
    `VulkanDeviceManager.cpp:705`) with a name → index map per slot. `RT_BeginTimerQuery`/`RT_EndTimerQuery`
    call `CmdEndQuery` twice. Results are read at the slot's next `RT_Begin` (retired) with
    `timestampFrequencyHz`. Keep the smoothing (`kGPUTimeSmoothing`, `:24, 166-206`) and the
    multi-segment frame total semantics (`Begin(true)`, `Rendering.md § GPU timing`).
  - Pipeline statistics: `QueryPool(PIPELINE_STATISTICS)` per slot, mapped to `PipelineStatistics`.
    This fixes the leak.
- Markers: `CmdBeginAnnotation(name, bgra)` / `CmdEndAnnotation`, plus Aftermath checkpoints on the
  native command buffer (P7 helper).
- Tracy: `VkCtxScope` gets `VkCommandBuffer(NRI.GetCommandBufferNativeObject(cmd))` (`:428-440`).
- Device lost: map `Result::DEVICE_LOST` from `QueueSubmit` and `Wait` to the P7 path, plus
  `nriReportDeviceLostInfo` logged.
- Delete NVRHI resource wrappers (`m_Info.ImageHandle` as `nvrhi::TextureHandle`, `GetHandle()`
  returning NVRHI) across `Image.h`, `Texture.h` and the buffer headers. `GetHandle()` becomes
  `nri::Texture*`/`nri::Buffer*` (renderer-internal).
- `VulkanSwapChain` (still Lux-owned): present-semaphore signalling moves into the last frame
  submit; the wrapped release semaphore is waited by `QueuePresent`. Acquire-semaphore waits go
  through the `RT_Submit` wait (ImGui swapchain pass, `RuntimeLayer.cpp:325-335`).
  `device->queueSignalSemaphore` + `executeCommandLists(nullptr, 0)` (`VulkanSwapChain.cpp:432-434`)
  become an NRI submit with only a signal. Frame pacing comes from the timeline.

**Playbook:** none.

**Thread and lifetime:**
- Submission happens on the render thread under `LockQueue`.
- Allocators are reset only for retired slots.
- The upload ring's per-slot reset also happens on retirement.

**Verification**
- Goldens for all scenes and the sweep.
- **Sync validation**: zero hazards, or ≤ baseline with no new unique ones.
- NRI validation clean.
- Async compute ON and OFF; async transfer ON (expect correct images; it remains off by default).
- Pass timings and the frame GPU time in the HUD within ±3% of P0, with the multi-segment frame
  (WorldOverlay2D) total correct.
- Tracy capture shows GPU zones. 🧑 or a Linux Tracy client if available.
- Thumbnails and readback.
- Golden capture itself uses the new readback path, so make sure it still works.
- Export a project and run it.
- 🧑 Windows MT.

**Docs**
- `Rendering.md § GPU timing`: NRI query pools.
- `§ Batched resource uploads`: `UploadRing`/`UploadContext`.
- `§ Validation errors`: the tracker owns barriers.
- `Threading.md`: the queue lock covers NRI submits.

**Exit criteria:** NVRHI command lists are no longer created:
`grep -rn "createCommandList\|executeCommandList\|setTextureState\|commitBarriers\|writeBuffer\|writeTexture" Core/Source`
returns nothing.

**Rollback:** revert to P12. Large, but self-contained.

---

### Phase 14 — Swapchain on NRI (main window, ImGui platform windows, runtime)

**Goal:** `NRISwapChain` presents everything. Lux's raw-Vulkan swapchain and surface code is
deleted.

**Changes**
- `Renderer/RHI/RHISwapChain.{h,cpp}` — **NEW** (§2.6). The API mirrors what callers use today:
  `Create(GLFWwindow*, w, h)`, `OnResize`, `BeginFrame` (acquire; `OUT_OF_DATE` → `m_NeedsRecreate`),
  `Present`, `NeedsRecreate`, `GetWidth/Height`, `GetBackBufferCount`, `GetCurrentBackBufferIndex`,
  `GetCurrentTexture/View`, `RT_GetAcquireWait()`.
  - It owns an acquire-semaphore ring (`CreateFence(SWAPCHAIN_SEMAPHORE)`) sized to `textureNum`,
    and a release semaphore per texture.
  - Back-buffer textures get resting state `Present`.
  - Keep the Linux vsync behaviour of `VulkanSwapChain.cpp:456-461` (`waitIdle` on present when
    vsync is on, not on Windows): `QueueWaitIdle(graphics)` with the same `#ifndef _WIN32`. Leave a
    comment explaining it is preserved, not endorsed.
- `Core/Platform/Windows/WindowsNativeWindow.cpp`, `Core/Platform/Linux/LinuxNativeWindow.cpp` —
  **NEW**: `nri::Window GetNativeWindow(GLFWwindow*)`. Linux branches on `glfwGetPlatform()`. These
  files are the only ones defining `GLFW_EXPOSE_NATIVE_*`.
- `Window.{h,cpp}`:
  - Replace `VulkanSwapChain* m_SwapChain` with `RHISwapChain`.
  - Delete `CreateWindowSurface`, `m_WindowSurface`, `InitSurfaceCapabilities` (`:306-312, 485-512`)
    and the `formatInfo` table (`:27-64, 169-185`; GLFW bit hints are meaningless with `GLFW_NO_API`).
  - `GetSwapChain()` returns `RHISwapChain&`.
  - The deferred-recreate logic in `ProcessEvents` is unchanged (`:544-578`).
- `ImGuiLayer.cpp:224-311`: `ImGuiViewportData::SC` becomes `RHISwapChain` created from
  `(GLFWwindow*)viewport->PlatformHandle`. Remove the `Platform_CreateVkSurface` requirement
  (`:305`). The platform-window render tasks (`:484-516`) use the new API.
- `RuntimeLayer.cpp:19, 325-335` and `ImGuiRenderer::RenderToSwapchain` (`:684-690`) use
  `RT_GetAcquireWait()`.
- `Framebuffer` swapchain targets: views from `RHISwapChain`.
- Delete `Platform/Vulkan/VulkanSwapChain.{h,cpp}`.
- `DeviceManager`/`VulkanDeviceManager`: delete swapchain-related params that no longer apply, and
  the `friend class VulkanSwapChain` (`VulkanDeviceManager.h:312`). The present queue family check
  stays for device selection (`FindQueueFamilies`).

**Playbook:** Platform divergence: both platform files are added in the same change (`Conventions.md`).

**Thread and lifetime:**
- Create/recreate on the main thread at `ProcessEvents`, with both threads idle.
- Acquire and present on the render thread.
- On recreate: wait for idle, destroy views and swapchain, recreate. This is the only non-queue
  destroy, and it is legal because the GPU is idle.

**Verification:**
- On Linux, under Wayland **and** X11 (`GLFW_PLATFORM=x11` or XWayland): window resize drag,
  minimise/restore (0×0), maximise, fullscreen.
- VSync on/off, present-mode switch (Application Settings), and buffer count 2/3/4. Under VSync off,
  present mode IMMEDIATE must exceed the refresh rate when focused (`Rendering.md § Present mode`).
- ImGui platform windows: create, move between monitors if available, close.
- Runtime player.
- Goldens.
- Smoke test and graceful close.
- 🧑 Windows DWM: the same matrix, plus the custom titlebar (recent commit `8a1fe83d`).

**Docs:** `Rendering.md § Present mode` (mapping to NRI flags); `Architecture §2.2 Window`.

**Exit criteria:** `grep -rn "VkSurfaceKHR\|vk::SurfaceKHR\|acquireNextImageKHR\|presentKHR\|createSwapchainKHR" Core/Source`
returns nothing.

**Rollback:** revert to P13.

---

### Phase 15 — Remove NVRHI

**Goal:** NVRHI is gone from the tree and the build.

**Changes**
- `git rm Core/vendor/nvrhi` (submodule) and its `.gitmodules` entry.
- `premake5.lua:171-208`: delete `HazelRootDirectory`, `DefaultTargetParams`, the NVRHI include
  and the project overrides. Check that `scripts/compat/` (`Hazel-ScriptCore/Source/Dummy.cpp`) is
  referenced only by `Core/vendor/nvrhi/premake5.lua:52,67,97`; if so, delete it.
- `Dependencies.lua`: delete the `NVRHI` entry (`:246-249`).
- Delete `Renderer/DeviceManager.{h,cpp}`. `VulkanDeviceManager` stops deriving from it and is
  renamed in P16. `DeviceCreationParameters` moves into `VulkanDeviceManager.h` minus NVRHI fields
  (`swapChainFormat` as `nvrhi::Format`, `enableNvrhiValidationLayer`). Preserve the
  `LUX_HAS_DX11/DX12` scaffolding: move the guarded DXGI/D3D includes and the
  `CreateD3D11`/`CreateD3D12` declarations into `RHIDevice.h` as reserved branches, with a comment
  that a D3D backend would come through NRI's D3D11/D3D12 support (`NRI_ENABLE_D3D1x_SUPPORT`,
  currently commented in the fork's premake).
- `Application.h/.cpp`, `Window.h/.cpp`: delete `GetGraphicsDevice`/`GetGraphicsDeviceManager`
  (move callers to `RHIDevice`), `m_DeviceManager` (becomes `VulkanDevice`), and the
  `runGarbageCollection` call (`Application.cpp:297`).
- `NVRHIInterop.{h,cpp}`: delete. Keep the `static_assert`s on the binary formats, rewritten
  without NVRHI as literal values.
- `VulkanDeviceManager.cpp`: delete NVRHI device creation (`:698-732`), `nvrhi::vulkan::resultToString`
  uses (use `vk::to_string`), `DefaultMessageCallback`, and the `nvrhi::validation` include.
- Scripts:
  - `scripts/Linux-Build.sh`, `scripts/Linux-Fetch.sh`, `Building.md:62`: replace "the vendored
    NVRHI requires 1.4.318+" with NRI's real minimum Vulkan header version. Find it in NRI's
    sources or CMake (`VK_HEADER_VERSION` checks) and keep the 1.4.335.0 pin unless NRI needs newer.
  - CI (`.github/workflows/main.yml`): no NVRHI-specific steps expected; confirm.

**Verification:**
- Clean clone test: `git clone --recursive` into `/tmp`, run `scripts/Linux-Build.sh debug` (or the
  agent's scratch directory), and confirm it builds from scratch.
- All configs build.
- Goldens pass.
- `grep -rn "nvrhi" Core/Source Editor/Source Lux-Runtime premake5.lua Dependencies.lua scripts .github`
  returns nothing.
- 🧑 Windows clean build via `scripts\Setup.bat`.

**Docs:** full sweep (Phase 16 finishes it). `Building.md` projects table and workarounds list
(remove the NVRHI bullet), `Conventions.md § DX11/DX12` (new location of the scaffolding).

**Exit criteria:** as verified.

**Rollback:** revert to P14. NVRHI still exists in history.

---

### Phase 16 — `Platform/Vulkan/` final shape, hardening, numbers, docs

**Goal:** the user's folder goal, plus every Goal-card success criterion proven.

**Changes: moves and renames.** Keep the history readable: one commit per move, no edits in the
same commit.
- `Platform/Vulkan/DescriptorSetManager.{h,cpp}` → `Renderer/DescriptorSetManager.{h,cpp}`.
- `Platform/Vulkan/VulkanShader.{h,cpp}` → `Renderer/RHI/RHIShader.{h,cpp}`. Rename the class
  `VulkanShader` → `RHIShader`, updating every `Ref<VulkanShader>` / `.As<VulkanShader>()` (grep).
  `Shader::Create` returns `RHIShader`.
- `Platform/Vulkan/VulkanShaderResource.h` → `Renderer/ShaderReflection.h`.
- `Platform/Vulkan/VulkanShaderUtils.h` → split: stage and format helpers into `Renderer/RHI/RHITypes`;
  SPIR-V/shaderc helpers into `ShaderCompiler/ShaderCompilerUtils.h`.
- `Platform/Vulkan/VulkanDeviceManager.{h,cpp}` → `Platform/Vulkan/VulkanDevice.{h,cpp}` (class
  `VulkanDevice`): instance, debug callback, physical-device pick, queue families, device creation
  with NRI-required features and Aftermath. Move the Tracy context (`:741-799`) out to
  `Platform/Vulkan/VulkanGPUProfiler.{h,cpp}`.
- `ShaderCompiler/` stays: it is the SPIR-V toolchain. Leave `VulkanShaderCompiler` named as is;
  it targets Vulkan SPIR-V.
- **End state** of `Platform/Vulkan/`: `VulkanDevice.{h,cpp}`, `VulkanGPUProfiler.{h,cpp}`, `Debug/*`
  (Aftermath + `AftermathCheckpoints`), `ShaderCompiler/*`. Every raw `vk*`/`vk::` use outside this
  folder is listed with a reason in `Rendering.md`. Expected: `vkCmdSetLineWidth` (D15) only.
- Delete leftovers found by
  `grep -rn "vulkan.h\|vulkan.hpp\|Vk[A-Z][A-Za-z]*\b\|vk::" Core/Source/Lux --include=*.{h,cpp} | grep -v "Platform/Vulkan/"`.

**Hardening matrix** (results in Appendix C):

| Axis | Values |
|---|---|
| OS / GPU | Linux RADV Renoir (agent); Windows RTX 4070 Ti 🧑 |
| Window system | Wayland, X11 (Linux); DWM (Windows) |
| Threading policy | Single, Multi. Linux Multi under X11 only; Wayland Multi races are pre-existing (`Threading.md`) |
| Config | Debug (validation + NRI validation + sync validation run), Debug-AS (Windows 🧑), Release, Dist |
| Product | Editor; Lux-Runtime from build folder; **fresh export**; **P0 NVRHI-era export** (old `ShaderPack.lsp` with the new runtime) |
| Features | the full sweep, async compute on/off, async transfer on (non-default), VRS (RTX), mesh shaders (RTX), Aftermath forced hang (optional 🧑) |
| Workflows | shader hot reload, texture hot reload, project switch, scene switch ×20 under load (device-lost recipe in `Rendering.md`), material editor preview, thumbnails, content browser, play/stop ×20 |

**Numbers** (`/profile` protocol, same as P0):
- render-thread and main-thread CPU, GPU frame time, per-pass times, barrier count per frame
- startup time-to-first-frame
- Editor/Runtime binary sizes
- `Platform/Vulkan` line count

Compare to P0 in a table. Fix only regressions against the Goal card (CPU worse, GPU >3% worse).

**Docs: full sweep, required in this phase**
- `Rendering.md`: layers (NRI), invariants restated, resource states, GPU timing, uploads, present
  mode, Y convention, validation, the raw-Vulkan exceptions list, golden capture.
- `Threading.md`: the quick-reference table (`nvrhi` → `NRI`), frame loop, queue lock.
- `Building.md`: projects, submodules, workarounds, options (`--no-aftermath` semantics), Vulkan SDK
  minimum, CI notes.
- `Conventions.md`: RHI vocabulary rule, DX scaffolding location, platform files.
- `Architecture-LuxEngine.md`: §1 diagram (`REN --> NRI`), §2.2, §2.3, Part 3 profiling note,
  directory map.
- `.claude/skills/shader-debug/SKILL.md` (§E device lost: NRI and Aftermath), `send-pr/SKILL.md:72, 118`,
  `plan-le/SKILL.md:181` (`nvrhi` → `NRI`). Update the matching `.agents/skills/*/SKILL.md`
  adapters if they quote those lines (CLAUDE.md § Codex Compatibility).
- `CLAUDE.md`: the Overview and Renderer sections mention NVRHI only through docs, but check
  ("Vulkan backend lives in `Core/Source/Lux/Platform/Vulkan/`": rewrite).

**Exit criteria:** every Goal-card success criterion is checked off in Appendix C with evidence.

**Rollback:** moves are pure renames, so revert per commit.

---

## Part 4 — Verification protocol (applies to every phase)

1. **Build:** Debug and Release, Core then Editor then Lux-Runtime. Dist at P1, P6, P13, P15, P16.
   Grep the logs (see "How to execute").
2. **Smoke:** `LUX_SKIP_BUILD=1 timeout 16 ./scripts/Linux-Run.sh release`. The render loop is
   reached (`grep -c Renderer2D` on the log ≥ 44). Exit 124 or 0.
3. **Graceful close:** `LUX_GOLDEN_EXIT=1` run, or the env-gated close hook, must exit 0. The
   shutdown order matters in every phase that touches devices or the deletion queue.
4. **Goldens:** `python3 tests/rendering/golden_run.py --label pN` (add `--features` from P4 on),
   then `--compare nvrhi-baseline pN`. A failure is a stop; inspect the diff PNG first.
5. **Validation:** Debug run of FMODDemo and Benchmark; count and diff unique messages against
   `bin/golden/nvrhi-baseline/validation.txt`. A sync-validation run at P4, P5, P13, P16. NRI
   validation is on in Debug from P6.
6. **Perf:** at the end of P4, P9, P10, P13 and P16, `golden_run.py --perf` in Release, compared to
   P0. Script-launched runs are unfocused; compare like with like (`Rendering.md § Present mode`).
7. **`/cr`** before each commit, then commit (stage only your files).
8. **Log** the phase status, commit hashes, numbers and deviations in Appendix C.

---

## Part 5 — Risks

| # | Risk | Likelihood | Detection | Mitigation |
|---|---|---|---|---|
| R1 | The wrapped device lacks a feature NRI assumes, so a crash or validation error at layout or pipeline creation | High (hazel needed a 1.3 push-descriptor fix) | P6 self-test, NRI and VK validation at startup | Enable NRI's required feature set in `VulkanDeviceManager::createDevice`; keep the hazel patch |
| R2 | NVRHI/NRI state desync in one command buffer (render pass left open, stale bindings) | Medium | I4 assertions; validation (`vkCmdDraw` outside render pass, wrong layout) | `clearState()` at segment start; `CmdSetPipelineLayout` first; no NVRHI recording inside segments |
| R3 | A missed state requirement means vendor-specific corruption | High (classic) | P4 cross-check against NVRHI, sync validation, goldens on AMD and NVIDIA | Requirement inventory in P4; `UndeclaredAccess` in P5; never ship explicit barriers default-on until the sweep is clean |
| R4 | Lifetime bug after NVRHI reference counting is gone: destroy while in flight | Medium | "object in use" validation errors; ASan (Windows Debug-AS 🧑); deletion-queue Debug thread assert | D7 queue live since P3; all destroys through it; audit lambdas |
| R5 | The P3 frame-slot change breaks a per-frame assumption (hard-coded 3, slot == back-buffer) | Medium | goldens, flicker, validation | Grep `static_vector<…, 3>`, `% 3`, `GetCurrentBackBufferIndex` users; keep the back-buffer accessor explicit |
| R6 | Descriptor pool exhaustion (`OUT_OF_MEMORY` on allocate) | Medium | NRI result codes logged | Size from reflection × FIF; a Debug assert with the manager's debug name; grow by recreating the pool through the deletion queue |
| R7 | The hazel + upstream merge is huge or breaks | Medium | P6 step 2 | Fallback: upstream plus cherry-picks (logged) |
| R8 | Fork patch LUX-1 (line width) conflicts on future upstream merges | Low | merge | Keep it tiny and documented in the fork's README `LUX-PATCHES` section; the long-term fix is quad lines |
| R9 | NRI swapchain on Wayland/X11 (missing defines, X11 macro clash, extent errors) | Medium | P14 matrix | `NRI_ENABLE_WAYLAND_SUPPORT`, per-platform TUs, Lux-side clamp plus the fork clamp |
| R10 | CPU regression from per-draw descriptor binds and tracker overhead | Low–Medium | perf at P4, P9, P10, P13 | Redundant-state cache; tracker skips permanent and resting no-ops; measure before optimising |
| R11 | Serialized format drift (stage values, struct sizes) breaks old caches or packs | Low | P2 `static_assert`s; P0 export run | D17 |
| R12 | The P0 capture is not deterministic | Medium | P0 double run | Pin exposure, time and temporal passes in capture mode before anything else |
| R13 | An implicit NVRHI behaviour is not replicated: UAV auto barriers, implicit render-pass splits, `keepInitialState` restore, `writeBuffer` ordering | Medium | goldens, validation, inside-rendering assert | §2.3 UAV rule, P10 audit, `RestoreRestingStates`, D12 |
| R14 | Agent context loss over days | High | — | This document plus Appendix C are the source of truth; commit per verified step |
| R15 | Async transfer queue legality with NRI barriers | Low (off by default) | validation with the setting on | P13 copy-queue rule (no resting restore on copy queue) |

---

## Part 6 — Open questions (each resolved in the phase named)

- **Q1 (P6).** Which extra features or extensions does NRI's wrapped-device path require on RADV
  Renoir and on NVIDIA? Resolve by reading `DeviceVK.hpp` and running the self-test.
- **Q2 (P8).** Do NRI-created images carry the TRANSFER usage NVRHI's clear, copy and `writeTexture`
  need on wrapped handles? Resolve by creating the first texture and running validation.
- **Q3 (P10).** Which passes, if any, record non-rendering commands inside
  `BeginRenderPass`/`EndRenderPass`? Resolve with the inside-rendering assert sweep.
- **Q4 (P13).** What is the right NRI layout for depth that is depth-tested read-only and sampled in
  the same pass (`DEPTH_STENCIL_READONLY` vs `DEPTH_READONLY_STENCIL_ATTACHMENT` for D32S8)? Resolve
  with sync validation plus goldens on tone-mapping and SSR.
- **Q5 (P14).** Which `VkFormat`/colour space does NRI choose for `SwapChainFormat::BT709_G22_8BIT`,
  and is it equivalent to today's `BGRA8_UNORM` + `SRGB_NONLINEAR`? Resolve by reading
  `SwapChainVK.hpp` format selection and doing a visual check 🧑. If not equivalent, pick the
  matching enum or patch the fork.
- **Q6 (P2).** What is the exact text of `nvrhi::utils::ShaderStageToString`? Copy it.
- **Q7 (P6).** What is the minimum Vulkan header and SDK version NRI v181 needs? It decides whether
  the 1.4.335.0 pin stays.
- **Q8 (P13).** Do Tracy's `VkCtxScope` timestamps on NRI-recorded native command buffers still pair
  with Lux's NRI timestamp queries (both in the same command buffer)? Resolve with a Tracy capture 🧑.
- **Q9 (after P16, user).** Re-plan `docs/BINDLESS_PLAN.md` on NRI (D2). Its Phase 0 numbers come
  from this plan's P0.

**What would invalidate this plan:** if NRI's wrapped-device path (`nriCreateDeviceFromVKDevice`)
cannot coexist with NVRHI on one device, the side-by-side strategy (D1) falls. Examples: NRI
requires owning queue creation, or its dispatch table conflicts with NVRHI's
`VULKAN_HPP_DEFAULT_DISPATCHER`. P6's self-test is designed to surface this in the first NRI phase.
If it fails, stop and return to the user with the parallel-backend option.

---

## Part 7 — Research notes

### Research brief: NVRHI → NRI migration

**Goal (from the Goal card):** replace NVRHI with NRI and minimise raw-Vulkan code, without
changing output.

- **Concept.** NRI explicitly lists "automatic barriers (better handled in a higher-level
  abstraction)" and "hidden management of any kind" as non-goals. Lux must supply both the state
  tracking and the lifetime management NVRHI did. This drives D5 and D7.
  [NRI README](https://github.com/NVIDIA-RTX/NRI)
- **Concept.** NVRHI's `keepInitialState` contract: a resource enters every command list in its
  initial state and is returned to it at close. Lux already relies on this everywhere (L4), so the
  tracker reproduces it as "resting states". NVRHI also tracks resource lifetime per command list,
  which is why Lux's never-drained release queue (L13) went unnoticed. Source: vendored
  `Core/vendor/nvrhi/doc/ProgrammingGuide.md:3, 46-52, 120`.
- **Prior art.** Studio Cherno's NRI fork (`hazel`) shows what an engine like Lux needed:
  - a premake build plus vendored VMA
  - an X11 `Window` clash fix
  - a Vulkan 1.3 push-descriptor crash fix
  - swapchain extent clamping
  - an `AcquireNextTexture` failure fix
  - a D32S8 readback-stride fix

  Its premake lacks `Debug-AS` and `NRI_ENABLE_WAYLAND_SUPPORT`, which P6 adds.
  [StudioCherno/NRI](https://github.com/StudioCherno/NRI) (compare `NVIDIA-RTX:main...hazel`)
- **Prior art.** Granite's render graph places barriers automatically: signal early, wait late,
  with events for in-queue and semaphores for cross-queue work. It informs P5's pass-entry
  batching; events are a follow-up.
  [Render graphs and Vulkan — a deep dive](https://themaister.net/blog/2017/08/15/render-graphs-and-vulkan-a-deep-dive/)
- **Pitfall.** Global pass-boundary barriers with all images in `GENERAL` layout avoid per-resource
  tracking. But the gains vary by vendor (−29% span on an RTX 5070, **+42%** on a Radeon 780M), and
  the approach still delegates aliasing, cross-queue and exceptional layouts to the engine.
  **Rejected** for Lux: per-resource tracking with resting states is the safer parity path, and Lux
  targets an integrated AMD laptop.
  [Global Pass Barriers Without Per-Resource RHI Tracking (arXiv 2607.26506)](https://arxiv.org/abs/2607.26506)
- **Concept.** Timeline-based deletion: tag each release with the timeline value of its last use
  and destroy it when the counter passes. It is more precise than fixed frame counting. P3 starts
  with frame-number retirement (NVRHI event queries) and P13 moves to the timeline.
  [Vulkan docs: Resource Lifetimes](https://docs.vulkan.org/tutorial/latest/Synchronization/Frame_in_Flight/03_resource_lifetimes.html)
- **Prior art.** NRI's samples (`AsyncCompute`, `BindlessSceneViewer`, `Readback`, `Resize`,
  `MultiThreading`) are the reference for queue timelines, bindless sets, readback and swapchain
  recreation. Read them before P9, P13 and P14.
  [NVIDIA-RTX/NRISamples](https://github.com/NVIDIA-RTX/NRISamples)
- **Rejected: `NRIImgui`.** It has no hook for Lux's per-texture `ForceOpaque`/`IsGrayscale` flags
  or the shared texture registry, and is off by default in CMake (D13).
- **Rejected: `NRIStreamer`.** Its batched copy semantics differ from NVRHI's in-order
  `writeBuffer` (D12). It could be revisited for initial asset uploads only.
- **Rejected: `NRIDescriptorHeap`.** It needs `VK_EXT_descriptor_heap`, which the bindless plan
  rules out, and RADV Renoir support is unverified.
- **Rejected: `NRIUpscaler` DLSS-SR/RR and FSR.** They are temporal and conflict with the standing
  no-temporal rule. Only NIS (spatial) could be a follow-up. This corrects the earlier suggestion in
  conversation that upscalers were a migration benefit.
- **Informs decisions:** D1, D3–D7, D10, D12–D15.
- **Still unknown:** Q1, Q5, Q7 (NRI specifics best settled by running code).

### Licenses and footprint

- NRI: MIT (`LICENSE.txt`, "Permission is hereby granted, free of charge…").
- VMA (bundled by NRI): MIT.
- NVRHI and RTXMU are removed.
- No new runtime installs.
- Measure binary size and startup in P0 and P16.

---

## Appendix A — File inventory (where each file changes, and its fate)

| File | Phases | Fate |
|---|---|---|
| `Platform/Vulkan/VulkanContext.*`, `VulkanDevice.*`, `VulkanRenderCommandBuffer.*`, `VulkanImGuiLayer.*`, `VulkanAPI.*`, `VulkanDiagnostics.*`, `Vulkan.*`, `VulkanAllocator.*` | P1 | deleted |
| `Renderer/RendererContext.*` | P1 | deleted |
| `Core/vendor/VulkanMemoryAllocator/` | P1 | deleted |
| `Platform/Vulkan/VulkanDeviceManager.*` | P1, P6, P7, P15, P16 | → `Platform/Vulkan/VulkanDevice.*` (+ `VulkanGPUProfiler.*`) |
| `Renderer/DeviceManager.*` | P1, P15 | deleted |
| `Platform/Vulkan/VulkanSwapChain.*` | P3, P8, P13, P14 | deleted (→ `Renderer/RHI/RHISwapChain.*`) |
| `Platform/Vulkan/VulkanShader.*` | P1, P2, P9, P12, P16 | → `Renderer/RHI/RHIShader.*` |
| `Platform/Vulkan/VulkanShaderResource.h` | P1, P2, P16 | → `Renderer/ShaderReflection.h` |
| `Platform/Vulkan/VulkanShaderUtils.h` | P2, P16 | split into RHITypes and ShaderCompilerUtils |
| `Platform/Vulkan/DescriptorSetManager.*` | P4, P9, P12, P16 | → `Renderer/DescriptorSetManager.*` |
| `Platform/Vulkan/ShaderCompiler/**` | P2, P7, P9 | stays |
| `Platform/Vulkan/Debug/**` | P7 | stays (+ `AftermathCheckpoints.*`) |
| `Core/Window.*` | P1, P6, P14, P15 | NRI swapchain, no surface code |
| `Core/Application.*` | P2, P3, P15 | no NVRHI; frame begin/end hooks |
| `Renderer/Renderer.*` | P1–P5, P8–P13, P15 | NRI throughout |
| `Renderer/RenderCommandBuffer.*` | P2, P4, P9, P10, P13 | NRI command buffers, tracker, queries |
| `Renderer/RenderGraph.*` | P2, P5 | access kinds and buffers |
| `Renderer/SceneRenderer.*` | P2, P4, P5, P10, P13 | Lux types; NRI draws through RCB helpers |
| `Renderer/Pipeline*.*`, `PipelineSpecification.h` | P1, P9, P10, P12 | NRI pipelines |
| `Renderer/Framebuffer.*` | P1, P10, P12, P14 | attachment views only |
| `Renderer/Image.*`, `Texture.*` | P1, P2, P4, P8, P9, P13 | NRI textures, views, uploads, readback |
| `Renderer/{Vertex,Index,Uniform,Storage}Buffer.*`, `*BufferSet.*` | P2, P4, P8, P9, P13 | NRI buffers |
| `Renderer/Material.*`, `RenderPass.*`, `ComputePass.h` | P4, P9, P12 | NRI sets |
| `Renderer/BindlessTextureTable.*` | P4, P9, P12 | NRI variable-size sets |
| `Renderer/Mesh.*` | P2, P4, P8, P9, P10 | NRI meshlet buffers and sets |
| `Renderer/Renderer2D.*`, `DebugRenderer.*` | P1, P4, P10 | through Renderer functions |
| `Renderer/ShaderPack.cpp`, `Serialization/ShaderPackFile.h` | P2 | `ShaderStage`, byte-compatible |
| `ImGui/ImGuiRenderer.*`, `ImGuiLayer.cpp`, `VulkanImGui.cpp` | P2, P4, P11, P14 | NRI |
| `Editor/Source/EditorLayer.cpp`, `Panels/RendererDebuggerPanel.cpp`, `Panels/MaterialEditor/MaterialEditorPanel.cpp` | P2, P4 | Lux types |
| `Editor/Source/Tools/GoldenCapture.*` | P0 | **NEW** |
| `Lux-Runtime/src/RuntimeLayer.cpp` | P13, P14 | RHI swapchain API |
| `Renderer/RHI/*` | P2–P16 | **NEW** |
| `Core/Platform/{Windows,Linux}/*NativeWindow.cpp` | P14 | **NEW** |
| `tests/rendering/golden_*.py` | P0 | **NEW** |
| `premake5.lua`, `Dependencies.lua`, `Core/premake5.lua`, `.gitmodules`, `scripts/compat/`, `scripts/Linux-*.sh` | P1, P6, P15 | NRI in, NVRHI out |
| `Core/vendor/NRI` (submodule `starbounded-dev/NRI@lux`) | P6, P10 (LUX-1) | **NEW** |

## Appendix B — Command cheat sheet

```bash
# confirm branch before any commit
git branch --show-current            # must print: dev

# build (Linux)
make config=debug Core   2>&1 | tee bin/build-core.log
make config=debug Editor 2>&1 | tee bin/build-editor.log
grep -E "error:|Linking (Core|Editor)" bin/build-*.log

# smoke
LUX_SKIP_BUILD=1 timeout 16 ./scripts/Linux-Run.sh release; echo "exit=$?"

# goldens
python3 tests/rendering/golden_run.py --label pN --features
python3 tests/rendering/golden_run.py --compare nvrhi-baseline pN

# sync validation run
VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT \
  LUX_SKIP_BUILD=1 timeout 60 ./scripts/Linux-Run.sh debug

# residue checks
grep -rn "nvrhi" Core/Source Editor/Source Lux-Runtime | wc -l
grep -rn "vk[A-Z][A-Za-z]*(\|vk::" Core/Source/Lux --include=*.cpp | grep -v Platform/Vulkan/
find Core/Source/Lux/Platform/Vulkan -name '*.[ch]*' | xargs wc -l | tail -1
```

## Appendix C — Execution log (the agent keeps this current)

| Phase | Status | Commits | Notes, numbers, deviations, user checkpoints |
|---|---|---|---|
| P0 | tooling done; baseline runs 🧑 | `35a787e6` | Tool, scripts, `GetPendingAsyncLoadCount`, docs committed. A FMODDemo smoke capture produced a correct image before runs moved to the user. **🧑 To record the baseline:** `python3 tests/rendering/golden_run.py --label nvrhi-baseline --features`, then the same with `--label nvrhi-baseline-2`, then `python3 tests/rendering/golden_run.py --compare nvrhi-baseline nvrhi-baseline-2` (must pass). Validation: add `--sync-validation` on a Debug run. Windows: same with `--label nvrhi-baseline-win`. The RenderGraph self-test failure and the empty `TextureCube` readback seen during P0 are both fixed (see "Out-of-phase fixes" below). |
| P1 | done (Release built) | `77fd88d6` | Last commit containing `VulkanDevice.cpp` (Aftermath enable code, ~lines 255-305) is **`bf8e6c90`** — use `git show bf8e6c90:Core/Source/Lux/Platform/Vulkan/VulkanDevice.cpp` in P7. Deleted 18 Platform/Vulkan + RendererContext files, `vendor/VulkanMemoryAllocator`, the dead `#if` blocks, Donut leftovers in `DeviceManager`, and PipelineCompute's stubs/command list. Also removed `RendererData`'s raw `VkDescriptorPool`/`VkWriteDescriptorSet` fields (not listed in the plan; they referenced the deleted `ShaderMaterialDescriptorSet`). `GetGPUMemoryStats` now queries `VK_EXT_memory_budget` itself; the old path read an allocator that was never initialised. Checks: Release Core/Editor/Lux-Runtime compile and link; `nm … Editor \| grep -c vmaCreateAllocator` = 0; exit grep clean (with `\bVulkanDevice\b` — the plan's old pattern matched the live `m_VulkanDevice`). Footprint: `Platform/Vulkan/**` 9135 → 6662 lines; Release `Editor` 16,440,224 B, `Lux-Runtime` 14,011,088 B. **Not run:** Debug/Debug-AS/Dist builds; **🧑** smoke + `golden_run.py --label p1` and `--compare nvrhi-baseline p1` (needs P0 baseline first); 🧑 Windows build. |
| P2 | done (Release + Debug spot-built) | see git log | `sizeof(ShaderModuleInfo)` = 24 before and after (Stage at 18, Flags at 20), now pinned by `static_assert`s in `ShaderPackFile.h`. `RHITypes.{h,cpp}` + `NVRHIInterop.{h,cpp}` added; every enum value is `static_assert`ed equal to NVRHI's, and the stage strings are NVRHI's verbatim, so reflection, the shader cache and `.lsp` files are unchanged (the old `VkDescriptorBufferInfo` slot held uninitialised bytes; `LegacyDescriptorBufferInfo` writes zeros in the same 24 bytes). Release Core/Editor/Lux-Runtime build; files with new asserts compiled in Debug. Exit grep (source files only): clean except `Application.h` (the plan's own `nvrhi::IDevice` forward declaration) and `Window.cpp`. **Deviations:** (1) `Window.cpp` keeps NVRHI: it is device bring-up (swapchain format table, `GraphicsAPI`, feature query) and moves with the swapchain in P14. (2) `Application.h` no longer includes `nvrhi.h`, but `Window.h` → `DeviceManager.h` still does, so nvrhi stays in that include graph until P14. (3) `SamplerSpecification::AddressMode` reuses the existing `TextureWrap` (Clamp/Repeat are the only modes used) instead of a new near-duplicate enum; `ToNVRHI(TextureWrap)` replaced `Texture.cpp`'s private `NVRHISamplerWrap`. (4) `Renderer::ClearImage` has no default range: NVRHI's default meant mip 0/layer 0 only while a Lux default means everything, and no caller relied on it. (5) `ShaderPack.cpp` had an unused `Utils::ShaderStage` enum and two converters with no callers; removed, since it shadowed the new type inside `Lux::Utils`. (6) The raw-texture `CreateFrameTexture` is now private `RegisterFrameTexture`; it had no callers left besides the new `Ref<Image2D>` overload. **🧑 To check:** warm editor start with the existing `Editor/Resources/Cache/Shader/` (no extra `Compiling` lines vs. a pre-P2 start); the P0 export run with the P2 `Lux-Runtime`; goldens. |
| P3 | done (Release + Debug spot-built) | see git log | `Renderer/RHI/GPUDeletionQueue.{h,cpp}` (frame-tagged releases, `RT_Retire`, `DrainAll`, pending count/bytes; Debug traces the pending count at most once a second when it changes). `Renderer::RT_BeginFrame` closes the ending frame behind a graphics-queue event, advances `RT_GetFrameNumber()`, waits for frame N − FramesInFlight and retires up to it; `RT_GetCurrentFrameIndex()` = frame number % FramesInFlight; shutdown waits idle then drains. `RendererConfig::MaxFramesInFlight` (3) now sizes the per-frame `static_vector`s and clamps `FramesInFlight` — the old back-buffer index could reach 7 with an 8-image swapchain and overrun `DescriptorSetManager::m_BindingSets`. The back-buffer clamp in `Renderer::Init` is kept (comment says why). Audit: the only `SubmitResourceFree` caller is `~BindlessTextureTable` (others were deleted in P1); it captures by value and its destructors are thread-safe. **Deviations:** (1) no `RT_EndFrame`/swapchain-tagged events: the frame is closed at the *next* `RT_BeginFrame`, owned by `Renderer`, so the event covers work queued after `Present` and frames whose acquire failed (no `Present` pacing); `VulkanSwapChain` is unchanged. (2) `RT_BeginFrame` runs before the acquire, as the plan says. **🧑 To check (Debug + validation):** continuous viewport resize 30 s; toggle SSR/GTAO/Bloom 20×; shader hot-reload 10×; scene switch 10× — zero validation errors, the `GPU deletion queue` trace returns to 0, GPU memory back within ±5%; MAILBOX/IMMEDIATE 60 s each without stutter regression; goldens; Windows MultiThreaded. |
| P4 | code done, toggle **off** (Release + Debug spot-built) | see git log | `Renderer/RHI/ResourceStateTracker.{h,cpp}` (subresource-granular model, UAV→UAV counted as a barrier, `End()` restores resting states, Debug `CrossCheck` against NVRHI's `getTextureSubresourceState`/`getBufferState`), `NVRHIBarrierEmitter.{h,cpp}` (+ `DescribeTexture`/`DescribeBuffer`). `RenderCommandBuffer`: tracker per command buffer, `setEnableAutomaticBarriers(!explicit)` in `RT_Begin`, restore in `RT_End`, `RT_Require*`/`RT_Transition*`/`RT_CommitBarriers`/`RT_CommitMeshletState`. Setting `Renderer.ExplicitBarriers` (Application Settings toggle, latched in `RT_BeginFrame`). Sites converted: attachment clears, `ClearImage`, `CopyImage`, `CreateFromSRGB`, `clearBufferUInt`×2 sites, GPUOnly `StorageBuffer::RT_SetData`, `VertexBuffer::RT_SetData`, `PipelineCompute` barriers, `transitionMip`/`transitionBloomMip`, `Texture2D::GenerateMips`, meshlet state. **Deviations:** (1) bound-resource requirements are derived at the commit choke points from the bound NVRHI binding-set descs, mirroring NVRHI's own rules (changed sets, dirty after copy/clear/write, UAV sets always, VB/IB/FB/indirect on change; other kinds reset per commit) — not recorded per (frame, set) in `DescriptorSetManager`. That covers every binding source (passes, materials, meshlet sets) and reproduces automatic mode's barriers, so `GenerateMips` and the environment filter need no `RequireUAVBarrier` special case. P9 supplies descriptor-side uses when sets move to NRI. (2) Resting states are read from the NVRHI `keepInitialState` desc (where they live today), not duplicated onto Lux objects; P8 adds them when resources move to NRI. (3) Depth attachments follow NVRHI's framebuffer `isReadOnly` rule, not the pipeline's `DepthWrite`. (4) ImGui and the two readback command buffers stay on automatic barriers (`SetAutomaticBarriersOnly`): staging textures have no public state API, and ImGui uses `beginTrackingTextureState`; P11/P13 remove both. (5) Renderer Debugger UI unchanged: the tracker is per command buffer, so a frame-level state view comes with the render graph (P5); per-tracker stats are on `RT_GetTracker().GetStats()`. (6) **The toggle stays off**: turning it on by default is the exit criterion, gated on the checks below. **🧑 To check, with the toggle on and off:** goldens (`--compare nvrhi-baseline p4-explicit`), Debug validation + sync validation (no new unique messages), zero `Tracker/NVRHI state mismatch` lines over the sweep, HDR environment + Preetham sky changes, Content Browser thumbnails, Material Editor preview, viewport resize during play, render-thread CPU time ±5%; Windows (NVIDIA) goldens + validation. Then flip the default to on (`Renderer.ExplicitBarriers` read default in `EditorLayer.cpp` and the settings panel). |
| P5 | done (Release + Debug spot-built, self-tests pass) | see git log | `RenderGraph`: `AccessKind`/`ResourceAccess`, `PassDesc::Accesses` (after `DebugName`), buffers via `AddExternalBuffer` (tagged handles, textures-then-buffers slots, never aliased), Reads/Writes extended from Accesses in `AddPass`, Accesses + buffers folded into the structure hash, compiled per-pass entry requirements (single-state resources only), `Execute(result, commandBuffer)` requiring them in one batch per pass (explicit barriers on), Debug `UndeclaredAccess` check via a tracker requirement log, `GetRuntimeDiagnostics()`. Four new self-tests (buffer lifetime/culling/external input, access-derived Reads/Writes, hash sensitivity, entry requirements) — all pass in the CPU-only harness. `SceneRenderer`: every pass gets kinds; buffers declared for Mesh Culling, Cluster Build/Light Culling, Selected Geometry, GBuffer, Deferred Lighting, GBuffer Debug, Transparent Forward, Auto Exposure, Composite; `UntrackedResources` and Auto Exposure's explicit `SideEffect` removed; async-compute cross-queue edge left as a follow-up comment. Renderer Debugger shows buffers in pass inputs/outputs and the runtime diagnostics. **Deviations:** (1) texture kinds are derived in `SceneRenderer::addPass` from each pass's existing Reads/Writes (graphics: written = attachments, color/depth by format, read+write = load/store; compute: written = storage write, read+write = declared both sampled and storage write so it gets no entry state; read-only = sampled), so texture membership — and therefore lifetimes and aliasing — is exactly what it was; buffers are listed explicitly. (2) `UndeclaredAccess` is resource-level (a graph resource required by a pass that did not declare it at all), not state-level: an in-pass state change on a declared resource is the pass's own business. (3) New kind `DepthReadWrite` (depth test against existing contents + write), since no Lux framebuffer has a read-only depth attachment. (4) Entry requirements allocate one small vector per pass per frame, only with explicit barriers on. (5) Debug builds name the executable graph's resources/passes (needed for the check). **🧑 To check (Debug, explicit barriers on):** zero `UndeclaredAccess` in the five scenes + feature sweep (Renderer Debugger → diagnostics); barrier count ≤ P4; goldens; buffers visible in the Renderer Debugger. |
| P6 | done (Release + Debug built; 🧑 push NRI `lux` before Lux `dev`) | NRI `lux`: `2d93520` (merge), `cc24eca`; Lux: see git log | **Merge, not fallback**: `lux` = hazel `fe76f4f` + upstream `main` `0994c32` (v180-305, `NRI_VERSION` 181); 5 conflicts, verdicts below. Submodule `Core/vendor/NRI` → `https://github.com/starbounded-dev/NRI` (the user forked there, not `sheazywi`), branch `lux`. NRI premake rewritten (upstream file layout via globs, Debug-AS, runtime per config, Wayland + X11). `RHI/RHIDevice.{h,cpp}`: `nriCreateDeviceFromVKDevice` with Lux's instance/device/extension lists and queue families (graphics; compute and copy only when created), Core/Helper/WrapperVK/SwapChain (+MeshShader when supported) interfaces, `DeviceDesc` log, Debug self-test steps 1–5. Release `nm`: `vmaCreateAllocator` defined once (NRI-VK); no extra Linux libraries needed. **Deviations:** (1) NRI builds against its own vendored Vulkan-Headers v1.4.364 (`External/VulkanHeaders`), because upstream needs `VK_EXT_descriptor_heap` and the pinned SDK is 1.4.335; NRI's public headers have no Vulkan types. (2) Fork patch `cc24eca`: platform surface functions load only when their extension is enabled (GLFW enables one; NRI hard-failed on the missing Xlib/Wayland one). (3) Feature parity: NRI reads support from the physical device, so `VulkanDeviceManager` now enables sync2 from core 1.3 support, 1.4 `maintenance5`/`maintenance6`/`pushDescriptor` (or `VK_KHR_maintenance5/6` + `VK_KHR_push_descriptor` on 1.3), `shaderDrawParameters`, `samplerFilterMinmax`, `hostQueryReset` and the descriptor-indexing UpdateAfterBind/UpdateUnusedWhilePending bits, all only when supported. (4) Self-test runs in `Window::Init` right after `RHIDevice::Init` (render thread idle, like Tracy's init), not after `Renderer::Init`; step 6 (wrap an NVRHI frame command buffer, I3/I4) moves to P9, where `RT_BeginNRISegment` is introduced. (5) NRI errors break in Debug via `OnNRIAbort` (NRI's default raises SIGTRAP, fatal without a debugger) and only log otherwise. **🧑 To check:** push NRI `lux` first (`git -C Core/vendor/NRI push origin lux`), then `dev`; CI green on all six jobs; one Debug editor start on Linux and Windows (NVIDIA) showing the `[RHI]` `DeviceDesc` lines and `[RHI] NRI self-test passed` with no `[NRI]` errors and no new validation messages; goldens unchanged. |
| P7 | not started | | |
| P8 | not started | | |
| P9 | not started | | |
| P10 | not started | | LUX-1 fork commit hash |
| P11 | not started | | |
| P12 | not started | | |
| P13 | not started | | |
| P14 | not started | | |
| P15 | not started | | |
| P16 | not started | | success criteria checklist with evidence |

**P6 — hazel patch verdicts** (merge of upstream `main` `0994c32` into hazel `fe76f4f`):

| Hazel patch | Verdict | Evidence |
|---|---|---|
| `premake5.lua` (`294b2cd`, `2756abe`, `47846cd`) | kept, rewritten | Upstream has no premake. File lists are globs, so new `Source/VK/*` (DescriptorHeap, Micromap, Video*, TransferContext, PipelineCache) and `Source/Shared/*` are picked up; Debug-AS added; `NRI_ENABLE_WAYLAND_SUPPORT=1` added; `VK_USE_PLATFORM_*` moved to NRI-VK only, as in CMake; NRIImgui stays off |
| `External/VMA/vk_mem_alloc.h` | kept, bumped | Now the commit CMake pins, `3aa921224c154a0d2c43912bc88e1c42ce1f7607` (3.4.0) |
| X11 `Window` clash (`97925db`, `Shared.cpp`) | dropped | Only NRI-Shared needed it, because hazel defined `VK_USE_PLATFORM_XLIB_KHR` for every project. With the macro scoped to NRI-VK (as CMake does), `Shared.cpp` builds unpatched |
| Push descriptors on Vulkan 1.3 (`2551a8b`) | dropped | Upstream loads `CmdPushDescriptorSet` with `GET_DEVICE_OPTIONAL_CORE_FUNC` ("v1.4 or VK_KHR_push_descriptor") |
| Swapchain extent clamp (`3f1d28b`) | kept, re-applied | Upstream `SwapChainVK::Create` still returns `INVALID_ARGUMENT` for an out-of-range extent |
| `AcquireNextTexture` failure handling (`a83f2e9`, `20c7808`) | kept, re-applied | Upstream still only rejects negative `VkResult`s, so `VK_TIMEOUT`/`VK_NOT_READY` count as success |
| D32S8 readback stride (`afbbe62`) | kept, re-applied | Upstream copy lambdas still use the 8-byte combined stride for a depth-only copy; the format is renamed `D32_SFLOAT_S8_UINT` |
| NGX / DLSS-RR (`48c5829`, `7e86ef2`, `7d96c63`, `47e8af4`) | dropped | Conflicted with upstream's upscaler rework (`NRIUpscaler.h`, `UpscalerInterface.hpp`); upscalers are out of scope. Upstream's upscaler files and `Impl*.cpp` signatures taken; the premake NGX gate is gone too (it pointed at a Hazel fetch script and added no library) |

**Windows builds come from CI.** GitHub Actions "Build LuxEngine" (`.github/workflows/main.yml`) builds Windows (`windows-2025`) and Linux in Debug, Release and Dist on every push to `dev`; check it with `gh run list` / `gh run view <id>`. Run `37390792151` (`42d6f2d1`) built P0, P1 and the out-of-phase fixes on all six jobs. The earlier red run `37365441018` was runner capacity ("job was not acquired by Runner"), not a compile failure. A phase's "🧑 Windows build" item is satisfied by a green CI run on the pushed commit; Windows *runs* (goldens, validation) are still the user's.

CI run `37412093880` (`3716bc2b`, P2–P4) built all six jobs (Windows + Linux, Debug/Release/Dist).

**Out-of-phase fixes (2026-10-05, on NVRHI, build-verified only):**
- `a2ed5ac5` RenderGraph self-test: the fixture's own `NullTexture` warning was counted. A
  CPU-only harness linked against `libCore.a` reproduced the old failure and passes now.
- `b8cd641e` BC readback: `Image2D::CopyToHostBuffer` returned 0 bytes for block-compressed
  formats, so packed `.dds` textures were empty (black) in exported games. ThumbnailCache's disk
  write is now limited to 8-bit RGBA so it cannot over-read BC data.
- `830696b8` `TextureCube::CopyToHostBuffer`/`CopyFromBuffer` implemented (mip-major, six faces per
  mip); the constructor's 4-bytes-per-texel sizing, the serializer's 1 s sleep, wrong mip count and
  leaked buffer fixed. Note: env maps are packed as source HDR bytes, so no live export used this.
- L13 release queue drained per frame behind event queries (see Phase 3 note); L24 `LineStrip`
  mapped; `Image2D` readback no longer unmaps after a failed map.
- Build: `Core/premake5.lua` deletes `libCore.a` before archiving on Linux. `ar -rcs` had kept the
  objects of every file deleted in P1 inside the archive, so P1's link check was weaker than
  logged; it was re-run against a clean archive and still links.

**Follow-ups (out of scope, recorded so they are not lost):**
- Re-plan bindless on NRI, with update-after-set and MUTABLE descriptors (D2, Q9).
- Quad-expanded wide lines to retire fork patch LUX-1.
- Narrower barrier stages, and events for split barriers.
- A cross-queue render-graph edge for async compute.
- NRIStreamer for initial asset uploads.
- NIS spatial sharpening/upscale (the only upscaler allowed by the no-temporal rule).
- D3D12 through NRI (the scaffolding is preserved; needs a user decision).
- Retire the Linux vsync `QueueWaitIdle` behaviour after measuring.
