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
Propagation, reflections, echograms and GPU execution are not implemented by this initial contract.

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
groundwork: they are not yet serialized, exposed in the editor or consumed by Scene. Propagation
candidate generation, engine integration and GPU execution remain unfinished.

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

This is a direct measurement, not the complete propagation solver. Reflection/opening candidate
generation and engine settings/Scene integration remain unfinished. The independent direct tests
cover frozen material formulas, solids/overlaps/thin walls, analytic shutters, source exclusion,
affine transforms, invalid data, aliasing and scratch reuse. LuxEngine's production oracle test
also compares all 23 tags, strengths, reversed/inside-solid rays and transformed shutter states.
