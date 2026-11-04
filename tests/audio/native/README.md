# Native audio backend checks

Offline checks for the native audio backend (`docs/AUDIO_NATIVE_BACKEND_PLAN.md`): the vendored
miniaudio on its own (Phase 0), and the engine's backend in `Core/Source/Lux/Audio/Native` (Phases 2–3).
They need no audio SDK and no sound card: `ma_engine` runs with `noDevice` and the tests
read its output directly.

```sh
cmake -S tests/audio/native -B build/native-audio -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native-audio
ctest --test-dir build/native-audio --output-on-failure
```

`lux_native_audio_tests` checks:
- WAV, FLAC, Ogg Vorbis and MP3 decoding against the fixtures;
- decoded and streamed playback through the engine;
- resampling to a 44.1 kHz engine;
- 3D panning and distance attenuation (Lux's -Z forward / +Y up / +X right convention);
- the low-pass node used for occlusion;
- the reverb node's tail;
- sample-accurate scheduled starts.

`lux_native_backend_tests` drives the engine's own API (`AudioEngine`, `AudioEventInstance`,
`AudioListener`) and measures the rendered PCM. Only the host is replaced: an offline engine config
and an in-memory Sound table stand in for the project and its asset manager, and mono tones are
written at the test's frequencies. It checks:
- reference resolution (`AudioEventRef`, the Sound handle in decimal) and both YAML forms, including
  byte-identical output when no Sound is assigned;
- Sound YAML version 2 (the node tree, parameters, curves, limits) roundtrip, version 1 migration,
  curve evaluation, and rejection of invalid values and malformed trees;
- panning from the source and listener positions, inverse and linear distance attenuation;
- attenuation targets, normalized listener weights and the neutral listener;
- occlusion gain and its low-pass, the exact bypass when unoccluded, 2D voices unaffected;
- Sound, instance and master bus volume, bus mute and focus mute;
- scene pause layered over script pause, and Start while paused;
- end-of-playback notifications, restart, stop, looping timelines, pitch, unsupported calls, and
  shutdown with wrappers still held;
- Random by weight, avoid-repeat, step sequences shared between instances, Switch by label
  (case-insensitive names, clamped and rounded values), Layers, node volumes down the path, and
  unplayable trees refused;
- volume, pitch, low-pass and high-pass curves, live parameter changes and volume variation;
- instance limits with oldest, quietest and no stealing, and the cooldown;
- streamed Sounds playing through page loads, every frame of a clip played (streamed and decoded),
  seeking a stream, and shutdown with streams open;
- `PhysicsAudioSystem` on native Sounds: a Surface switch, Impulse, Speed and Weight curves, the
  shared scrape loop, impact cooldown and footsteps (the native port of `PhysicsAudioTests.cpp`);
- audio file metadata without an engine.

It also builds `StringUtils.cpp` and the platform `Thread` (the streaming thread). It compiles the
backend's own miniaudio unit (`Audio/Native/MiniaudioImplementation.c`), never the
Phase 0 `lux_miniaudio` library: the engine raises `MA_ENGINE_MAX_LISTENERS`, which changes
miniaudio's struct sizes.

Two programs are run by hand:

- `lux_native_audio_bench [voices=64] [seconds=10]` renders the graph Phases 2–4 will build: every
  voice is 3D with its own low-pass and a splitter, feeding one of four buses and a shared reverb.
  It prints the per-period render cost.
- `lux_native_audio_smoke [file]` plays through the default output device. By default it plays the
  sample project's music, then sweeps a tone from left to right, and prints the backend, the device
  and the engine init time. Use it for the listening check on Windows and Linux.

`fixtures/` holds a half-second stereo tone (440 Hz left, 660 Hz right) in each format.
`make_fixtures.py` regenerates them; it needs ffmpeg, but the tests do not.
