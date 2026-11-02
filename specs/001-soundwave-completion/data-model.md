# Data Model: Soundwave Completion

## AudioOcclusionSettings (project audio block)

| Field | Type | Range | Default | Stage |
|---|---|---|---|---|
| ~~Source~~ | enum | — | — | A: removed; YAML key accepted and ignored on load, never written |
| UpdateRateHz | float | 1–120 | 20 | existing |
| CastBudget | uint | 1–1024 | 32 | existing |
| Strength | float | 0–4 | 1 | existing |
| Kernel.ReflectionDepth / OpeningGridSize / MaxBounces / CandidateBudget / PathBudget / OpeningNodeBudget / Absorption / CompareRaycast | — | existing | existing | existing |
| Kernel.ReverbRays / ReverbBounces / ReverbReferenceSeconds | — | 16–4096 / 1–64 / 0.1–10 | 256 / 16 / 1.5 | Phase 3 |
| Kernel.AirAbsorptionLF | float dB/100 m | 0–20 | 0.1 | B |
| Kernel.AirAbsorptionHF | float dB/100 m | 0–20 | 1.0 | B |

Validation: every row range-checked in `Validate()`; unknown keys rejected (existing rule).

## Per-source acoustic result

`Soundwave::SolveResult` (unchanged shape) + diagnostics: `ReflectionPaths`, `OpeningPaths`,
per-bounce path counts (B), `Stalled` updates (C).

## GPU buffers (stage C)

| Buffer | Content | Lifetime |
|---|---|---|
| Nodes | flattened world BVH nodes | per geometry generation |
| Triangles | world triangles + material/instance | per geometry generation |
| Portals | `PortalBox` array | per geometry generation |
| Requests | listener + per-source position/instance | per update |
| Scratch | per-source search state | sized to budget, reused |
| Results | per-source gains + reverb | per update; copied to readback ring |
