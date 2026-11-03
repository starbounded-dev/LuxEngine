# Quickstart: verifying the FMOD replacement

Windows only for now. Commands run from the repository root. Steps marked **(editor)** launch the
editor and need the owner's OK for that run.

## Every phase

```bash
python scripts/Win-GenProjects.py --last      # only when files were added or removed
powershell -File scripts/Lux-BuildTest.ps1 -Configurations Debug
cmake --build bin/native-audio-tests --config Debug
ctest --test-dir bin/native-audio-tests -C Debug --output-on-failure
```

Expected: the build reports PASS, and all native tests pass.

## Phase 0: baseline

1. **(editor)** Export the sample:
   `python tests/rendering/golden_run.py --config debug --label base --scenes AudioDemo --export-dir bin/fmodless-base/AudioDemo`.
2. Run the export for 30 s. Record clip memory and the first-play frame time from its log and the
   Audio Debugger figures in `report.md`.

## Phase 1: Sound Banks

- The new native test "sound banks" passes. Two banks: memory rises on load and falls on unload, a
  shared clip survives unloading one bank, and a clip still playing is not freed (SC-003).
- **(editor)** Make a bank, add the demo Sounds, list it in AudioDemo's scene settings; Play logs no
  "played outside any loaded bank".

## Phase 2: streaming

- The new native test "pack stream" passes: a 5-minute clip streamed through the VFS stays under
  1 MB of audio memory (SC-004).
- **(editor)** Export again. The exported music plays and the log shows no "not a Sound asset" or
  decode errors.

## Phase 3: legacy data

- `python tests/yaml/run.py`: AudioDemo, Benchmark and the registry match the re-recorded lines.
- **(editor)** Open a copy of the pre-Phase-9 sample and run "Remove FMOD data". The report lists
  each FMOD reference, and afterwards there are none (SC-006).

## Phase 4: script API

```bash
dotnet build Editor/LuxSampleProject/Assets/Scripts/LuxSample.csproj -c Release
dotnet run --project tools/ScriptApiDocs -c Release -- Editor/Resources/Scripts/ScriptCore.dll Editor/Resources/Docs --check
```

## Phase 6: FMOD deleted

```bash
git grep -n -i fmod -- . ':!docs/*_PLAN.md' ':!specs' ':!docs/Lux-Commit-History.json'
```

Expected: only legacy readers and migration messages (SC-001). A fresh clone with default options
builds (SC-002).

## Phase 8: end to end

**(editor)** Play AudioDemo and Benchmark, export, and run the export in Lux-Runtime. The owner
listens. Record SC-001 to SC-006 in `report.md`.
