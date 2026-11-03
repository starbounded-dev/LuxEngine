# Contract: audio in an exported game (D2 = a)

## Files

The export carries no bank files, no `fmod*` libraries and no `Assets/Audio/Banks/`. Audio lives
inside `Assets/AssetPack.lap`.

## Runtime project format 31

- Writes no `AudioBankManifest`.
- Readers for formats 17–30 still accept one, and log once: "This export carries FMOD banks, which
  are ignored; re-export to drop them."

## Pack audio entries

Each `AudioFile` entry stores `Offset`, `Size`, `Extension` and `Stream` (see
[data-model.md](../data-model.md)).
- **Non-stream** entries are read into `AudioFile::EncodedData` and registered with
  `ma_resource_manager_register_encoded_data`, as today.
- **Stream** entries are never read whole.

## Virtual file names

The miniaudio resource manager opens packed clips by name through a custom `ma_vfs`:

```text
luxpack/<AudioFile handle><extension>      e.g. luxpack/6962752638689701569.mp3
```

- `open`: looks up the handle in the pack index, opens its own handle to `AssetPack.lap`, and seeks
  to `Offset`.
- `read` / `seek` / `tell`: clamped to `[Offset, Offset + Size)`.
- `info`: reports `Size`.
- Every open stream has its own file handle, because miniaudio's job threads call these
  concurrently.
- Any other name falls through to the default VFS, so loose files keep working in the editor.

## Variants

- **D2 b:** the same entries and names over `Assets/Audio.lap`.
- **D2 c:** no VFS; the clips are copied to `Assets/Audio/...` and opened by path.
- **Chosen D1 = B with D2 a:** banks are lists inside this pack (a `SoundBank` entry plus the path
  table, both in [data-model.md](../data-model.md)). Per-bank files can come later with the same
  entry format.
