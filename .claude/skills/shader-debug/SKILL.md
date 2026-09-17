---
name: shader-debug
description: LuxEngine shader debugging. Diagnoses a shader that won't compile, an edit that has no visible effect, black or garbage output, a startup crash after a shader change, binding collisions, and device-lost or vendor-specific GPU faults — using the engine's compile-error report, shader cache, manual hot reload, debug views, the validation layer, offline glslc/spirv-val, and RenderDoc. Use whenever a .glsl/.glslh/.hlsl change misbehaves or rendering output is wrong.
---

# shader-debug — LuxEngine shader debugging

Shader bugs here rarely look like shader bugs. A failed compile keeps the *old* shader running, a
stale cache survives restarts, a buffer declared at someone else's `(set, binding)` breaks a
*different* pass, and a shader the driver accepts can fault the GPU minutes later. This skill is
the order to check things in so none of those costs an afternoon.

`/shader-debug <symptom>` runs the triage for that symptom.

**Read first:** `.claude/docs/Rendering.md` — § *Shaders*, § *Invariant 1 — (set, binding) is a
GLOBAL namespace*, and § *Validation errors are bugs*. That doc is the authority; this skill is the
debugging procedure on top of it.

**Before any shader theory, rule out configuration.** If the complaint is "it looks different
since commit X", run `git log -p -- Editor/LuxSampleProject/LuxSample.luxproj` first. The editor
saves every renderer quality setting into that file, and a whole session has already been lost
debugging a shader regression that was a committed settings change.

---

## How the pipeline actually behaves

These are the facts the triage relies on. Each is from the source, not from convention.

**Sources and loading**

- Shaders live in `Editor/Resources/Shaders/`; includes in `Include/GLSL/` (`.glslh`),
  `Include/Common/` (`.slh`, shared GLSL/HLSL), `Include/HLSL/`. Paths are relative to the working
  directory, which must be the `Editor/` source folder.
- **Shaders are loaded by an explicit list** in `Renderer::Init` (`Core/Source/Lux/Renderer/Renderer.cpp`,
  `Renderer::GetShaderLibrary()->Load("Resources/Shaders/...")`). A new `.glsl` that is not in that
  list is never compiled.
- A file starts with `#version`, and `#pragma stage : vert|frag|comp|task|mesh` splits it into
  stages (`ShaderPreprocessor.h`); each stage runs from its own `#version` line. A malformed
  `#version`, a malformed stage pragma, or a file with no stage pragma at all is a
  `LUX_CORE_VERIFY` — a crash, not a log line. Headers without `#pragma once` log a warning.
- Each stage is compiled with `__GLSL__`, its stage macro (`__VERTEX_STAGE__`,
  `__FRAGMENT_STAGE__`, `__COMPUTE_STAGE__`, `__TASK_STAGE__`, `__MESH_STAGE__`), and every global
  macro from `Renderer::SetGlobalMacroInShaders` (grep for it to see the current set).

**Compilation** (`Platform/Vulkan/ShaderCompiler/VulkanShaderCompiler.cpp`)

- shaderc, target Vulkan 1.2, **warnings are errors** (`SetWarningsAsErrors`). An unused-variable
  warning fails the build of that shader.
- Every stage is compiled twice: a **debug** binary (debug info, unoptimized — used for
  reflection) and a **runtime** binary (optimized, except compute stages, which are never optimized
  because of a shaderc internal error). **Pipelines run the runtime binary**, which has no debug
  info.

**The cache** — this is where most "impossible" behaviour comes from

- Binaries: `Editor/Resources/Cache/Shader/Vulkan/<file>.cached_vulkan[_debug].<stage>`.
  Reflection: `<file>.cached_vulkan.refl` in the same folder. Change registry:
  `Editor/Resources/Cache/Shader/ShaderRegistry.cache` (per-stage hash of the source and every
  included header).
- **A compile error with a cached binary available keeps the old shader running.** The error report
  says `Cache fallback: A cached binary was loaded, so the old shader can keep running.` The screen
  not changing does not mean the edit was ignored — it means it failed.
- **The registry records the new hash before compiling** (`VulkanShaderCache::HasChanged`
  serializes first). So after a failed compile, the next launch sees the stage as unchanged, loads
  the old cached binary, and **logs no error at all**. The only way to re-surface the error is a
  forced compile (below).
- With no cached binary, a startup compile failure logs
  `Shader '<path>' was not loaded because compilation failed and no usable cache was available.`
  and the shader is null; a failed hot reload logs `Failed to recompile shader!` at fatal level
  (it does not abort).
- A reflection cache with a bad header trips `LUX_CORE_VERIFY(validHeader)` — `Verify Failed` with
  no file or line.

**Reloading** — there is no file watcher

- **Ctrl+Shift+R** (Edit → Reload All Shaders) force-recompiles every shader. Renderer Debugger →
  **Shaders** tab has Reload All and a per-shader Reload.
- Reload runs on the render thread (`VulkanShader::Reload` → `Renderer::Submit`). Dependents are
  rebuilt by `Renderer::OnShaderReloaded` **only if they were registered** with
  `Renderer::RegisterShaderDependency`. An unregistered pipeline keeps the old module forever —
  reload "does nothing" for that one pass.

**Where errors go**

- The editor **Log** panel and `Editor/logs/LUX.log`. Compile errors are a structured block:
  `Shader`, `Stage`, `Permutation` (Debug/Optimized), `Exact line`, `Source line`,
  `Cache fallback`, `Macro set`, `Compiler output`.
- `Source line` is looked up in the *preprocessed, include-expanded* stage text. If it doesn't
  match the complaint, trust the `file:line` in `Compiler output` and open that file.
- A `Shader pre-process error` (usually a bad `#include`) is reported separately; the compile error
  that follows it is a consequence, not a second bug.
- The file log is not flushed on abort. After a `VERIFY` crash the tail of `LUX.log` can be missing
  — the console output is more complete.

**Shipped games**

- Dist has no shader compiler (`LUX_HAS_SHADER_COMPILER` is `!LUX_DIST`); the runtime loads
  `ShaderPack.lsp`, written during runtime export (`ShaderPack::CreateFromLibrary`). A source fix
  does not reach an exported build until it is exported again.

---

## Triage by symptom

### A. "It doesn't compile" / compile error in the log

1. Press **Ctrl+Shift+R** so the error is current (a relaunch may be hiding it — see the cache).
2. Read the structured block: which **stage**, which **permutation**, which **macros**. Errors that
   only happen under one macro set are a `#if` branch you didn't test.
3. Remember warnings are errors.
4. If the message is confusing, reproduce offline (Tools, below) — `glslc` output is identical in
   substance and faster to iterate on.

### B. "My edit has no effect"

Check in this order; stop at the first hit.

1. **It failed to compile** and the cached binary is running. Ctrl+Shift+R and read the log.
2. **It was never reloaded.** There is no watcher. Ctrl+Shift+R.
3. **The pass didn't pick it up** — its pipeline/material/pass is not registered with
   `Renderer::RegisterShaderDependency`. Restart the editor to confirm: if a restart shows the
   change, registration is the bug.
4. **You edited a different file than the one compiled.** Includes search `Include/GLSL/` then
   `Include/Common/`; a same-named header in both, or a relative include, can shadow. Confirm the
   shader is in the `Renderer::Init` load list.
5. **The code path isn't live.** The feature is off, the pass is culled or gated, or a global macro
   selects the other branch. Check the pass in Renderer Debugger → **Render Graph** (executed vs
   culled) and the macro set.
6. **You're looking at the wrong image.** Renderer Debugger → Render Graph → **Render Pass
   Isolation** may be pinned to a debug view.
7. **The cache is corrupt.** Close the editor, delete `Editor/Resources/Cache/Shader/`, relaunch.
   Startup is slow on a cold cache — that is expected.

### C. Black, garbage, or wrong output

1. **Search the log for `binding collision`.** `Uniform buffer binding collision at (set=…,
   binding=…)` or `Storage buffer binding collision …` means two differently-named buffers share a
   slot and another pass is reading the wrong one. Treat it as a build break. Pick a new slot after
   grepping the whole corpus:
   `grep -rn "set = 1, binding = 17" Editor/Resources/Shaders/`. (Only uniform and storage buffers
   share the global namespace; textures and images are reflected per shader.)
2. **Search for descriptor errors:** `[RenderPass (…)] Input <name> not found`,
   `Resource is null! <name> (set.binding)`, `Required resource is wrong type!`,
   `Bake - Validate failed!` (`DescriptorSetManager.cpp`). A `Resource is null!` naming a buffer the
   shader never declared is a collision from step 1.
3. **Check C++/GLSL layout agreement.** A `UB*` / `CB*` struct in `SceneRenderer.h` and its GLSL
   block must match field for field under std140/std430. `vec3` aligns to 16 bytes. This is never a
   compile error on either side — it is silent garbage.
4. **Isolate the pass.** Renderer Debugger → **Render Graph** tab → **Render Pass Isolation**:
   Geometry, Depth, Normals, SSR, AO, Bloom, Composite, the GBuffer channels, Deferred, the GPU scene
   views (primitive/material/object IDs, bounds, motion) and the material views (texture validity,
   alpha mode, roughness, metalness, missing). Walk forward until the image goes wrong; the bug is in
   that pass or its inputs. The views are suspended while Play is running.
5. **Read the Render Graph diagnostics** in the same tab: `ReadBeforeWrite`, `DeadWrite`,
   `AliasLifetimeConflict` and friends. A pass reading a resource it never declared reads aliased
   memory.
6. **Capture in RenderDoc** (Tools, below) and inspect the failing draw's bound resources and
   inputs.

### D. Crash at startup after a shader change

1. **Bad preprocessor input** — malformed `#version` / `#pragma stage` → `VERIFY`. Check the file
   header first.
2. **Corrupt or stale reflection cache** → `Verify Failed` with no location. Delete
   `Editor/Resources/Cache/Shader/` and relaunch.
3. **Binding collision** leading to a pass failing `Validate` — search the console output for
   `binding collision` and `Resource is null!`.
4. If the log gives nothing, get a stack: symbolize with `llvm-symbolizer` against the exact exe
   that crashed, using the faulting offset from the Windows Application event log
   (`Get-WinEvent`, provider `Application Error`). Offsets are per build — never symbolize an old
   offset against a new exe.

### E. Device lost, TDR, or "works on one GPU, broken on another"

1. **Run with the validation layer and read the first error, not the loudest.** After a device is
   lost, the timeline semaphore reads `UINT64_MAX`, nvrhi retires every command buffer at once, and a
   burst of `vkDestroyDescriptorPool` errors follows. That burst is the aftermath. Scan backwards
   for the earliest shader-module or pipeline-creation error.
2. **A `vkCreateShaderModule` capability error is never cosmetic.** "SPIR-V Capability X was
   declared, but <feature> is required" means the module is illegal; the driver may run it until it
   faults far from the cause. Enable the device feature (`VulkanDeviceManager.cpp`) or change the
   shader.
3. **Validate the SPIR-V offline** with `spirv-val` (below). Vendors differ in what they tolerate.
4. **Known vendor trap:** dynamic indexing of a large local constant array (the PCSS Poisson table)
   expands into per-fragment scratch copies on RADV and can time out the GPU.
   `python3 tests/rendering/run_shadow_shader.py` checks deferred lighting for exactly that; use it
   as the template for similar checks.
5. **Nsight Aftermath is not active.** Its sources exist (`Platform/Vulkan/Debug/`,
   `VulkanDevice.cpp`), but that device path is the legacy one under `#if OLD` in `Window.cpp`; the
   live nvrhi device is created in `VulkanDeviceManager::CreateDevice` without it. Do not wait for
   an `.nv-gpudmp` file. GPU-assisted validation is not wired either. A fault the validation layer
   cannot see needs one of those wired in first — say so rather than guessing.
6. The validation layer's slowdown can hide timing-sensitive faults. A clean validated run is not
   proof of a fix.

---

## Tools

### Force a recompile

Ctrl+Shift+R, or Renderer Debugger → Shaders → Reload / Reload All Shaders. For a truly cold start,
close the editor and delete `Editor/Resources/Cache/Shader/`.

### Offline compile and validate

Reproduces the engine's compile outside the editor. The tools are in `%VULKAN_SDK%\Bin`
(`glslc`, `spirv-val`, `spirv-dis`, `spirv-cross`, `spirv-reflect`).

1. Extract the stage: the text from that stage's `#version` up to the next `#version`, with the
   `#pragma stage : <stage>` line removed.
2. Compile it the way the engine does:

```powershell
glslc -fshader-stage=frag --target-env=vulkan1.2 -Werror `
  -D__GLSL__ -D__FRAGMENT_STAGE__ -D<each global macro>=<value> `
  -IEditor/Resources/Shaders/Include/GLSL -IEditor/Resources/Shaders/Include/Common `
  stage.frag -o stage.spv
spirv-val --target-env vulkan1.2 stage.spv
spirv-dis stage.spv
```

Add `-O` to mirror the optimized runtime binary (not for compute), `-g` to mirror the debug one.
`tests/rendering/run_shadow_shader.py` is a working, scripted example of this recipe. One
difference: the engine's own preprocessor also handles `#pragma stage` inside headers, which plain
`glslc` does not — a header that uses it can compile differently offline.

### Validation layer

- **Debug** builds enable it (`enableDebugRuntime` under `LUX_DEBUG`, `Core/Source/Lux/Core/Window.cpp`).
  `VulkanDeviceManager::vulkanDebugCallback` sends errors to the engine log and the Log panel as
  `Vulkan validation error:` blocks. It skips any location listed in
  `ignoredVulkanValidationMessageLocations` — never add one to quiet a message.
- **Release**, without code changes: force `VK_LAYER_KHRONOS_validation` with Vulkan Configurator
  (`vkconfig-gui` in the SDK) and set its log output. The engine's own debug callback is not
  installed in this mode, so messages go where vkconfig sends them.
- **Release**, via code: temporarily set `deviceParams.enableDebugRuntime = true` in the `#else`
  branch in `Window.cpp`. Revert before finishing — it is a large CPU tax.

### RenderDoc / Nsight Graphics

Launch `bin\<Config>-windows-x86_64\Editor\Editor.exe` with the working directory set to the
`Editor\` source folder (or `-C <repo>\Editor`). Passes appear as named debug markers. Pipelines
run the optimized binary with no debug info, so the shader viewer shows decompiled SPIR-V, not your
GLSL — map it back through the resource names and the pass marker. Compute stages are unoptimized
and read closer to the source.

---

## Finishing

- State the root cause and the evidence for it, and whether the fix was **verified in the running
  editor** or only compiled. A shader that compiles is not a shader that works.
- Remove temporary validation toggles, debug views left pinned, and measurement-time settings in
  `LuxSample.luxproj`.
- If the fix changed a `UB*` struct or added a buffer, the matching GLSL (or C++) side changed in
  the same edit, and the new `(set, binding)` was grepped.
- Run `/cr` before committing. Do not commit from `/shader-debug`.
