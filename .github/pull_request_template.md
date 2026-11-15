## What changed

<!-- What someone using the engine or the editor will notice, in a sentence or two. -->

## Why

<!-- The problem this solves. Link the issue or board item: "Fixes #123". -->

## Required

Every box must be ticked before this can merge: the "PR checklist" check fails until they are, and "CI result" fails until the Windows builds and tests pass. Tick a box only after doing what it says. If an item truly doesn't apply, tick it and say why on the same line.

- [ ] **Branch:** cut from `dev` as `feature/<slug>` or `fix/<issue>-<slug>`, and targets `dev` (or this is a `dev` → `master` release)
- [ ] **Issue:** "Why" links the issue or project board item this closes, or says there is none
- [ ] **Editor:** I ran the change in the editor (Release), including Play mode if it touches gameplay, scripting, physics or audio, and it does what "What changed" says
- [ ] **Linux:** I built and ran it on Linux, or it can't affect Linux (say why). CI doesn't build Linux for same-repository PRs
- [ ] **Tests:** the headless suites this touches pass (`ctest` for `tests/audio/native`, `tests/audio/run.py`, `tests/audio/run_managed.py`, `tests/rendering`, `tests/runtime`), and new behaviour has a test or a golden scene
- [ ] **Rendering:** golden scenes are unchanged, or the new references are accepted and the reason is below (or: no rendering change)
- [ ] **Standing rules:** no temporal rendering (TAA, temporal upscalers or denoisers), and nothing new a game maker has to install besides the .NET SDK
- [ ] **Docs:** the README ("What works today", "Honest limitations"), `docs/Editor/` and the in-editor help match the new behaviour (or: no user-visible change)

## When it applies

- [ ] **C# API changed:** regenerated the API reference in `Editor/Resources/Docs/api`
- [ ] **File format changed** (project, scene, prefab, material, Sound, bank): old files still load, or there's an upgrade path
- [ ] **New vendored dependency:** its licence is compatible and added to `NOTICE`
- [ ] **Performance change:** before and after numbers with the GPU, driver, resolution, configuration and scene
- [ ] **Visible change:** screenshots or a short recording below

## Notes, screenshots, numbers

<!-- Golden image differences, timings, anything a reviewer should look at first. -->
