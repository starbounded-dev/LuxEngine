# Headless surface audio regression tests

On Linux, regenerate `Core/Makefile`, build the Release Core objects (for yaml-cpp) and Debug Jolt,
then run from the repository root:

```sh
python tests/audio/run.py
```

Requires `clang++`, the repository's FMOD SDK and `fmodstudiocl`. The runner compiles production
code with Debug assertions, creates a disposable copy of the sample FMOD project in `/tmp`, and
uses FMOD's no-sound output. It does not alter the sample project or require a renderer/audio device.
Logs and artifacts are retained in the printed temporary directory. `--output /tmp/my-audio-tests`
selects a fresh directory. Use `--banks /path/to/Build/Desktop` to reuse fixture banks instead of
running FMOD Studio; those banks must contain `event:/SurfaceHit` (one-shot) and
`event:/SurfaceMotion` (continuous), with local `Speed`, `Weight`, `Surface`, and `Impulse`
parameters ranging from 0 to 10000. `Master.bank` and `Master.strings.bank` are loaded.

Checks cover surface-table YAML/defaults/invalid input, impact cooldowns, compound-contact
coalescing, footstep parameters, pause, event-type validation, bank reload, fade completion,
entity cleanup, real Jolt worker contacts, masses/impulse estimates, sensors, body removal/sleep,
file-stream failures, packed surface-table truncation and asset-pack error propagation. Only application host setup and failing
asset serializers are substituted; playback/contact implementations and SDK calls are real.
