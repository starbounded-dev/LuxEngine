# M4: C# API completion (issue #40)

Point-in-time plan for `feature/csharp-api-completion`. What is built is described in
`.claude/docs/Architecture-LuxEngine.md` §2.7; this file is the order of work and the decisions behind it.

Goal (catalog M4): a script can raycast, change scene, read time, use the maths types, switch things off,
find things and react to the application, without custom native code.

Each checkpoint is one board sub-issue, one commit, one ticked box in the PR, and its issue is closed
(with its roadmap card set to Done) when it is pushed and verified.

## Checkpoints, in dependency order

| Order | Issue | Why here |
|---|---|---|
| 1 | #90 SC2 Basic types: `Quaternion`, `Mathf`, `Time`, `Random`, `Color` | Every later API takes or returns these (hits, rotations, delays). Time scale touches the update loop, so it goes first. |
| 2 | #80 S3 `Entity.Active`, component `Enabled` | Small; the finders in S4 and the queries in P3 need to know what "switched off" means. |
| 3 | #81 S4 Find by name, tag, component; user tags | Adds the user tag to `TagComponent` (serialize, copy, inspector). |
| 4 | #72 P3 Physics queries from C# | Adds a layer mask to the C++ query structs and a `Physics` class. |
| 5 | #91 SC4 More components reachable from C# | Lights, meshes and material parameters, music director, portals. |
| 6 | #79 S2 Scene loading from scripts | Riskiest: swaps the running scene, carries persistent entities with their live scripts. |
| 7 | #92 SC5 Timers and coroutines | Needs `Time` (scale) and `Entity` liveness. |
| 8 | #93 SC6 Application events | Focus, pause, quit, resize. |

## Decisions

- **Time.** `Scene` owns the clock. `OnUpdateRuntime` computes a scaled step (`Time.TimeScale`, default 1) and
  feeds it to scripts, physics and animation; audio and the accessibility overlay keep real time. Scale 0
  stops simulation. `Time.DeltaTime` inside `OnFixedUpdate` is the fixed step.
- **Enabled.** One internal call pair keyed by `typeof(T)` (the same dispatch maps as `HasComponent`). A
  component with no switch (`Enabled`, `Visible`, `Active`) reads as enabled and logs when set off.
- **User tag.** One free-text string per entity (`TagComponent::UserTag`), Unity-style, serialized only when
  non-empty. `TagComponent::Tag` stays the entity name. Finders skip switched-off entities unless asked.
- **Queries.** `Physics.Raycast/RaycastAll/SphereCast/BoxCast/CapsuleCast/OverlapSphere/OverlapBox/OverlapCapsule`
  with a layer mask (bit per physics layer ID) and an ignored entity. They run on the main thread, between
  steps, through the existing `PhysicsScene` queries.
- **Scene loading.** Calls from a script are deferred to the end of the frame (the caller is inside the update
  loop). A request is kept on the running `Scene`; the owner (`RuntimeLayer`, `EditorLayer` in Play) performs
  it. Single load keeps entities marked persistent, with their managed script instances; additive load merges
  a scene into the running one and can be unloaded by scene.
- **Verification.** Managed tests (`tests/scripting`) run ScriptCore against stand-in native calls; the Play
  checks (`tests/play`) run every new API in the real editor and in an exported player; headless harness for
  serializer and scene-copy checks. Windows only (Linux is deferred by the owner).

## Out of scope

Physics honouring switched-off entities (P2), `NativeScriptComponent` switching (S5), input actions (I2),
save-game API (G2).
