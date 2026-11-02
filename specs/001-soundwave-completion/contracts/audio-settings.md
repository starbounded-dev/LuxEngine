# Contract: project audio occlusion block

The only external interface this feature changes is the saved settings block; C# stays the two
floats (`Occlusion`, `ReverbSend`).

```yaml
Occlusion:
  # Source: Engine        # accepted and ignored when present (pre-removal files); never written
  UpdateRateHz: 20
  CastBudget: 32
  Strength: 1
  Kernel:
    Version: 1
    ReflectionDepth: 2
    OpeningGridSize: 8
    MaxBounces: 2
    CandidateBudget: 4096
    PathBudget: 64
    OpeningNodeBudget: 128
    Absorption: 1
    CompareRaycast: false
    ReverbRays: 256
    ReverbBounces: 16
    ReverbReferenceSeconds: 1.5
    AirAbsorptionLF: 0.1   # stage B, dB per 100 m
    AirAbsorptionHF: 1.0   # stage B
```

Rules: missing key → default; unknown key → load error (existing); out of range → load error.
The runtime binary block embeds this YAML (≤ 4 KiB).
