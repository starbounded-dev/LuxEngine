# Soundwave

Apache-2.0 acoustic kernel source library. Public headers depend only on the C++20 standard
library; `Lux::Soundwave` does not include engine, physics, renderer or audio-backend headers.
LuxEngine compiles `src/*.cpp` directly into `Core`, without its precompiled header or a new DLL.
The include root is registered in `Dependencies.lua`.

## Current contract

- `MaterialProperties` holds LF/HF absorption, scattering, solid transmission distances and
  one-sided energy-loss fractions. Stable numeric tags select 23 literal default rows.
- `Crossing` describes a material hit in metres along a path, its LF/HF loss in decibels, and
  whether it is one-sided or a portal. Entity IDs remain in the engine adapter.
- `Path::Reset` accepts the caller's positions and distance and retains crossing capacity.
  Add crossings in the intended reduction order; `Finalize` preserves that order, sums the
  bands and caps each loss at 60 dB. `GainLF`/`GainHF` return linear amplitude gains.
- `SolidLossDb` and `FlatLossDb` preserve the original engine's arithmetic. The engine adapter
  sorts walls once before passing them to the library, avoiding a second unstable sort.

`Path` owns CPU storage. It is not a GPU upload layout: neither `std::vector` nor C++ structure
padding is a shader-buffer contract. The GPU implementation is a later migration phase.
This initial contract remains the scalar/direct compatibility layer; the independent CPU solver is described below.

## Verification

From the LuxEngine root, run `python tests/audio/run_soundwave.py` with a C++20 compiler on PATH
(an x64 Visual Studio developer prompt on Windows). These checks need no engine or audio SDKs.
They cover the original scalar formulas, reduction order, loss caps, reuse and material validation.

`python tests/audio/run_occlusion_windows.py` in that Windows prompt additionally compiles the
production engine adapter and its existing material accessor. It compares all seven fields in
every default row bit-for-bit and exercises solid/one-sided walls, portals, scheduling and smoothing.
This migration-only comparison still requires the existing acoustic dependency. Linux runs the
same integration assertions through `python tests/audio/run.py`.

Full engine builds, runtime checks and the migration gates remain separate requirements.

## CPU geometry groundwork (Phase 2 in progress)

`Grid` owns world-space triangles and a deterministic median-split spatial hierarchy. Stable
instance IDs, submesh/surface IDs, material tags and portal flags survive capture. `Build` validates
the snapshot transactionally: invalid geometry does not replace the last valid one. The explicit
limit is 1,048,576 triangles. Zero-area and numerically degenerate triangles are rejected.

`Trace` walks the hierarchy without allocating traversal storage, returns ordered entry/exit
triangle hits, supports source-instance exclusion, and retains the caller's hit-vector capacity.
Use one hit vector per reader. Endpoints are excluded; an optional margin in metres excludes
touching endpoints on reflection legs. Invalid rays return false, not an unobstructed success.
Hits on shared face diagonals are intentionally retained for the later crossing walk to merge.
Triangle intersection uses wide intermediates to avoid overflow on finite large geometry.

`Settings` defines bounded reflection depth, opening samples, bounce/candidate work, direct-path
strength and a fraction of authored material absorption. These types are independent library
types; the engine now serializes them as described below. They are consumed by the engine CPU reference adapter; editor controls remain unfinished. Propagation
engine integration and GPU execution remain unfinished; independent candidate propagation is described below.

The independent runner now also executes `SoundwaveGeometryTests`: analytic solid distances and
winding, shared-edge hits, inside-solid rays, source exclusion, transactional replacement,
deterministic hierarchy coverage, invalid inputs, large geometry and concurrent immutable readers.

## CPU direct measurement (Phase 2 in progress)

`EvaluateDirect` in `Solve.h/.cpp` pairs solid hits per instance, merges shared-edge duplicates
within 1 mm, treats unmatched entry/exit faces as thin surfaces, and applies the existing scalar
material loss arithmetic in a fixed crossing order. The entry material prices a paired solid.
`DefaultMaterials` provides an owned table for project overrides. `PortalBox` carries an applied,
column-major world-to-unit-box inverse, so clipping measures world-space thickness even when
both endpoints are inside a shutter. Matching portal triangles are excluded from duplicate loss.
The engine adapter remains responsible for passing the last applied opening transform.

`EvaluationError` reports invalid settings, materials, rays or portal transforms. Callers must
handle it and retain the last valid result; attempted output is cleared. Endpoint values are copied
before reset, allowing re-evaluation from a Path's own saved endpoints. Reuse `DirectScratch`
per reader and reuse the output Path to retain their vector capacities.

This direct measurement is reused by the independent propagation solver below. Scene now uses the CPU reference adapter; complete Phase 2 verification remains unfinished. The independent direct tests
cover frozen material formulas, solids/overlaps/thin walls, analytic shutters, source exclusion,
affine transforms, invalid data, aliasing and scratch reuse. LuxEngine's production oracle test
also compares all 23 tags, strengths, reversed/inside-solid rays and transformed shutter states.

## Independent CPU propagation (Phase 2 in progress)

`Solve` combines the unchanged direct measurement with bounded indirect candidates. `Grid::Build`
now caches boundary/crease edge adjacency and per-triangle edge indices. Exact coincident vertices
are stitched across instance boundaries; coplanar diagonals/material seams are excluded. Non-manifold
edges are excluded from diffraction and counted in `TopologyStats`. Preparation uses the hierarchy
to select a bounded nearby triangle pool, with explicit truncation diagnostics.

Specular paths use image sources, reconstruct each bounce onto an actual triangle, require the
neighbouring legs on the same side, then test every leg for visibility. Authored LF/HF absorption
and scattering split specular energy. The traversal honors `ReflectionDepth`/`MaxBounces` through
16 interactions; it has no hidden two-bounce cap. Reflection paths do not boost dry occlusion.

Opening candidates sample cached edges at a distance-minimizing apex and the authored grid samples,
with radial offsets into surrounding space. Every candidate requires at least 1 mm clearance from
all triangle surfaces, and every graph leg must be clear. This matters at shared door/wall faces:
two points exactly on opposite faces would otherwise form an undetected segment inside a solid.
Analytic portal volumes and captured triangles both block boolean visibility. A bounded graph
search retains one shortest-distance state per node/depth and rejects repeated edges and unnecessary
bends. It is an approximation, not exhaustive wave propagation or all possible mixed paths.

Edge attenuation uses the Fresnel knife-edge approximation in ITU-R P.526-6 eq. 17, evaluated with
acoustic wavelength (343 m/s, 250 Hz LF and 4 kHz HF). The small-angle excess-path relation supplies
the diffraction parameter; applying successive losses at bends is an approximation for complex
wedges. No radio free-space-loss or terrain/earth model is used. Relative extra-path spreading is
normalized to the source/listener distance with a 1 m near-field reference; the direct signal retains
FMOD's authored distance fade. Only a better visible opening route can raise the dry LF/HF gains.

`SolveResult` retains path kind, points, length, band gains and interaction counts for diagnostics
and the later echogram. The LF route driving dry audibility is retained separately even if path
storage is full. `CandidateBudget`, `PathBudget` and `OpeningNodeBudget` bound work/storage;
`SolveDiagnostics` reports truncation and counts. Reuse `SolveScratch` and result storage per reader.
Invalid numerical work returns an explicit error instead of creating an unoccluded result.

`SoundwavePropagationTests` checks an analytic reflection/energy, absorption/scattering, blocked
legs, third-bounce paths, thick-corner and off-axis doorway audibility, sealed closed-door rejection,
direct-only equality, deterministic results and low-budget behavior. This remains library work:
Editor controls and the complete
Phase 2 runtime gates are unfinished. Echogram, GPU execution, air/caustic reach and SDK removal
remain later phases.

Public mathematical references (no imported implementation):
- https://research.cs.aalto.fi/acoustics/virtual-acoustics/research/room-acoustics-modeling/69-geometrical-acoustics.html
- https://www.itu.int/dms_pubrec/itu-r/rec/p/r-rec-p.526-6-199910-s%21%21pdf-e.pdf

## Static and moving snapshots (Phase 2 integration groundwork)

`GeometryView` borrows one fixed `Grid`, an optional moving `Grid`, and a sorted, unique
instance-exclusion span. It allocates no geometry and renumbers the two hierarchies, triangle
orders and edge references into one deterministic index space. `EvaluateDirect` and `Solve`
accept this view; their original single-Grid overloads remain available. Replacing the moving
snapshot does not rebuild or copy the static hierarchy. An exclusion removes an instance from
traces, clearance tests, reflection/opening preparation and analytic portal clipping immediately.

The caller must own both grids and the exclusion storage, keeping them immutable throughout
each query/solve. Create a fresh view after replacing a grid; cached counts cannot follow a
mutated grid. The same grid supplied twice or unsorted/duplicate exclusions make a view invalid;
solves return `InvalidGeometry`, and queries fail explicitly. Topology is cached within each
grid, not stitched between the two layers; every accepted path is still checked against both.
Raw geometry accessors preserve snapshot data; the solver applies instance masks during queries
and candidate preparation. A deletion can expose additional edges only after topology is rebuilt.

Tests compare composite and flattened direct results bit-for-bit, cover hierarchy/edge index
translation and moving-only scenes, and exercise a separate closed door against its static frame.
Removing the shutter via the exclusion list restores the opening route, including its analytic box.
Scene now owns and prepares these snapshots through SceneAudioAcoustics/AudioAcoustics; full Phase 2 runtime and GPU gates remain separate.

LuxEngine stores these authored values in `Audio.Occlusion.Kernel` with `Version: 1`:
reflection depth, opening grid size, bounce/candidate/path/node limits, absorption and the
temporary `CompareRaycast` diagnostic flag. The existing outer `Strength` row remains authoritative.
`AudioOcclusionSettings::GetKernelSettings` maps them to the standalone value type without
duplicating strength. Missing rows load struct defaults; malformed, unsupported-version, unknown,
duplicate and out-of-range kernel rows are rejected transactionally with a tagged error.
The runtime block remains the existing bounded length-prefixed YAML representation, so old
blocks without `Kernel` still load. Scene now consumes the saved settings for CPU reference playback; editor controls remain unfinished.
