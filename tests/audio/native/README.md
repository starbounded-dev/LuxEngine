# Native audio backend checks (Phase 0)

Offline checks and a benchmark for the vendored miniaudio (`Core/vendor/miniaudio`), from
`docs/AUDIO_NATIVE_BACKEND_PLAN.md` Phase 0. They need no FMOD SDK, no Asterwave and no sound card:
`ma_engine` runs with `noDevice` and the tests read its output directly.

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

Two programs are run by hand:

- `lux_native_audio_bench [voices=64] [seconds=10]` renders the graph Phases 2–4 will build: every
  voice is 3D with its own low-pass and a splitter, feeding one of four buses and a shared reverb.
  It prints the per-period render cost.
- `lux_native_audio_smoke [file]` plays through the default output device. By default it plays the
  sample project's music, then sweeps a tone from left to right, and prints the backend, the device
  and the engine init time. Use it for the listening check on Windows and Linux.

`fixtures/` holds a half-second stereo tone (440 Hz left, 660 Hz right) in each format.
`make_fixtures.py` regenerates them; it needs ffmpeg, but the tests do not.
