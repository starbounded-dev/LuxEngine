# ParticleSystemComponent

`public class ParticleSystemComponent : Component`

Releases many small quads (smoke, sparks, dust) from this entity: play it, stop it, burst it.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `EmissionRate` | `float` | get, set | Particles released per second. |
| `IsPlaying` | `bool` | get | True while the system is releasing particles or has some in the air, and is not paused. |
| `Looping` | `bool` | get, set | Whether the system starts over when its duration ends. |
| `ParticleCount` | `int` | get | How many particles are alive. |

## Methods

```
public void Clear()
```

Removes every particle at once. It does not stop the system releasing more.

```
public void Emit(int count)
```

Releases `count` particles right now, whatever the rate says (a burst from a script). Past the system's Max Particles the rest are dropped.

```
public void Pause()
```

Freezes every particle where it is. [`ParticleSystemComponent.Play`](ParticleSystemComponent) resumes.

```
public void Play()
```

Starts releasing particles, from the beginning of the system's duration when it had stopped or a one-shot system had ended, and resumes a paused one.

```
public void Stop()
```

Stops releasing particles. Those already in the air live out their life; [`ParticleSystemComponent.Clear`](ParticleSystemComponent) removes them.

[All types](index)
