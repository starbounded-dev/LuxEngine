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
