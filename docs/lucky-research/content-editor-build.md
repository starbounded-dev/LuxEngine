# LuckyEngine investigation: content, scenes, editor, documentation, delivery and verification

Research date: October 23, 2026. Read-only source/history investigation; no engine was built, launched or benchmarked. This chapter is input for the consolidated comparison, not an implementation plan approved by the user.

Lucky baseline: `b9ec0f117224f3c4556340d94d827ce7d159f9e7`, local `M:/LuckyEngine/MAC-Source`. Lux baseline: remote dev `cc25be60385cf342a61030c2b12204c226b94ffa`, read through GitHub's file connector. The local Lux checkout is older and was not used as a claim about current behavior. `feature/render-features` is separate and is not silently combined with dev. Historical Hazel comparisons below use `pr-base` (`d16adf54a`, Hazel 2024.1.1), not an assertion about current proprietary Hazel.

The strongest findings are **how** geometry is simplified and partitioned, how imported scene instances are represented, how changes and undo are owned, and how help is attached to the exact editor context. Lucky is not uniformly ahead: its async and runtime-pack paths have visible shortcomings, and Lux has valuable retained rendering, runtime export hardening, prefab variants and value-based scoped undo that should be preserved.

## 1. Documentation is part of the product's interface

### The problem

Knowing a component exists does not tell a user how to use it. A generic Help link forces the user to identify the right subsystem, locate the right page and guess whether its examples match the installed engine. The same discovery problem affects an assistant trying to author a working scene.

### How Lucky connects the editor to human documentation

[Docs.h](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Utilities/Docs.h:12) derives a documentation version from the engine's compile-time major/minor version. [Docs.cpp](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Utilities/Docs.cpp:11) composes `https://docs.luckyrobots.com/<version>/<relative-path>` and opens it using the platform URL helper. There is no embedded website fetch in that implementation and no offline website bundle was found. The current unit assertion expects `2026.2`; the website's current stable alias points to a later release.

The user does not have to navigate through the home page every time:

- [ComponentDocs.h](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/ComponentDocs.h:20) associates a C++ component type with **two independent destinations**: a conceptual/how-to page and a scripting API reference. Empty paths mean no page for that axis. A script component can have both; a transform can have only its API page.
- [SceneHierarchyPanel.h](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/SceneHierarchyPanel.h:293) puts a concept-documentation button on the component header and a separate API action in the gear menu. `if constexpr` omits unsupported actions; it does not discover website availability or validate that a page still exists.
- [ImGuiEx::DocsButton](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/ImGui/ImGuiEx.cpp:3699) centralizes icon drawing, a descriptive hover tooltip, click-to-open and right-click **Copy URL**. Call sites supply context rather than rebuilding URL logic.
- [SettingsPage](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/EditorPanel.h:14) carries optional documentation title/path metadata; [SettingsPanel.cpp](M:/LuckyEngine/MAC-Source/LuckyEditor/src/Panels/SettingsPanel.cpp:273) draws the appropriate button for the selected settings page.
- [SimpleUXWelcome.cpp](M:/LuckyEngine/MAC-Source/LuckyEditor/src/SimpleUX/SimpleUXWelcome.cpp:288), [EditorLayer's Help menu](M:/LuckyEngine/MAC-Source/LuckyEditor/src/EditorLayer.cpp:1323), the [scene hierarchy title](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/SceneHierarchyPanel.cpp:329), the recording panel and gRPC panel provide broader entry points.

This has low frame cost: fixed metadata, a small immediate-mode widget and an OS browser action only when clicked. It also has maintenance costs: path metadata can drift, a major/minor URL does not identify an exact development commit, and an external site needs connectivity. A comment saying an action “never 404s” is intention, not an implemented link check. There are still hardcoded old `docs.hazelengine.com` prefab-help URLs around `EditorLayer.cpp:4251`; the central helper has not removed all old paths.

History shows the design evolving from generic navigation to context-sensitive help: `0d9c6414b8c9cc621397220f16985026d14a7d07` introduced the URL/docs helpers; `d0e787194...` connected component references; `3aa96f2029445a2b177cc6eac62d173d87e02162` split guide/API metadata and added header buttons; `8c358009a...`/`bfb2f5191...` added a prefilled bug-report link. The latter gathers version, OS, CPU, GPU/backend into a URL through `UrlBuilder`, reducing the information users have to transcribe.

### The external manual and the limits of this checkout

The [official 2026.1 scripting documentation](https://docs.luckyrobots.com/2026.1/scripting/) describes a useful authoring split: API signatures/reference are generated from `Hazel-ScriptCore`, descriptions come from C# XML comments, and task guides are written separately. That reduces signature drift while allowing tutorials to explain complete workflows. The site provides version selection. Its generator and deployment project were not found in this checkout; one should not claim the engine repository contains the whole docs publishing system.

The [current public documentation home](https://docs.luckyrobots.com/stable/) groups learning around onboarding, example projects, the editor and simulation/scripting areas. The [get-started guide](https://docs.luckyrobots.com/stable/get-started/) walks from prerequisites to a bundled welcome scene and a first project. These are user-facing learning materials, separate from `.claude/docs` contributor architecture notes.

Version discipline matters here: [current MCP documentation](https://docs.luckyrobots.com/stable/mcp-server/) says the in-editor Agent panel was removed when upgrading from 2026.2, whereas this inspected source snapshot contains that panel. The website is valuable evidence of a documentation method; newer website behavior is not proof of behavior in this older development checkout. Opening the bare `/2026.2` URL failed in the web tool, while versioned pages were searchable. No complete website crawl or browser rendering QA was performed.

### The bundled assistant knowledge system is different

[Resources/AgentDocs/index.md](M:/LuckyEngine/MAC-Source/LuckyEditor/Resources/AgentDocs/index.md:1) is a curated local corpus of component notes, C# class notes and workflow recipes. It includes not just signatures but complete steps such as write → build → attach → assign → Play, plus known pitfalls. The packaging manifest copies `LuckyEditor/Resources` excluding shader sources, so these Markdown files accompany the packaged editor: [packaging-manifest.json](M:/LuckyEngine/MAC-Source/scripts/packaging-manifest.json:44).

[DocsQueryTools.cpp](M:/LuckyEngine/MAC-Source/LuckyEditor-Agent/src/LuckyEditorAgent/Get/DocsQueryTools.cpp:16) implements the actual retrieval mechanism:

1. Resolve topics against the engine resources root, accepting a topic stem or relative topic path. Reject absolute paths and literal `..` components.
2. Return the index for an empty topic.
3. Search topic filenames, categories and path prefixes with an output cap, rather than loading all prose into every request.
4. Read only the requested Markdown file.
5. On a missing topic, compute edit-distance/substring scores and return up to three suggestions.

This is **topic discovery and file retrieval**, not an embedding store and not full-text semantic search. Directory iteration is performed per request; order is filesystem-dependent; category values are a fixed three-item registry. The contributor guide's proposed `conventions/` category is not in the current [DocsCategoryRegistry](M:/LuckyEngine/MAC-Source/LuckyEditor-Agent/src/LuckyEditorAgent/Get/DocsCategoryRegistry.h:12). That is a concrete example of documentation being slightly ahead of implementation.

[AutoInjectDocs.cpp](M:/LuckyEngine/MAC-Source/LuckyEditor-Agent/src/LuckyEditorAgent/LLM/AutoInjectDocs.cpp:37) adds another mechanism: fixed keyword→topic rules, an overview on the first turn, deduplication against earlier injected topics, word boundaries so “rain” does not match “training,” and at most three topics per turn. It injects complete topic text; the limit is a topic count, not a byte/token budget. This is inexpensive and predictable for a small vocabulary, but keyword matching has false positives and does not generalize to every phrasing/language. It belongs to the inspected Agent implementation, not to the public website.

The [knowledge strategy](M:/LuckyEngine/MAC-Source/LuckyEditor-Agent/docs/AGENT_KNOWLEDGE_STRATEGY.md:1) distinguishes universal tool behavior, situational tool descriptions, global instructions, on-demand prose and behavioral evaluations. That is a design document; actual retrieval and auto-injection are verified above. A strong transferable principle is to correct an unsafe or confusing tool contract in code, then document context-specific choices, rather than hoping a large prompt compensates.

### What to adapt for Lux

Lux already has a useful documentation foundation in [docs/Editor/README.md](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/docs/Editor/README.md), with editor architecture, audio, Content Browser, extension guidance, keybindings, panels, theme/font/icon, undo and viewport/camera pages. The opportunity is to connect, verify and package this existing knowledge as a usable product experience, not to claim Lux has no documentation or rewrite all of it from zero.

**Recommended direction, not implemented:** a self-contained Lux learning system grounded in Lux's own APIs and workflows. Reuse the separation of concepts, recipes and generated reference, and the contextual entry points. Do not relabel Hazel's robotics-oriented API pages as Lux documentation.

Use stable topic IDs for components, settings and errors. Resolve a topic ID to packaged Markdown/API pages matching the installed Lux build; the same topic can optionally open an online version. A Help panel can start with a searchable topic index, back/forward navigation, anchor links, code copying and images, without embedding a full web browser. Package the core manual so Help works offline. Expose the same topic IDs to future assistant tools so human help and assistant recipes do not become two inconsistent sources of truth.

Generate type/member signatures from Lux's actual C# assembly/source and keep explanatory XML comments near the member; write task walkthroughs separately. Validate references and compile tutorial snippets against that exact ScriptCore revision. A useful first end-to-end lesson is create project → import mesh → assign material → attach simple controller → Play → export. Each lesson should name prerequisites, expected visible result, stop/error states and the next task. This is more valuable than a giant index of component names.

Acceptance should include installing a relocated package on a machine without the source checkout, opening help from an inspector and settings page, searching offline, copying/running a sample, and rejecting an absent topic gracefully. Online documentation generation/search/offline packaging are proposals; their implementation should be chosen after inspecting Lux's current UI/text rendering utilities, not presumed free because Lucky has Markdown rendering for chat.

## 2. LODs: spatial clustering versus attribute-aware topology simplification

### Same feature label, materially different algorithms

Current [Lux Mesh.cpp](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/Core/Source/Lux/Renderer/Mesh.cpp#L94) builds LODs by quantizing positions into a bounded spatial grid. Vertices in a cell are merged; position, UV, normals, tangents and binormals are averaged. Triangles that collapse to repeated vertices are dropped, and sorted three-index keys remove duplicate triangles. A reduction must save at least roughly eight percent of the source index count. Fixed target ratios are 0.5/0.25/0.125, and generated LODs get distance multipliers 24/48/96. Each generated level appends its own vertices and indices to render geometry; source geometry remains separate for collision and other consumers. Rigged submeshes skip this simplification.

This is simple and self-contained, but grid cells are decided by position rather than topology, material/UV seams or normal discontinuities. Averaging two coincident vertices from different UV islands can change the texture mapping; averaging a hard edge can change shading; spatial merging can change thin geometry. Those are inferred risks of the algorithm, not visual failures reproduced during this investigation. Sorting triangle keys also treats opposite winding as the same geometric triple, another choice worth testing with two-sided meshes.

[Lucky MeshProcessor.cpp](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/MeshProcessor.cpp:92) instead makes geometry conditioning an ordered import-time pipeline:

1. Canonical vertex deduplication; skinned vertices include bone influences in the equivalence streams so distinct skin weights are not merged accidentally.
2. LOD0 triangle reorder for post-transform vertex-cache locality.
3. Optional overdraw reorder with `OverdrawThreshold = 1.05`, allowing a bounded cache-locality tradeoff.
4. Successive simplification from the previous LOD using `meshopt_simplifyWithAttributes`, normal weights of 0.5 per component and border locking. Target ratio is 0.5, per-step geometric error is 0.02 relative to mesh extent, and reduction stops when it stalls at 95% or more of the previous index count.
5. One vertex-fetch remap applied across all levels; LODs share the optimized vertex stream and add index ranges, rather than generating a separate averaged vertex range per level.
6. Build/optimize meshlets and compute bounding/cone data for every eligible LOD.

Defaults and limits are in [MeshProcessor.h](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/MeshProcessor.h:7). Rigged submeshes receive base geometry optimization but not generated LODs or meshlets. This is not a completed skinned-LOD solution.

### History reveals the failure that shaped the method

`67209c95e730b113bd0329b077cf11ee4818e198` changed position-only `meshopt_simplify` into normal-aware simplification. The diff explains coarse table surfaces darkening because collapses smeared side normals across the surviving top triangles. Border locking was added to prevent independently simplified submesh boundaries cracking. The geometry-cache processor version changed from 1 to 2 in the same commit: changing the algorithm without invalidating old processed data would hide the fix.

This matters more than “Lucky has meshoptimizer.” Their first use of the library still produced unacceptable shading. The improvement came from feeding the right attributes and constraints into it, then invalidating cached output. Normal attributes are included; UVs are not explicitly added as weighted simplification attributes here. Border/vertex topology and original vertices help, but do not prove all UV/textured cases are solved.

### What to prefer in Lux

**High-value experiment:** keep Lux's retained scene, per-instance GPU LOD selection and indirect grouping, while replacing or comparing only the import-time geometry producer. A normal-aware, border-preserving simplifier is a stronger candidate for authored assets than purely positional averaging. Preserve the existing source/collision geometry contract. Decide whether shared optimized vertices versus independent LOD vertices actually reduces runtime residency after considering all CPU/GPU copies and runtime serialization.

Test hard-edged tables, UV seams, thin railings, disconnected nearby surfaces, cutout foliage and multiple material boundaries. Compare silhouette/normal/texture error at equal screen size, vertex/index bytes, preprocessing/cache time and GPU timings at equal quality. The current Lucky importer disables generated LODs and meshlets when mesh shaders are unavailable: [AssimpMeshImporter.cpp](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/AssimpMeshImporter.cpp:862). Lux already has a conventional indirect LOD path, so copying that capability gate would unnecessarily remove useful LODs from non-mesh-shader devices.

## 3. Meshlet construction: packing input triangles versus optimizing locality

Current [Lux Mesh.cpp](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/Core/Source/Lux/Renderer/Mesh.cpp#L223) walks input triangles in order, grows one meshlet until it would exceed 64 vertices or 124 triangles, and flushes it. A hash map assigns local vertex slots. Bounds are the mean position plus maximum distance; the backface cone is based on accumulated normalized triangle normals and widest deviation, with a disabled-test sentinel for invalid/spread-out cones. Three byte-sized triangle slots are packed into a 32-bit word.

[Lucky MeshProcessor::BuildMeshlets](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/MeshProcessor.cpp:285) uses `meshopt_buildMeshlets` with a cone weighting parameter, then `meshopt_optimizeMeshlet` and `meshopt_computeMeshletBounds`. It stores compact micro-indices with aligned runs, separate descriptor/bounds arrays and quantized cone data. The limits happen to match Lux; the partitioning/bounds/encoding methods do not.

Sequential packing is cheap and can work well when input triangles are already spatially/cache coherent. Meshoptimizer can search a better partition, trading import work for tighter locality, vertex reuse and culling bounds. Its advantage must be measured on Lux's imported mesh order and target GPUs. A smaller radius/cone is valuable only if it translates into rejected work without false negatives. Mirrored/nonuniform scales and two-sided materials need explicit culling tests. Adapt the producer and encoding deliberately; do not assume Lucky's descriptor layout matches Lux shaders because both say “64/124.”

## 4. Geometry cache: avoid repeated processing without publishing partial garbage

### Mechanism and optimization

[MeshGeometryCache.h](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/MeshGeometryCache.h:13) deliberately caches processed geometry only. Assimp still reads the live node hierarchy, skeleton, materials and animations. This avoids treating an entire imported asset as one stale blob when only geometry conditioning is expensive.

[CacheFilePath](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/MeshGeometryCache.cpp:19) hashes source path, file size, timestamp, processor version and all processing settings into `<project>/Cache/MeshGeometry/<stem>-<hash>.hzm`. The cache can therefore distinguish mesh-shader-capable and fallback processing choices. Cache hits rebuild the triangle cache after loading processed arrays; GPU buffers are created afterward. This saves simplification/meshlet work, not Assimp parse/material loading or GPU upload.

The most valuable correctness method is [TryLoad](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/MeshGeometryCache.cpp:63): validate header/metadata lengths, section offsets/sizes, count prefixes, vertex/index ranges, LOD ranges, meshlet limits/local indices and bone influence counts. Read into **local arrays**, then move them into the imported MeshSource only after all validation succeeds. Corruption becomes a cache miss and ordinary processing starts from intact live import data.

[EditorAssetManagerTest.cpp](M:/LuckyEngine/MAC-Source/UnitTests/src/Asset/EditorAssetManagerTest.cpp:255) corrupts count words, indices, meshlet offsets, meshlet vertex data and truncates the file, asserting both rejection and preservation of the already imported mesh. This tests a real failure contract, not merely save/load symmetry. The tests were read, not run.

### Limits worth improving before copying

The hash is a metadata key, not a content-addressed dependency graph. A glTF can keep its JSON size/mtime while its external `.bin` changes; importer flags/version are not independently represented. This can cause stale processed geometry in principle. [Store](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/MeshGeometryCache.cpp:272) writes directly to the final path and removes other cache files sharing the same stem. Two assets in different directories with the same filename stem can evict one another's entries. Different capability/settings variants can likewise replace one another. These primarily cause cache churn; concurrent access/crash behavior was not exercised.

Current Lux's geometry generation is in Mesh.cpp, not the guessed `Asset/MeshProcessor.cpp` or `Asset/MeshGeometryCache.cpp` paths (both 404 at the pinned revision). That observation alone does **not** prove there is no cache elsewhere. Recommendation: first profile import time, then add a dependency-aware cache around the verified geometry producer if warranted. Prefer stable source identity, importer/processor versioning, dependency fingerprints, bounded validation, temporary-file publication and cleanup keyed by full source identity rather than stem. Reuse Lucky's validate-then-publish contract and corruption cases.

## 5. Imported node instancing: one geometry can have many placements

### The bug class

Assimp may expose one `aiMesh` referenced by several scene nodes. Storing one transform on that geometry overwrites earlier placements. It is a representation issue: geometry identity and placement identity are distinct.

This remains visible in **current** [Lux AssimpMeshImporter::TraverseNodes](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/Core/Source/Lux/Asset/AssimpMeshImporter.cpp#L484): every node reference assigns `Submesh::Transform`. Current [RenderScene.cpp](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/Core/Source/Lux/Renderer/RenderScene.cpp#L466) emits one retained GPUScene instance per submesh using that transform. The fallback submission similarly multiplies by `submesh.Transform`. Source inspection supports a concrete repeated-node placement problem; it was not reproduced by importing a fixture.

### Lucky's solution and historical evidence

`b9decc839a20d1cd7a967000779d43310eb23c1f` adds a derived placement list per submesh. [MeshSource::BuildSubmeshInstanceTransforms](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Renderer/Mesh.cpp:121) recursively accumulates `parentWorld * local`, appending each referenced geometry's transform. Procedural/unreferenced submeshes fall back to their existing baked transform.

[AssimpMeshImporter.cpp](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/AssimpMeshImporter.cpp:264) builds these lists and encloses **all** placements using transformed AABBs. [SceneRenderer.cpp](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Renderer/SceneRenderer.cpp:3442) loops over node placements and feeds each into its ordinary transform/instance batching. Runtime deserialization rebuilds the derived lists too: [MeshRuntimeSerializer.cpp](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/MeshRuntimeSerializer.cpp:344). The original commit's bounds calculation transformed just min/max corners; HEAD uses `AABB::Transformed`, illustrating why a useful historical fix still needs follow-up scrutiny.

### Fit with Lux

**Recommend a focused correctness adaptation:** retain one geometry/submesh record and represent every imported placement explicitly in Lux's persistent proxies/GPUScene. Include a stable node/placement identity in cached submission and selection data. Invalidate placements and bounds when an asset reload changes its node references. Do not reintroduce Lucky's per-frame full transform rebuilding just to fix the import model.

Test two nodes referencing one mesh, nested rotations, negative/nonuniform scales, runtime pack roundtrip and reimport from N to M placements. Trace collision cooking, picking and editor bounds too: Lucky's renderer fix is verified, but this investigation did not establish that every physics consumer expands the same placements. Lux's acoustic geometry loop also uses `submesh.Transform`, so correct rendering alone would not prove all scene consumers agree.

## 6. Async assets: publication ordering is useful; copying the implementation wholesale is not

### Lucky's main mechanism

[EditorAssetSystem.cpp](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/AssetSystem/EditorAssetSystem.cpp:58) has one asset worker, a mutex/CV request queue, a completed-response vector and a snapshot of main-thread loaded assets. It wakes every 100 ms to poll loaded-asset timestamps. New or reloaded data is placed into the response vector under a lock. Reload events are enqueued under that same lock, and [RetrieveReadyAssets](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/AssetSystem/EditorAssetSystem.cpp:190) marks events ready only after moving completed responses to the main thread. The intent is to ensure a reload event cannot reach subscribers before the replacement asset is visible.

Shutdown is deliberately ordered: stop/join workers, clear worker-held references, then unload manager assets while the manager still exists. The project-session boundary and manager reference make project ownership explicit. [EditorAssetManager.h](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/AssetManager/EditorAssetManager.h:69) documents main-thread-only manager operations and copied, locked metadata access. Those ownership contracts matter more than whether an API has “Async” in its name.

### Explicit weaknesses in the source

The source itself records a race in `GetAsset`: two non-main callers can both miss the loaded/ready checks and load independent copies before either publishes. That can duplicate generated dependent assets and handles. Polling scans loaded assets while holding a long-lived snapshot mutex. `EnsureAllLoadedCurrent` first reads snapshot emptiness before locking. The source also flags project asset-directory mutation and the timestamp window after reading a file. These are not proved runtime races in this session, but they prevent treating the implementation as a generally safe concurrent asset service.

`HasPendingLoads` checks a processing flag, request queue and completed vector. There is a small transition between popping a request and setting the processing flag, and monitor-driven loads have their own timing. It is not a substitute for a generation-aware end-to-end readiness contract.

### Current Lux difference

[Lux EditorAssetSystem.cpp](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/Core/Source/Lux/Asset/AssetSystem/EditorAssetSystem.cpp#L23) uses a blocking worker queue and an atomic pending count incremented on queueing and decremented only when responses are drained. It does not need a 100-ms wakeup for its queue. [Lux EditorAssetManager](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/Core/Source/Lux/Asset/AssetManager/EditorAssetManager.cpp#L108) uses asset loading status to avoid repeating ordinary async requests, dependency/dependent maps and data-change revisions consumed by retained rendering.

Neither design is a full proof of cancellation/reload correctness. A useful Lux audit should track request identity, project/asset generation, reload coalescing, stale completions, dependent invalidation and failed terminal responses. Preserve Lux's pending-through-publication accounting; borrow the explicit ordering of replacement publication before notifying listeners. Do not import periodic locking/polling or duplicate-load windows just because Lucky hot reload appears more elaborate.

## 7. Scene ownership and transform semantics

Lucky's [Scene constructor](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Scene/Scene.cpp:161) receives a `Project&`, binds script storage to that project's engine and uses project-owned asset/renderer/dispatcher services. June history (`b7c371438...`, `4622883f7...`, `d56ec8dfd...`, `3fe793e72...`) progressively removed active-project/static-manager assumptions. Opening a project through an event boundary addresses switching halfway through frame/UI work, not just stylistic dislike of singletons.

[ProjectSession.h](M:/LuckyEngine/MAC-Source/LuckyEditor/src/ProjectSession.h:38) groups editor/runtime/simulation scenes, viewports, panel/layout managers, undo/ImGui context and in-flight scene/script/physics/asset-pack work under one project owner. The project member is declared first so it is destroyed last. The intended lifetime contract is that users of project services die before their owner; all async captures and queued render resources still need to satisfy that contract, rather than member ordering alone proving it. Actual [EditorLayer.cpp](M:/LuckyEngine/MAC-Source/LuckyEditor/src/EditorLayer.cpp:2340) still asserts there is only one session, so this is **preparation for multi-project support**, not proof that multiple open projects already work.

Current Lux still has active-project static access in its importer/asset manager. Session-based ownership is worth adapting if project switching, independent previews or background work expose concrete lifetime bugs. A broad singleton-removal rewrite is not automatically the highest-value task; the testable contract is that work for project A cannot publish into project B after switching.

[TransformHierarchy.cpp](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Scene/TransformHierarchy.cpp:86) also makes a deliberate semantic choice. `ComposeChildWorld` composes translation, quaternion and componentwise scale while keeping a shear-free resolved TRS. `DeriveLocal` uses inverse parent rotation and safe division, retaining existing values on near-zero scale axes. A separate `ResolveWorldMatrix` multiplies matrices through the parent chain and preserves shear. These are different worlds under rotated nonuniform parents. This is not a universal “better transform” algorithm: inspect which renderer/physics/gizmo consumers choose which contract before adopting it. The related `docs/no-shear-transform-hierarchy.md` is design context; source and cross-system invariants decide correctness.

## 8. Prefabs: structural reconciliation versus value propagation

The historical Hazel `pr-base` prefab copies an explicit list of components recursively into a private scene and records a prefab/entity link. Lucky's [CopyableComponents.h](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Scene/CopyableComponents.h:14) centralizes independently copyable types while excluding identity/relationship and order-sensitive bodies/controllers. This reduces duplicated lists, but adding a component still requires understanding separate serialization, copy, diff and runtime initialization rules.

Current [Prefab.cpp](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Scene/Prefab.cpp:172) compares component values by stable component hash, stores per-component overrides and maps prefab source identity to instances. Nested prefabs have separate source/nested-context IDs. [ReconcileChildren](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Scene/Prefab.cpp:1056) matches existing children by nested context first and source identity second, creates missing source children, and removes deleted children while preserving external/nested ownership rules. Changes are propagated through tracked instance roots; flags can suppress update/rebuild behavior. Script storage and runtime object initialization require explicit handling.

Current [Lux Scene::PropagatePrefabEdits](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/Core/Source/Lux/Scene/Scene.cpp#L1365) and `AdoptPrefabBaseEdits` already support override-aware propagation on existing entities. [PrefabSerializer](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/Core/Source/Lux/Asset/PrefabSerializer.cpp#L27) persists a `BasePrefab` link for variants. The history explicitly limits structural/nested variant cascades. Lucky has richer child reconciliation; Lux has a concrete base-variant feature. One is not a strict superset of the other.

**Recommendation:** when structural prefab edits become a goal, study Lucky's identity matching and ownership flags while retaining Lux variants. Specify deleted child versus instance-added child, reparented child, nested-prefab ownership, override granularity, entity-reference remapping and cycle handling before coding. Test changing the prefab during Play separately from editing the source scene. `fd6d91d31...` fixes PrefabManager using the wrong runtime scene—a useful historical reminder that per-scene ownership is correctness, not optional tidiness. `Prefab::GetAssetList` still carries a comment that it has not been tested/updated for the new system, so runtime dependency closure must not be assumed correct merely because editor propagation is sophisticated.

## 9. Undo: direct reversible operations versus scoped value snapshots

[Lucky EditorOperation.h](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/EditorOperation.h:58) defines an `Apply()` operation that becomes its own inverse. `CopyPropertyState` swaps saved/current values through a pointer; `ComponentState` resolves scene/entity UUIDs and swaps/adds/removes a component; entity create/delete stores entity YAML plus script storage; batch operations reverse application order on alternate applications. The [EditorStack](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/EditorOperation.cpp:351) tracks a head, truncates the future on a new edit and applies stored operations for undo/redo. Widget macros coalesce a drag into one operation using a context marker.

This can be much cheaper than serializing a whole scene for a float edit, but it carries pointer and coverage obligations. Raw/copy property states require referenced addresses to remain valid; the header explicitly relies on LIFO restore ordering. `EntitySetParent::Apply` currently obtains `target.Transform()` before its `if (!target)` check, and unchanged parent in one pair returns from the whole batch. These are concrete code-level hazards, not reproduced crashes. There is an explicit TODO to bound stack size, and the header lists incomplete edit categories. The sophistication of a stack does not prove every editor action is undoable.

The parenting history is particularly informative: `05bcb65431d55da2daf4b3c3093f3767f6ab40f0` stores the prior transform matrix with the parent so repeated undo/redo does not accumulate decomposition/recomposition error. The solution is to retain exact state needed for reversal, not to rerun a mathematically approximate inverse repeatedly.

Current Lux uses labelled **per-entity serialized value diffs** with scoped captures, hierarchy-closed patches applied to the live scene and a complete baseline/fallback; settings/material edits can share the stack through closures. [Pinned architecture](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/.claude/docs/Architecture-LuxEngine.md#L556) describes why a roughly 7k-entity scene motivated scoping rather than full-scene serialization on every field edit. Lux already addressed the obvious performance comparison; replacing it with raw-pointer snapshots is not an established improvement.

**Prefer preserving Lux's value/lifetime safety**, measuring scoped capture/patch cost and testing coverage. Borrow exact transform restoration and clearly ordered compound operations where relevant. Lucky's [ExecuteSceneMutation](M:/LuckyEngine/MAC-Source/LuckyEditor/src/EditorLayer.cpp:3586) is a strong boundary for future external tools: verify main thread, focused editor scene and Edit mode before invoking a mutation with an explicit session undo stack. Tools must not obtain history from whichever ImGui context happens to be current.

The [operation tests](M:/LuckyEngine/MAC-Source/UnitTests/src/Editor/EditorOperationTest.cpp:31) cover LIFO, empty boundaries, redo, future truncation and forward/reverse batching. Those tests do not establish pointer lifetime or complete scene-editor coverage.

## 10. Serialization and runtime packs: distinguish fast access from robust delivery

Lucky retains yaml-cpp human-readable scene/project/prefab authoring with large explicit component serialization and `EntityDomain` filtering for client/server. Lux already migrated to rapidyaml with typed compatibility checks; copying the parser choice is not an improvement. Compare preservation of unresolved scripts, references, defaults and failure behavior, not parser names.

[AssetPackFile.h](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Serialization/AssetPackFile.h:13) uses a per-scene map of handle→typed offset/size. [AssetPackSerializer](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Serialization/AssetPackSerializer.cpp:21) reserves index space, writes app binary and payloads, deduplicates shared assets globally by handle, then seeks back to fill the index. A shared texture referenced by many scenes has repeated index entries but one payload. [AssetPack::LoadAsset](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Serialization/AssetPack.cpp:62) tries the active scene index first and falls back to scanning scene maps.

This is seekable lazy asset access, **not proof of world streaming, chunk residency, memory mapping or I/O prioritization**. [CreateFromProject](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Serialization/AssetPack.cpp:222) discovers scenes and prefab references, adds audio assets broadly, and logs missing assets. Failed scene/asset serialization can continue because the fatal exceptions are commented out. `AssetPack::AddAsset` and `Serialize()` are legacy stubs, while CreateFromProject drives the active writer. Read the call path, not isolated method names.

The [index reader](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Serialization/AssetPackSerializer.cpp:118) checks signature/exact version, then trusts scene counts, maps and offsets much more than the geometry-cache reader does. It returns true after loops without the same bounded section-validation contract. The project runtime format has its own exact version. Binary raw structs/`sizeof` fields need ABI/endian/layout review before cross-platform file portability is claimed. “Builds on three operating systems” is not the same as “one binary asset pack is portable across them.”

[RuntimeAssetManager](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/AssetManager/RuntimeAssetManager.cpp:97) deduplicates ordinary queued requests through a pending handle set and returns placeholders. A notable failure-path gap is visible: [RuntimeAssetSystem::TryLoadData](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Asset/AssetSystem/RuntimeAssetSystem.cpp:87) publishes only successful assets; pending handles are cleared when successful assets are synced. Failed loads therefore lack a terminal completion that clears the pending set, so an ordinary failed async request can remain pending rather than retry/report a terminal state. This is source tracing, not a runtime reproduction.

Lux already has hardened export sequencing, bounded manifests and compatibility work. **Keep explicit export failure reporting and self-contained packaging.** Borrow the useful one-payload/many-index dependency organization only if it improves Lux's actual pack format. Apply the cache's validate-then-publish discipline to runtime data. Test truncation, enormous counts, bad offsets, missing/cyclic prefab dependencies, failed decode terminal state and relocation of an exported game.

## 11. Project jobs versus engine/runtime packaging

These are three distinct operations:

1. **Project sharing:** [ExportProjectJob](M:/LuckyEngine/MAC-Source/LuckyEditor/src/Utilities/Content/ExportProjectJob.cpp:1) zips authored project content; [ImportProjectJob](M:/LuckyEngine/MAC-Source/LuckyEditor/src/Utilities/Content/ImportProjectJob.cpp:190) extracts into staging, locates one project file, rejects an existing destination, copies with exclusions, renames/patches identity and reports failure/progress. This does not itself produce a standalone playable game.
2. **Runtime cooking:** scene-domain dependency discovery, HZAP/HZM/shader packs and project runtime settings feed LuckyRuntime.
3. **Editor/product distribution:** Python platform packagers assemble executables, resources, managed assemblies, libraries, launchers and installers.

[ProjectExportFilters.h](M:/LuckyEngine/MAC-Source/LuckyEditor/src/Utilities/Content/ProjectExportFilters.h:53) centralizes exclusions for create/import/Save As/export: caches, build output, IDE state, autosave shadows, generated script projects and `.git`. This avoids drifting “Save As copies one set, export copies another” policies. Broad directory-name rules can exclude user-authored folders named `bin`/`obj`; test the desired project contract rather than assuming all matching names are disposable.

[packaging-manifest.json](M:/LuckyEngine/MAC-Source/scripts/packaging-manifest.json:44) centralizes common payloads. [Linux-Package.py](M:/LuckyEngine/MAC-Source/scripts/Linux-Package.py:92) resolves shared libraries and rewrites executable RPATHs to `$ORIGIN` locations; [Mac-Package.py](M:/LuckyEngine/MAC-Source/scripts/Mac-Package.py:86) follows `otool` dependency/rpath information, bundles eligible transitive dylibs, rewrites load commands and signs the result. Windows has executable/DLL/resource staging plus ZIP/MSI/MSIX support. Mac documentation explicitly says .NET itself is not bundled; self-contained native files do not eliminate every external prerequisite.

Current [Lux runtime-export architecture](https://github.com/starbounded-dev/LuxEngine/blob/cc25be60385cf342a61030c2b12204c226b94ffa/.claude/docs/Architecture-LuxEngine.md#L614) already protects non-export folders from overwrite, builds missing/stale binaries off-thread, pins clicked settings, locks conflicting editor input and advances packaging in discrete main-thread steps. That ownership-aware main-thread asset read may be safer than moving the whole operation to a worker over mutable project state. Compare snapshot/locking/immutability first; “background export” is not inherently better.

Recommended adaptation is a common manifest and explicit prerequisite/dependency validation where Lux's platform packaging is duplicated. Verify relocated artifacts with source/build/Conan directories unavailable. Do not confuse Lucky's engine installer sophistication with proof that every user runtime-export format is robust.

## 12. Build and verification architecture

Lucky uses CMake for the native engine, editor, runtime, modules and test executables; managed projects are CMake custom targets. Setup handles Conan, submodules, LFS and pinned GPU SDK tooling. Source globs use configure dependencies, and transients generally live under `build/`. Relevant implementations: [root CMakeLists.txt](M:/LuckyEngine/MAC-Source/CMakeLists.txt:1), [Hazel/CMakeLists.txt](M:/LuckyEngine/MAC-Source/Hazel/CMakeLists.txt:1), [cmake/CSharpProject.cmake](M:/LuckyEngine/MAC-Source/cmake/CSharpProject.cmake:1).

The root also contains concrete portability repairs: detect missing Debug-CRT Vulkan libraries before hundreds of linker mismatches, scope Dist→Release imported-target fallback so Conan debug/release helpers are not all pulled into every configuration, use platform-specific unsupported implementations where an SDK feature is unavailable. These mechanisms matter more than selecting CMake as a brand. A migration recommendation for Lux would need build-time measurements and dependency/platform pain that justify it; useful guards can be adopted in the present build first.

[UnitTests/CMakeLists.txt](M:/LuckyEngine/MAC-Source/UnitTests/CMakeLists.txt:23) explicitly enumerates test sources and links real Hazel. [main.cpp](M:/LuckyEngine/MAC-Source/UnitTests/src/main.cpp:257) installs a shared headless engine environment so fixtures can use real scene/project/script/GPU services without creating the full editor window. **Headless here is not necessarily GPU-free.** Vulkan device/shaders/audio initialization is substantial and shared once to lower suite startup cost.

History `ced9b4610...` introduces tests that create real scenes/projects; `d8265a9b0...` adds test timing and shared-environment performance work; `a23aa34a4...` preserves startup failure logs; `98142d3c9...` addresses Linux ASan/NVIDIA initialization compatibility. The [runner](M:/LuckyEngine/MAC-Source/UnitTests/RunUnitTests.py:1) provides filters, stress opt-in, timing, platform setup, failure-log preservation, Windows page heap and coverage hooks. These are implemented workflows; historical commit subjects saying tests passed are not tests independently repeated here.

The [unit workflow](M:/LuckyEngine/MAC-Source/.github/workflows/unit-tests.yml:17) declares Windows/Linux/macOS self-hosted runs, recursive submodules/LFS and failure artifacts. [build.yml](M:/LuckyEngine/MAC-Source/.github/workflows/build.yml:32) exercises multiple configurations. Configuration presence is not proof of current runner availability or passing jobs; no remote CI status was retrieved.

An important inventory warning: `UnitTests/src/HazelMeshFileTests.cpp` exists and contains roundtrip/metadata-size compatibility tests, but it is **not enumerated in the inspected explicit UnitTests target**. Do not count a test file as active coverage without tracing its build registration. Conversely, geometry-corruption tests in EditorAssetManagerTest.cpp are in an enumerated source file. [EditorOperationTest](M:/LuckyEngine/MAC-Source/UnitTests/src/Editor/EditorOperationTest.cpp:31), prefab, serialization, transform hierarchy and script-glue tests provide useful scenarios to study; the mere number of files is not a correctness metric.

**Recommended for Lux:** choose a few end-to-end invariants tied to real regressions—shared-node imports, exact undo parenting restoration, stale asset publication, runtime relocation, invalid pack data and tutorial execution. Prefer real small fixtures and explicit failure artifacts. Separate pure CPU geometry tests from GPU-backed integration tests so preprocessing/cache correctness does not require a graphics driver. Measure shared test-environment cost before treating Lucky's setup as a template for every unit test.

## 13. Decisions suggested by this investigation

| Problem | Better candidate for Lux | Why / boundary |
|---|---|---|
| Finding the right learning material | Contextual topic IDs + versioned generated reference + packaged manual | Reuses Lucky's low-friction help method while satisfying offline/self-contained Lux goals; website pipeline remains to be built. |
| Low-quality simplified geometry | Compare normal-aware topology simplification against Lux's position-grid averaging | More direct preservation of shading/borders; requires textured/thin-geometry tests and screen-error policy. |
| Poor meshlet partitions | Compare meshoptimizer partitions against Lux's input-order packing | Same limits do not imply equal culling/locality; measure at equal visual quality. |
| Repeated source-node instances disappear | Explicit imported placements feeding Lux's retained GPUScene | Lucky provides a small conceptual fix; retain Lux's persistent invalidation/submission advantages. |
| Repeated mesh preprocessing | Dependency-aware geometry cache with atomic publication | Borrow full validation and untouched-live-data fallback; improve Lucky's key/cleanup semantics. |
| Wrong project/history receives async edits | Explicit project/session ownership and validated mutation boundary | Removes ambiguous global context; do not claim multi-project support is already enabled. |
| Missing structural prefab propagation | Study source/nested identity reconciliation | Combine with Lux's existing variants rather than replace them; resolve deletion/override semantics first. |
| Undo cost and lifetime | Keep Lux scoped value diffs, borrow exact reversal invariants | Lucky pointer swaps are cheap but bring lifetime and edit-coverage obligations. |
| Async asset state confusion | Preserve Lux pending-through-sync accounting; audit generations and publication events | Lucky's source has duplicate-load/readiness/failure-path caveats. |
| Portable delivery | Shared manifests + relocated-package verification | Distinguish project ZIP, cooked runtime and editor installer; keep Lux export guards. |

## 14. Node authoring: shared canvas, domain-specific compilation and incomplete caches

### What is actually present in this snapshot

The current tree contains **animation, sound and robot motion graphs** backed by a shared native graph editor. The full tracked-path/source searches for `PodComponent`, `PodGraph`, `ProceduralMesh` and corresponding procedural geometry implementation returned no matches. Later public 2026.3 documentation describes procedural graphs; that must not be projected backward into this 2026.2 checkout. No procedural geometry dependency/rebuild system can be audited from absent source here.

Historical Hazel `pr-base` already includes `NodeGraphEditor`, `NodeEditorModel`, AnimationGraph and SoundGraph files. Lucky reuses that framework for MotionGraph; history `55374b8f0` introduces RobotControllerComponent/MotionGraph in November 2025. It is an inherited authoring method extended to a new domain, not evidence that Lucky invented all three graph systems.

### Shared authoring does not mean a shared runtime

The problem is avoiding three implementations of canvas interactions, typed pins, graph properties, selection and layout while keeping sound, skeletal pose and robot state semantics independent.

[NodeGraphEditorBase](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/NodeGraphEditor/NodeGraphEditor.h:90) is an asset editor over `ax::NodeEditor`. Its [initialization callbacks](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/NodeGraphEditor/NodeGraphEditor.cpp:248) delegate graph/node layout save/load to the model rather than using a global node-editor settings file. Layout positions and view state are JSON strings alongside the asset; they are not the executable graph. [NodeEditorModel.h](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/NodeGraphEditor/NodeEditorModel.h:12) defines node/link access, factories, pin compatibility, serialization and compile/play callbacks. `IONodeEditorModel` adds named inputs/outputs/local variables through `PropertySet` and `choc::value`.

The shared [link validation](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/NodeGraphEditor/NodeEditorModel.cpp:304) checks presence, direction, storage/type compatibility and same-node links. Its `CausesLoop` enum is **not** proof of a general DAG cycle detector: the IO override around line 972 adds a particular same-local-variable check. Domain sorting/validation still needs scrutiny for arbitrary cycles.

Domain models turn editable nodes into runtime **prototypes**: node type/IDs, default-value endpoints, connections and subgraph structures. Factories instantiate actual runtime processors and bind typed endpoints. For example, [MotionGraphFactory.cpp](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Robots/MotionGraphFactory.cpp:636) initializes model-sized buffers/processors and dispatches different value/event/local-variable connection types. [MotionGraphNodeEditorModel.cpp](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/NodeGraphEditor/MotionGraph/MotionGraphNodeEditorModel.cpp:721) validates model assignment, output nodes, factory types, missing links and blend-space geometry before constructing the replacement prototype. “Compile” here is graph validation/order/prototype construction, not automatically a shader compiler or a procedural mesh evaluator.

[MotionGraphEditor::SetAsset](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/NodeGraphEditor/MotionGraph/MotionGraphEditor.cpp:162) reloads a private asset copy for editing, so compiling a candidate does not immediately replace the manager's shared asset. The comment frames discard-without-saving as a future possibility; the copy alone does not prove a complete transactional Save/Revert workflow.

### Invalidation, last good output and cache boundaries

The IO model has a dirty-player state. [Node/link changes](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/NodeGraphEditor/NodeEditorModel.cpp:996) invalidate it; property rename/type changes adjust associated pins/links and invalidate compiled structure. [Property-value handling](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/NodeGraphEditor/NodeEditorModel.cpp:843) distinguishes potentially live parameter changes from changes requiring rebuilt data readers, such as wave assets and local variables. Source TODOs explicitly call out incomplete invalidation for other asset types. This is a small mutation-classification scheme, not incremental recompilation of only dirty subgraphs.

[SoundGraphNodeEditorModel](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/NodeGraphEditor/SoundGraph/SoundGraphNodeEditorModel.cpp:82) sorts the graph, builds a prototype, creates/validates a player, suspends audio processing, rebuilds streaming sources and replaces the preview player. [OnCompileGraph](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/NodeGraphEditor/SoundGraph/SoundGraphNodeEditorModel.cpp:260) clears dirty and saves the new prototype only after compile success; a failed compile retains the prior usable prototype. Motion compilation likewise assigns `motionGraphAsset->Prototype` only after successful construction around line 919. This is the useful publication contract: invalid edits can remain editable without destroying the last good runtime representation.

Sound graphs have a [prototype disk cache](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Audio/SoundGraph/Utils/SoundGraphCache.cpp:36), with storage under the project cache and count-based removal of oldest files. It uses graph filename stems as keys in the inspected model. [Deserialize](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Editor/NodeGraphEditor/SoundGraph/SoundGraphNodeEditorModel.cpp:512) explicitly leaves checking whether the authored graph is newer than the prototype as a TODO. Same-stem collisions, node-definition/schema versioning and dependencies therefore deserve tests; this is not the dependency-aware geometry-cache design from section 4. Motion and animation prototype caching is still commented out/TODO in the inspected construction code. Do not infer caching everywhere from the shared canvas.

The sound preview uses busy waits for `IsSuspended()` without a visible timeout in these paths. That may block the editor if the audio processing handshake cannot complete; the mechanism is verified, while such a hang was not reproduced here. Graph item lookup is largely linear node/pin/link scanning, so a reusable editor framework is not itself proof of large-graph scalability.

History exposes real lifetime/error lessons: `68826ad1bb754ea30595a4005c89b7f5c6ea6dea` catches `choc::json::ParseError` in layout callbacks because that exception does not derive from `std::exception`; malformed node positions must not terminate the editor. `5913daab9765b0337b0aef1ad7b9e06234b05a5d` moves heap-backed pin values out of static descriptor initialization and materializes them only when copying descriptors into live pins, fixing a test crash. These are useful mechanisms to understand before transplanting the framework.

**Choice for Lux:** reuse the architectural separation if visual authoring becomes a concrete goal: shared canvas/model contract, domain validation/compiler, immutable candidate prototype and explicit publish-on-success. Specify stable IDs, schema migrations, cycles, edit/undo coverage and live-parameter versus structural edits first. Do not adopt a robotics graph runtime to obtain a canvas, claim an unverified current Lux gap, or replace Lux's FMOD pipeline with inherited SoundGraph machinery. Validation should cover invalid/cyclic links, node-definition changes, failed compile preserving last good output, graph rename/same-stem cache collisions, missing asset dependencies and closing the editor during preview work. No graph UI was run in this investigation.

## 15. Material authoring is a parameter/texture editor, not a material graph

[MaterialEditorPanel.h](M:/LuckyEngine/MAC-Source/LuckyEditor/src/Panels/MaterialEditorPanel.h:24) and its implementation expose a searchable material strip, live preview, parameter column and texture browser. The inspected implementation edits existing `MaterialAsset` properties/texture slots; it does not create a node graph or compile a custom user shader graph. History `07755f9f2` replaces older material panels in April 2026.

The interesting optimization is **bounded preview work**. [UpdateThumbnailsThisFrame](M:/LuckyEngine/MAC-Source/LuckyEditor/src/Panels/MaterialEditorPanel.cpp:355) prioritizes a dirty/current preview, otherwise generates one missing material-strip image per frame. [Texture thumbnail work](M:/LuckyEngine/MAC-Source/LuckyEditor/src/Panels/MaterialEditorPanel.cpp:379) has a separate disk-read budget, generates at most one uncached image per frame, unloads a full-resolution source that was loaded only for its thumbnail, caches permanent failure to avoid retrying every frame and leaves transient renderer-not-ready failures eligible for retry. Preview state remains dirty until image generation actually succeeds. This avoids eagerly loading/rendering an entire library at once.

[Property editing](M:/LuckyEngine/MAC-Source/LuckyEditor/src/Panels/MaterialEditorPanel.cpp:1384) serializes physical materials, while NIR authoring on memory-only imported materials persists in scene overrides keyed by source name. Mutable reference sliders bypass setters, so NIR values are explicitly synchronized to the render material after live edits. [Project change handling](M:/LuckyEngine/MAC-Source/LuckyEditor/src/Panels/MaterialEditorPanel.cpp:308) drops the thumbnail generator because it holds handles registered in the old project's manager. That illustrates the real invalidation problem: cached images, source-derived overrides, GPU parameter publication and manager ownership have different lifetimes.

Current Lux already has a material editor, private preview rendering and thumbnail caching, as established earlier. Compare thumbnail residency/budgets, invalidation after external texture edits, project switches and failed generation; do not treat preview UI as an absent feature or infer a shader-graph system from the panel's name. Large-library responsiveness and restoration of runtime-visible edits should be measured before recommending a change.

## 16. Coverage and explicit blind spots

Read AGENTS.md/CLAUDE.md, conventions, build/threading guidance and test-specific guidance; inspected implementation files and focused diffs for the mechanisms above. Inspected historical Hazel import/asset/prefab files through `git show pr-base:path`. Retrieved current Lux importer, Mesh.cpp, retained RenderScene/SceneRenderer, editor asset worker/manager, prefab serializer/scene propagation and current architecture excerpts. Reviewed local documentation helpers, inspector/settings integrations, AgentDocs retrieval/injection and public official documentation results.

Not proved: successful builds, renderer performance, visual LOD quality, all import formats/material properties, complete scene serializer migration behavior, every prefab nesting/reference case, pack portability/robustness, every editor mutation's undo coverage, online documentation generator implementation, all website pages, external CI status or shipped-package behavior. Current-Hazel comparisons beyond the historical available snapshot require additional source access. The absence of a guessed filename or a test registration is reported at its actual scope; it is not evidence that an entire capability is absent.
