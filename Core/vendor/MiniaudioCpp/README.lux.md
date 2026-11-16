# MiniaudioCpp (vendored)

- Upstream: https://github.com/Jaytheway/MiniaudioCpp (Jaroslav Pevno)
- Copied from LuckyEngine's vendored copy (`Hazel/vendor/JPLMiniaudioCpp`, MAC-Source commit
  `04872b529c56395db5263be26f97a9623656363a`, 2026-04-30).
- Licence: ISC, see `LICENSE`.

C++ wrappers over miniaudio's C API: owning handles for engines, sounds and nodes
(`Internal::CResource`), node routing traits, and `TBaseNode<T>` for custom processing nodes. Used
only by the native audio backend (`--audio-backend=native`, `Core/Source/Lux/Audio/Native/`).

Only the wrapper's own sources are copied (`include/MiniaudioCpp/`, and `src/` from upstream's
`src/MiniAudioInterface/`). Its bundled miniaudio, dr_libs, choc and c89atomic are not: it builds
against `Core/vendor/miniaudio` and `Core/vendor/choc`, and its miniaudio implementation unit is
replaced by `Audio/Native/MiniaudioImplementation.c`.

Local changes, each marked `Lux:` in the source:

- `C_ResourceHandling.h` and `VFS.h` include `Lux/Audio/Native/Miniaudio.h` instead of
  `miniaudio/miniaudio.h`, so every unit agrees on the miniaudio configuration macros (they
  change struct sizes).
- `src/` includes its headers as `MiniaudioCpp/...`, matching the include root.
- `EngineNode::SetPitch/GetPitch` use `std::atomic_ref<float>` instead of c89atomic.
