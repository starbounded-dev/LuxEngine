# Audio

`public static class Audio`

Gameplay audio. Call on the engine's main thread; handles expire when Play stops.

## Methods

```
public static EventInstance CreateInstance(string eventRef)
```

Creates an event you control: start, stop, move and change it, then dispose it.

- `eventRef`: The event's GUID or path (`event:/...`).

Returns: The new instance, not started yet.

```
public static float GetBusVolume(string busPath)
```

A mixer bus's volume.

- `busPath`: The bus path, `bus:/` for the master bus.

```
public static float GetGlobalParameter(string name)
```

Reads a global FMOD parameter.

```
public static bool LoadBank(string bankFile)
```

Loads a bank immediately. Relative paths start at the project's Assets directory. Call during scene setup; load the master and strings banks before playing event paths.

```
public static bool PlayFootstep(Entity character, float speed, float weight = 75f, float probeDistance = 1.2f)
```

Queries the walkable 3D collider below a character and plays its surface footstep. Returns false when airborne, paused, or no valid event is assigned. Distance is measured from the entity origin.

```
public static void PlayOneShot(string eventRef)
```

Plays an event once, without position, and lets it release itself.

- `eventRef`: The event's GUID or path (`event:/...`).

```
public static void PlayOneShot(string eventRef, Vector3 position)
```

Plays an event once at a world position and lets it release itself.

- `eventRef`: The event's GUID or path (`event:/...`).
- `position`: Where the sound plays.

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

Sets a global FMOD parameter (one every event sees).

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

[All types](index)
