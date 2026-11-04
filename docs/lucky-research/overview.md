# LuckyEngine and Lux: implementation study

Reviewed October 23, 2026. This replaces the earlier comparison and its withdrawn May-checkout rankings. **Lux is independently developed and deeply inspired by Hazel; it is not a Hazel fork. LuckyEngine is a Hazel fork.** The useful reference is how a problem was solved, under which constraints, and with which remaining weaknesses.

The strongest opportunities found are **mesh conditioning and imported-node correctness; shader reflection and descriptor identity; bounded command/2D submissions; repeatable profiling; and documentation integrated into the editor**. Lux already has substantial systems that the earlier comparison missed. Its retained scene submission, subresource tracking, current audio stack, prefab variants, and rendering validation tools deserve to be evaluated on their own merits. Lucky's robotics and dataset infrastructure solves different product problems. Temporal techniques are documented for future study, not proposed as current work.

## Reading map

- [Scope, evidence, and source identity](#scope-evidence-and-source-identity)
- [Repository and history map](#repository-and-history-map)
- [Rendering implementation comparison](#rendering-implementation-comparison)
- [Content, editor, build, and user documentation](#content-editor-build-and-user-documentation)
- [Runtime, simulation, ownership, and automation](#runtime-simulation-ownership-and-automation)
- [Reflection, crash recovery, services, and browser code](#reflection-crash-recovery-services-and-browser-code)
- [Decisions for Lux and verification](#decisions-for-lux-and-verification)
- [Coverage limits and research artifacts](#coverage-limits-and-research-artifacts)

## Scope, evidence, and source identity

### Pinned implementations

| Source | Exact identity | How it is used |
|---|---|---|
| Lucky supplied source | `M:/LuckyEngine/MAC-Source`, `pr-142`, `b9ec0f117224f3c4556340d94d827ce7d159f9e7` | Current Lucky implementation for this study; October 1 development snapshot, not a verified release |
| Historical Hazel | Lucky's actual `pr-base`, `d16adf54a8c60b9c43e500123e5ee3f984b56ee9`, version 2024.1.1 | Concrete inherited baseline; does not establish what current private Hazel does |
| Current Lux dev | [`cc25be60385cf342a61030c2b12204c226b94ffa`](https://github.com/starbounded-dev/LuxEngine/tree/cc25be60385cf342a61030c2b12204c226b94ffa) | Main current baseline, including merged foliage work |
| Lux render feature branch | [`7ae13e595bf289b08b6d2f9b9021fed26ef7c00a`](https://github.com/starbounded-dev/LuxEngine/tree/7ae13e595bf289b08b6d2f9b9021fed26ef7c00a) | Separate area-light/refraction/experimental FFT-bloom work; not combined with dev-only foliage |
| Local Lux checkout | `366345f4a34a8ed83f122a22d92e1ee03af87a52` plus pre-existing changes | Old May state; deliberately not used to claim current Lux lacks a system |

The current remote Lux heads were checked during this study. Dev and render-features have 1,120 shared ancestors, 10 dev-only commits, and 5 feature-only commits. Neither contains the other. The [Lux history review](C:/Git/luxengine/docs/Lux-History-Review.md) explains the complete Lux baseline and corrections in more detail.

Lucky's checked-in [Version.h](M:/LuckyEngine/MAC-Source/Hazel/src/Hazel/Core/Version.h:12) identifies 2026.2. Its public documentation was also inspected, but the current stable website is newer. A public help page is evidence of a published user experience, not proof a behavior exists in this particular source snapshot. Likewise, `.claude/docs` contains architecture guidance, while many root `docs` files are proposals or earlier audits. Current call sites and defaults take precedence over stale prose.

### User priorities and comparison rules

Temporal rendering was removed because the prior agent implementation and repeated repair attempts worked badly. It can return later; it is not a priority now. This overrides historical repository text that describes a permanent preference against temporal rendering. Lux's GPU frame slots were Claude's NRI integration choice, not a design the user independently endorsed. They remain open to review against the actual synchronization problem.

Every recommendation should answer: what fails or costs too much; how each implementation works; who owns the data; when it is refreshed; which thread or GPU queue uses it; what is optimized; what is sacrificed; and what evidence would justify changing Lux. A matching dependency or feature name does not answer those questions. Neither an impressive commit message nor an agent's rationale proves a runtime result.

### Evidence classes

**Observed source** means an inspected implementation or call chain at the pins above. **Historical evidence** means a commit message/patch or actual baseline file. **Reported measurement** means a number recorded by Lucky/Lux authors, not reproduced here. **Inference/candidate** means a proposed benefit or a source-level concern that still needs validation. These distinctions are maintained throughout the chapters.

This is a broad static engineering study and full reachable-history index, with deeper inspection of selected implementations and changes. Neither engine was built, run, benchmarked, or graphics-validated. It is not an exhaustive line-by-line audit of every file or historical diff. No engine source, branch, project, authentication state, or external service was changed.

## Repository and history map

### Complete reachable history and file inventory

Lucky has **6,583 reachable commits**, back to initial commit `b72349c783813f724ee05a4afea2d0b4ddd8745e` in October 2018. All recorded parents are present; the repository is not a shallow-history snapshot. The actual Hazel baseline has 2,248 ancestors; 4,335 reachable commits are outside that ancestry. This is a graph difference, **not** a count of Lucky-authored changes after July 2024: older side branches and later Hazel merges cross that boundary. Namespace `Hazel`, author dates, or a recent merge date alone do not establish feature authorship.

The full messages, IDs, parents, author/commit times, and baseline-membership flags are in the [Lucky commit ledger](C:/Git/luxengine/docs/lucky-research/Lucky-Commit-History.json). The existing [Lux ledger](C:/Git/luxengine/docs/Lux-Commit-History.json) contains 1,135 unique current-remote ancestors plus 34 old local-only commits. This makes both histories inspectable without collapsing them into a misleading linear feature timeline.

The supplied Lucky index contains **4,200 tracked entries**, including **24 submodule references**. The non-vendored source/text inventory covers **2,258 files and 579,596 lines**. These counts include generated C#, scripts, configs, tests, and prose; they are not a hand-written C++ size estimate. Working text matched indexed blob bytes or LF-normalized text. A full `git status` attempt hit an LFS transport error, so no claim is made that all assets/submodules form a clean or buildable checkout. See the [file inventory](C:/Git/luxengine/docs/lucky-research/Lucky-File-Inventory.json) and [summary](C:/Git/luxengine/docs/lucky-research/inventory-summary.json).

The checkout's configured `origin` points to `StudioCherno/Hazel`, not a verified canonical Lucky development remote. It was not fetched. README describes a private development repository; top-level license and separately licensed dependencies must be distinguished. This report concerns the supplied implementation, not the public availability or support status of every component.

### System map and depth of investigation

| Area | Actual implementation roles | Study coverage |
|---|---|---|
| `Hazel/src/Hazel/Renderer`, `Platform`, shaders, path-tracing module | NRI objects, state/lifetime, raster/compute, shading, visibility, shadows, post-processing, optional tracing, 2D/text/fonts/debug drawing | Deep selected data flows, defaults, failure-fix history, current Lux comparisons |
| `Asset`, `Scene`, `Project`, serialization | Import/conditioning/cache, asset loading, hierarchy/prefabs, editor/runtime serialization | Deep selected pipelines, correctness and invalidation cases |
| `LuckyEditor`, engine `Editor`, `ImGui`, gizmos | Session/panels, undo, settings, inspectors, content jobs/vault, contextual help, node authoring and material previews | Deep selected workflows; broad layout inventory |
| `Core`, `Module`, `Script`, managed API | Ownership, weak references, workers, clocks, capabilities, input/events, Coral, reload, ABI | Deep selected lifetimes and timing; managed API surface indexed |
| `Physics`, `Robots`, `Animation`, `Audio` | Jolt/MuJoCo, controls/policies, ACL/graphs/root motion, miniaudio/JPL | Selected integration and historical provenance; dependency internals not universally available |
| `Data`, `Lidar`, `Grpc`, `Net` | Sensor/episode timing, metric depth/RGBD/masks, raycast LiDAR and replay, buffers, FFmpeg/Parquet, RPC synchronization | Selected end-to-end control/recording/sensor paths and failure handling; model limits distinguished from calibrated fidelity |
| `LuckyEditor-Agent`, `Resources/AgentDocs`, auth/Hub | Tool dispatch, undo integration, documentation retrieval, network services | Selected host/protocol/knowledge and service lifetimes; no remote service execution |
| Reflection/crash reporter/browser target | Generic reflected assets, watchdog and dumps, reduced MuJoCo browser simulator | Selected implementation traced in the supplemental chapter |
| `UnitTests`, Python `tests`, workflows/build scripts | Headless tests, integration harnesses, captures, CMake/Conan, packaging | Wiring/representative cases inspected; none executed here |
| Launchers/resources/vendors and submodules | Startup/packaging/content/dependencies | Inventory and selected integration; not all binary content or dependency code audited |

### History as a sequence of problems and repairs

| Change in reachable history | What it explains | Lesson for Lux |
|---|---|---|
| Historical Hazel 2024.1.1 baseline | Coral and ACL animation already existed | Do not credit every inherited system as a new Lucky invention or propose a migration Lux already completed |
| `8f5f8fed6`, `9cbf8cdf8`, `0630fa228` | CoreCLR/Jolt process registration split from project/application services; most singleton removal | Separate unavoidable process lifetime from project lifetime; check remaining globals before claiming isolation |
| `74ad2c16b`, `54be9ef2b`, `ab0cbf098` | NRI bring-up and platform integration | Compare completion/state/resource contracts, not wrapper names |
| `8f4112f1e`, `f3a9de2d7`, `0af4c819e` | Multi-rate scheduling followed by removal of duplicate physics accumulators and near-time coalescing | One timing authority and phase ordering matter more than the existence of a fixed-update feature |
| `34a17d486` | Removed stale vendored LuckyLab/Python SDK copies because they fed outdated advice back into coding/docs | Documentation and generated knowledge need clear authoritative sources and version ownership |
| `839395d8b` and shader-local reflection fixes | Hot reload/descriptor identity failures needed concrete lifetime and layout repairs | Avoid global binding-name collisions; preserve old GPU objects until their users complete |
| `19203440e` | WeakRef failure triggered a lazy control block and lock-before-use API | Learn the ownership requirement, but independently prove the concurrent locking algorithm |
| `67209c95e` | Coarse LOD shading/crack fixes changed attribute-aware simplification and cache versioning | Geometry error includes normals and boundaries, not only position or triangle count |
| `de52bfbfe`, `dfba554d5` | Module lifecycle work; explicit layer teardown before reflection registry destruction | Startup capability decisions and reverse lifetime order are part of the implementation |
| `b9ec0f117` | Default-off split-lighting experiment, paired image comparison and profiling controls | Treat an optimization as a measured hypothesis with a reference path; do not promote one GPU's reported win to universal fact |

Detailed mechanisms, selected patches, limitations, and source anchors follow. Commit IDs can be resolved directly against the supplied Lucky repository and the full ledger.
