# Audio

`public static class Audio`

Gameplay audio. Call on the engine's main thread; handles expire when Play stops.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `PlaysSoundAssets` | `bool` | get | Always true: the engine plays Sound assets. FMOD is gone. |

## Methods

```
public static void ClearBusVolume(string busPath)
```

Hands a bus back to the mix after [`Audio.SetBusVolume`](Audio): its volume returns to the project mixer's and snapshots apply to it again .

- `busPath`: The bus path, `bus:/` for the master bus.

```
public static EventInstance CreateInstance(Sound sound)
```

Creates an explicitly owned instance of a Sound asset.

```
public static EventInstance CreateInstance(string eventRef)
```

Creates a Sound instance you control: start, stop, move and change it, then dispose it.

- `eventRef`: The Sound's path relative to Assets, with or without `.lsound`.

Returns: The new instance, not started yet.

```
public static float GetBusVolume(string busPath)
```

A mixer bus's volume.

- `busPath`: The bus path, `bus:/` for the master bus.

```
public static float GetGlobalParameter(string name)
```

Reads a global parameter.

```
public static bool IsBankLoaded(SoundBank bank)
```

True once the bank is loaded and every clip it decodes is ready.

```
public static bool LoadBank(SoundBank bank)
```

Loads a Sound Bank: its Sounds' clips decode in the background, and the call returns at once. Loads are counted; match each with [`Audio.UnloadBank`](Audio). Banks the scene lists load on their own.

Returns: False when the bank is unknown.

```
public static bool LoadBank(string bankPath)
```

Loads a Sound Bank by its path relative to Assets, with or without `.lbank` (`"Audio/Banks/Level1"`). FMOD `.bank` files are refused: make a Sound Bank instead.

Returns: False when no Sound Bank has that path.

```
public static bool PlayFootstep(Entity character, float speed, float weight = 75f, float probeDistance = 1.2f)
```

Queries the walkable 3D collider below a character and plays its surface footstep. Returns false when airborne, paused, or no valid event is assigned. Distance is measured from the entity origin.

```
public static void PlayOneShot(string eventRef)
```

Plays a Sound once, without position, and lets it release itself.

- `eventRef`: The Sound's path relative to Assets, with or without `.lsound` (`"Audio/Sounds/Fart"`).

```
public static void PlayOneShot(Sound sound)
```

Plays a Sound asset once. It must not loop.

```
public static void PlayOneShot(string eventRef, Vector3 position)
```

Plays a Sound once at a world position and lets it release itself.

- `eventRef`: The Sound's path relative to Assets, with or without `.lsound`.
- `position`: Where the sound plays.

```
public static void PlayOneShot(Sound sound, Vector3 position)
```

Plays a Sound asset once at a world position. It must not loop.

```
public static void SetBusMuted(string busPath, bool muted)
```

Mutes or unmutes a mixer bus.

- `busPath`: The bus path, `bus:/` for the master bus.
- `muted`: True mutes.

```
public static void SetBusVolume(string busPath, float volume)
```

Sets a mixer bus's volume (1 is unchanged).

- `busPath`: The bus path, `bus:/` for the master bus.
- `volume`: Linear volume multiplier.

```
public static void SetGlobalParameter(string name, float value)
```

Sets a global parameter (one every Sound sees).

```
public static void SetVCAVolume(string vcaPath, float volume)
```

Sets a VCA's volume (1 is unchanged).

- `vcaPath`: The VCA path (`vca:/...`).
- `volume`: Linear volume multiplier.

```
public static EventInstance StartSnapshot(string snapshotRef, float intensity = 1f)
```

Starts an authored snapshot. Expose its Intensity dial as a continuous 0–100 parameter in Studio. The caller owns the returned instance; Stop fades, Dispose stops immediately.

```
public static void UnloadBank(SoundBank bank)
```

Drops one load of a Sound Bank; its clips are freed once no loaded bank or playing voice holds them.

```
public static void UnloadBank(string bankPath)
```

Drops one load of a Sound Bank, named by its path relative to Assets.

[All types](index)
