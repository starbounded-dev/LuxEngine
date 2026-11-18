# Procedural World Terrain — Discussion Log

**Date:** 2026-11-17
**Status:** Discussion only. No plan was written, no engine code was changed, nothing is scheduled.
This is a point-in-time record of an idea conversation, like the other `docs/*_PLAN.md` files. It is
not architecture. The authority for what exists is `.claude/docs/Architecture-LuxEngine.md`.

---

## 1. How the conversation went

1. Request: "Procedural world-wide terrain generation."
2. I started loading the `plan-le` planning skill and reading old terrain notes. You stopped me:
   **"dont make a plan right now!"** No plan was produced.
3. You chose **"Just discuss ideas"**.
4. I laid out the main design forks (section 4).
5. You set the scope: roughly **10 km²**, as a stress test and showcase of the engine, **not to be
   built now** ("i wont add it right now but yeah why not"). You then fixed it at **3.8 km x 3.8 km**.
6. You asked for papers. I searched terrain synthesis, erosion and rendering (section 5).
7. You asked for vegetation scatter papers and pasted a French reading list. I searched and checked
   the list (section 6).
8. You pasted a second overview of SIGGRAPH 2021–2026 techniques. I checked the named papers and
   compared the techniques with your engine rules (section 7).
9. You asked for this log.

---

## 2. Decided / stated by you

| Item | Value |
|---|---|
| Purpose | Stress test the engine, and a showcase for the future |
| Timing | Not now. Parked |
| Size | 3.8 km x 3.8 km = 14.44 km² (earlier said "10 km² should be enough") |
| Plan | Explicitly not wanted yet |

---

## 3. Standing rules that constrain any future terrain work

These come from `CLAUDE.md` and the saved project notes. They shaped which options were rejected.

- **No temporal rendering.** No TAA, SMAA T2x, or temporal accumulation or reuse of any kind.
  Quality is bought spatially.
- **Ray tracing, terrain and GI restart from scratch.** The earlier attempts were deleted and never
  worked as intended. Do not resume or mine them.
- **Self-contained, small, refined.** No mandatory installs for game makers. Any new dependency must
  be a small permissive library (MIT / BSD / zlib / Apache-2.0) and state its size cost.

---

## 4. Design forks discussed

### 4.1 What "world-wide" means
| Option | Notes |
|---|---|
| Huge flat world (10–100 km) | Covers most games. Needs floating origin only at the large end |
| Infinite flat world | Chunks generated on demand. Physics and world-sized data (rivers, biomes) get harder |
| Planet (sphere) | Cube-sphere quadtrees, double-precision positions, different physics. Only if ground-to-orbit is wanted |

**Lean:** a finite flat world. With your 3.8 km choice this was settled.

### 4.2 Precision at 3.8 km
A float32 position 5 km from the origin is still precise to about 0.5 mm, so a 3.8 km world needs
**no floating origin and no double precision**. (My own arithmetic, not tested in the engine.)

### 4.3 Height data size (own arithmetic)
| Resolution | Texels | 16-bit | 32-bit float |
|---|---|---|---|
| 1 m/texel, 3.8 x 3.8 km | about 14.4 M | about 29 MB | about 58 MB |
| 1 m/texel, 10 x 10 km (the alternative reading of "10 km²") | about 100 M | about 200 MB | about 400 MB |

At 3.8 km the whole height map can stay resident, so **no height streaming** is needed. The old
attempt used about 2 m per texel, which is too coarse for close-up ground.

### 4.4 Generation model
- Make the terrain a **pure function of seed and position**, so any chunk generates independently, in
  any order, with identical results.
- The old attempt kept GPU noise and CPU noise bit-identical for rendering and collision. That was
  fragile. A safer pattern is one source of truth (generate once, everything consumes it).
- **Bake once** (at load, or at export) with GPU compute, keep it resident. Erosion is allowed in the
  bake. The alternative is runtime generation, which allows infinite worlds but is harder to make
  deterministic.

### 4.5 Erosion vs independent chunks
Real hydraulic erosion is non-local, so it fights chunk independence. Two-layer approach:
a **coarse global map** (continents, mountains, river network, biomes) generated and eroded once,
plus **per-chunk detail noise** on top. Rivers need the global map because they cross many chunks.
At 3.8 km the whole world can simply be baked and eroded as one piece.

### 4.6 Rendering
- Clipmap or quadtree LOD with **geomorphing in the vertex shader**, which keeps LOD transitions
  spatial (compatible with the no-temporal rule).
- My old notes say the RHI had no hardware tessellation stage, so assume displaced grids. Needs
  re-checking.
- Materials: blend by slope, height and biome. Triplanar mapping on cliffs. Runtime virtual texture
  and stochastic tiling to hide repetition.

### 4.7 Physics
Stream Jolt heightfield tiles around the player, or use one big heightfield. **Jolt's heightfield size
limits were not checked** and decide which. The old attempt only had collision over a fixed region at
the origin.

### 4.8 Authoring
Generate the base from seed and parameters. Store sculpting and painting as a **delta layer** so edits
survive a re-generate.

### 4.9 Where the engine will really be stressed
1. **Vegetation scatter** (millions of instances, culling, LOD). The terrain itself is the cheap part.
2. Shadow cascades over a huge view distance, without temporal techniques.
3. Collision (Jolt limits).
4. Bake time at startup, and whether to cache it to disk.

---

## 5. Papers: terrain synthesis, erosion, rendering

Read from search summaries and abstracts only, not the full texts.

| Paper | What it gives | Use for Lux |
|---|---|---|
| Schott et al. 2023, [Large-scale terrain authoring through interactive erosion simulation](https://history.siggraph.org/learning/large-scale-terrain-authoring-through-interactive-erosion-simulation-by-schott-paris-fournier-guerin-and-galin) (ACM TOG 42(5)) | Model uplift, run stream-power erosion; fast drainage approximation. Code: [StreamPowerErosion](https://github.com/H-Schott/StreamPowerErosion) (license not checked) | Best match for "bake a whole eroded world" |
| Cordonnier et al. 2016, [Large scale terrain generation from tectonic uplift and fluvial erosion](https://www.cs.purdue.edu/homes/bbenes/papers/Cordonier16CGF.pdf) | Original uplift + stream-power idea | Background for the 2023 paper |
| Génevaux et al. 2013, [Terrain generation using procedural models based on hydrology](https://www.cs.purdue.edu/homes/bbenes/papers/Genevaux13ToG.pdf) | River network first, then blend procedural terrain and carved river patches | Consistent rivers across the map |
| Guérin et al. 2017, [Erosion meta-simulation with a cGAN](https://www.cs.purdue.edu/homes/bbenes/papers/Guerin17ToG.pdf) | Neural network applies erosion fast | **Skipped**: needs a trained model, against the self-contained rule |
| Paris et al. 2019, [Survey](https://people.cs.uct.ac.za/~Jgain/wp-content/papercite-data/pdf/paris2019.pdf) | Groups methods: procedural / erosion simulation / example-based | Orientation |
| Smelik et al., *A Survey of Procedural Methods for Terrain Modelling* | General survey | Surfaced by search, not opened |
| Losasso and Hoppe 2004, [Geometry clipmaps](https://hhoppe.com/proj/geomclipmap/); Asirvatham and Hoppe 2005, [GPU Gems 2 ch. 2](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry) | Nested regular grids centred on the camera, heights from a texture | The old attempt's technique; explains ring/transition problems |

Remark: full erosion simulation is expensive and does not scale to very large terrains per frame. At
14 km² it is affordable as a one-time bake.

---

## 6. Vegetation scatter, plus the French reading list

### 6.1 Papers and talks found
| Source | What it gives |
|---|---|
| Cordonnier et al. 2017, [Authoring landscapes by combining ecosystem and terrain erosion simulation](https://cs.purdue.edu/homes/bbenes/papers/Cordonnier17ToG.pdf) | Plants and terrain shape each other. Offline authoring, not runtime |
| SBGames 2018, [GPU-based procedural vegetation distribution](https://www.sbgames.org/sbgames2018/files/papers/ComputacaoFull/188348.pdf) | Quadtree + hash buffers, GPU parallelism and instancing on large terrains. Closest match to the goal |
| Jahrmann and Wimmer 2017, [Responsive real-time grass rendering](https://www.cg.tuwien.ac.at/research/publications/2017/JAHRMANN-2017-RRTG/JAHRMANN-2017-RRTG-draft.pdf) | Poisson-disk sampling, GPU culling, indirect rendering |
| [Large-scale tiled plant populations](https://ojs.hbz-nrw.de/dipp_ojs/jvrb/article/view/1434) (JVRB 2013) | Wang-tile Poisson discs. Offline, so suits a baked placement map |
| Haar and Aaltonen, SIGGRAPH 2015, [GPU-Driven Rendering Pipelines](https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf) | Cluster-level GPU culling, occlusion depth, indirect draws. The pipeline to copy for trees and rocks |
| Sucker Punch, [Procedural Grass in Ghost of Tsushima](https://gdcvault.com/play/1027214/contactUs) | Talk exists, but no usable summary found. Needs slides or video |
| Octahedral impostors, e.g. [this forest demo](https://discourse.threejs.org/t/a-forest-of-octahedral-impostors/85735); SpeedTree [LOD docs](https://docs.unity3d.com/speedtree-runtime-sdk/manual/level-of-detail.html) | Cheap distant trees. Weak points: transitions and memory. Fade needs MSAA alpha-to-coverage in your engine, unchecked |

No single paper was found for "real-time GPU blue-noise placement with instancing for forests".
The `atomicAdd` stream-compaction technique is my own general knowledge, not from these sources.

### 6.2 French reading list: what I checked
Items 1–5 (Far Cry 5 terrain, CDLOD, Mei GPU erosion, Frostbite shader splatting, Far Cry 4 virtual
textures) were **not** verified. Two corrections:
- **Item 6** (Haar and Aaltonen): the talk is about culling and drawing mesh clusters. The slides I saw
  do not describe scattering grass in a compute shader or compacting instances with `atomicAdd`.
- **Item 7**: the cited GPU Gems 3 chapter is about relief mapping with cone stepping, not mountain
  shadows. Ray-marching a height map toward the sun for long-range terrain shadows is a real
  technique, but that source does not cover it.

The suggested 5-agent split in the paste was only a suggestion. It was not acted on.

---

## 7. Second paste: "SIGGRAPH 2021–2026 taxonomy"

### 7.1 Named papers, verified by search
| Claim in paste | What I found |
|---|---|
| InfiniteDiffusion at SIGGRAPH 2026 | Real: [arXiv 2512.08309](https://arxiv.org/pdf/2512.08309), single independent author, first titled "Terrain Diffusion". **No evidence of SIGGRAPH 2026**; venue unconfirmed |
| "Converting Terrain Images to Heightmaps" | Real paper is [Pixels2Peaks](https://aryamaanjain.github.io/pdf/2026/pixels2peaks.pdf) (Jain, Gain, Cordonnier, ACM TOG 45(4), 2026). Single image to heightmap with hydrological guidance. An authoring aid, not a world generator |
| Gaussian Point Splatting, relightable | Real: [SIGGRAPH'26, arXiv 2605.25975](https://arxiv.org/pdf/2605.25975.pdf). Renders hundreds of millions of Gaussians. **No relighting or materials mentioned** in sources found |
| "Industry moved away from heightmaps" | Overstated. The 2023 and 2026 papers found still output heightmaps |
| Decima / Frostbite hybrids, Nanite details, neural material compression | Not verified |

### 7.2 Techniques in the paste vs Lux rules
| Technique | Verdict | Reason |
|---|---|---|
| Nanite-style virtualized geometry / visibility buffer | Out of scope | Very large project, no payoff at 3.8 km |
| Task and mesh shaders | Possible | My notes mention a mesh-shader prepass in the engine. **Not verified in source** |
| Two-pass Hi-Z occlusion culling | Possible | Useful for vegetation clusters |
| SDF / voxel / dual contouring terrain | Out of scope | Only needed for caves and overhangs. A heightfield plus placed rock meshes covers cliffs |
| Diffusion / neural terrain, neural material compression (ONNX, TensorRT, cooperative matrix) | Rejected | Mandatory trained weights and inference: against self-contained, small, refined |
| Gaussian splatting | Rejected | Suits scanned content, not procedural terrain |
| Ray queries for mountain shadows | Rejected for now | RT restarts from scratch and no RT render path exists. Heightmap ray marching or cascades need no RT |
| ReSTIR | **Rejected** | Reuses samples over time: breaks the no-temporal rule |
| Radiance cascades | Separate question | Grouped with ReSTIR in the paste; GI also restarts from scratch |
| Runtime virtual texture + stochastic tiling | Keep | Cheap ground materials, hides repetition |

---

## 8. Current short list (if this is ever built)

1. Baked, eroded height map from the stream-power papers (4.4, 4.5, section 5).
2. Clipmap or CDLOD-style renderer with vertex-shader geomorphing (4.6).
3. GPU instancing with cluster culling for vegetation, impostors for distant trees (6.1).
4. Runtime virtual texturing with stochastic tiling for ground materials (4.6).
5. Jolt heightfield collision. Tiles or a single field depends on Jolt's limits (4.7).

---

## 9. Open questions

- Jolt `HeightFieldShape` size limits: one field for 3.8 km, or streamed tiles?
- Does the current renderer already have mesh-shader and indirect-draw infrastructure to reuse?
- Does the RHI expose tessellation or MSAA alpha-to-coverage? (Old notes say no tessellation.)
- Bake at load, bake at export, or cache to disk? Acceptable startup time?
- License of the StreamPowerErosion code, if it is ever adapted.
- Is 3.8 km x 3.8 km final? (Initial figure was "10 km²", which would be about 3.2 km square.)

---

## 10. What was NOT done

- No plan, phases or decisions table were written.
- Engine source was **not** read this session for this topic. The only repo checks were: a text search
  for "terrain" excluding vendor folders (found only documentation mentions, such as in
  `FEATURE_CATALOG_2027.md`, `NEXT_UP_BACKLOG.md` and `README.md`, and no source files), and a
  `git log --grep=terrain` (no commits). A branch listing was cut off after 30 entries, and none of
  those was named for terrain, so "no terrain branches" is **not fully confirmed**.
- The older terrain notes (about 148 days old) describe a deleted attempt. Their lessons, kept here
  only as hints to re-verify: vertex-stage textures need their own declaration and set 1 or higher;
  overlapping full grids per LOD level failed, and rings with exact nesting and edge stitching worked;
  skirts looked bad.
- Papers were read from search summaries and abstracts only.
