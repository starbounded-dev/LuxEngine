# Time

`public static class Time`

The game clock. Scaled values follow [`Time.TimeScale`](Time) (0 freezes, 0.5 is slow motion) and stop while the game is paused; unscaled values always run at real speed. Scripts, physics and animation use the scaled clock; audio and the accessibility overlay use real time.

## Properties

| Name | Type | Access | Description |
|---|---|---|---|
| `DeltaTime` | `float` | get | Seconds since the last frame, scaled by [`Time.TimeScale`](Time): the value `OnUpdate` receives. Inside `OnFixedUpdate` it is the fixed step. |
| `ElapsedTime` | `double` | get | Seconds of scaled game time since Play started. |
| `FixedDeltaTime` | `float` | get | Seconds between physics steps, the step `OnFixedUpdate` receives. Set in the project's physics settings. |
| `FrameCount` | `long` | get | The number of frames updated since Play started. |
| `TimeScale` | `float` | get, set | The speed of game time: 1 is normal, 0 freezes scripts, physics and animation, 2 doubles them. A negative value is treated as 0. It survives a scene load. |
| `UnscaledDeltaTime` | `float` | get | Seconds since the last frame at real speed, ignoring [`Time.TimeScale`](Time). |
| `UnscaledTime` | `double` | get | Seconds of real time since Play started, ignoring [`Time.TimeScale`](Time) and pause. |

[All types](index)
