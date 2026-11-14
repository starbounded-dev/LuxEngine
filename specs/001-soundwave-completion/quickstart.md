# Quickstart: validating Soundwave completion

Prerequisite: x64 MSVC developer prompt (`vcvars64.bat`), Python 3.

1. **No Vercidium (stage A)** — move `Core/vendor/VA_RAY` aside, run `scripts\Win-GenProjects.bat`,
   then `powershell -File scripts\Lux-BuildTest.ps1` (Debug, Release, Dist). Expect PASS and no SDK
   warning. Linux: CI `make config=debug Core Runtime`, `tests/audio/run.py`.
2. **Old files load** — `run_occlusion_windows.py` includes a settings block with `Source: Raytraced`;
   it must load and re-save without `Source`.
3. **Library behaviour** — `python tests/audio/run_soundwave.py`: reverb, air absorption (60 m
   courtyard audible), bounce counts.
4. **GPU parity (stage C)** — the headless parity test prints max |ΔdB| over the scenes; expect ≤ 0.5.
5. **Play (needs user OK to launch the editor)** — FMODDemo / Sponza: corner and doorway audible,
   stone room vs corridor reverb, overlay toggled on a selected source.
6. **Export** — export a sample game; its folder has no `vaudionative.*` / `libvaudionative.so`.
