# Animation test assets

`RiggedSimple.gltf` (glTF-Embedded) is from the Khronos glTF-Sample-Assets repository,
Models/RiggedSimple. © 2017 Cesium, licensed CC BY 4.0
(https://creativecommons.org/licenses/by/4.0/). It has two joints and one animation, and is used
only to verify skeleton and clip import.

`LuxBiped.fbx` (binary FBX), `LuxBiped.gltf` and `LuxBiped.bin` are the engine's own 17-bone biped
with Idle and Walk clips, made by `make_luxbiped.py` (Blender: `blender -b --factory-startup -P
make_luxbiped.py -- <folder>`) and covered by the project's Apache-2.0 license. The FBX and glTF are
the same model, so `SkeletalImportTests.cpp` checks that the two importers agree.
`tests/animation/inspect_model.py <file>` prints how any model imports (skeleton, clips, skin).

`local/` is git-ignored: drop a licensed model there (for example a Mixamo `X Bot.fbx`) and the
tests run extra sanity checks on it. Never commit such files.
