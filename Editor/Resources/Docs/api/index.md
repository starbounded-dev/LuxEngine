# C# API Reference

Everything a script can use from the `Lux` namespace, generated from the scripting assembly the editor ships with. For how scripts fit together, start with [C# Scripts](../scripting/csharp-scripts).

## Entities and assets

- [AssetRef](AssetRef) — The base of script fields that reference an asset.
- [Entity](Entity) — An object in the running scene, and the base class of every entity script.
- [Material](Material) — A material asset.
- [Mesh](Mesh) — A mesh asset.
- [Prefab](Prefab) — A prefab asset, for [`Entity.Instantiate`](Entity).
- [Scene](Scene) — A scene asset.
- [StaticMesh](StaticMesh) — A static mesh asset.
- [Texture2D](Texture2D) — A texture asset.

## Components

- [AudioListenerComponent](AudioListenerComponent) — Where the game hears from.
- [AudioPortalComponent](AudioPortalComponent) — Acoustic shutter and optional link between two AudioZone components.
- [AudioSourceComponent](AudioSourceComponent) — Plays an FMOD Studio event from the entity.
- [AudioSurfaceComponent](AudioSurfaceComponent) — Overrides the acoustic material and surface sounds of the entity's colliders.
- [AudioZoneComponent](AudioZoneComponent) — Scene-owned ambience and snapshot zone.
- [BoxCollider2DComponent](BoxCollider2DComponent) — A box shape for the entity's 2D rigid body.
- [BoxColliderComponent](BoxColliderComponent) — A box shape for the entity's 3D rigid body or character.
- [CameraComponent](CameraComponent) — A camera the game can render from.
- [CapsuleColliderComponent](CapsuleColliderComponent) — A capsule shape (along Y) for the entity's 3D rigid body or character.
- [CharacterControllerComponent](CharacterControllerComponent) — A character that walks on the world instead of being pushed around by it (a Jolt virtual character): it climbs steps and slopes, rides moving platforms and stops at walls.
- [CircleCollider2DComponent](CircleCollider2DComponent) — A circle shape for the entity's 2D rigid body.
- [CircleRendererComponent](CircleRendererComponent) — Draws a circle or ring (2D).
- [Component](Component) — The base of the component classes scripts read and change.
- [MeshColliderComponent](MeshColliderComponent) — A collider shaped like a mesh; also acoustic geometry.
- [RigidBody2DComponent](RigidBody2DComponent) — A 2D physics body (Box2D).
- [RigidBodyComponent](RigidBodyComponent) — A 3D physics body (Jolt).
- [SphereColliderComponent](SphereColliderComponent) — A sphere shape for the entity's 3D rigid body or character.
- [SpriteRendererComponent](SpriteRendererComponent) — Draws a textured or colored quad (2D).
- [TagComponent](TagComponent) — The entity's name.
- [TextComponent](TextComponent) — Draws text in the world with an MSDF font.
- [TransformComponent](TransformComponent) — Position, rotation and scale relative to the parent.

## Input

- [GamepadAxis](GamepadAxis) — A gamepad axis.
- [GamepadButton](GamepadButton) — A gamepad button in the standard layout, the same on every mapped controller.
- [Input](Input) — Keyboard, mouse and gamepad input, read from scripts.
- [KeyCode](KeyCode) — A keyboard key, by its position on a US layout.
- [MouseButton](MouseButton) — A mouse button.

## Math

- [Vector2](Vector2) — A 2D vector of floats: a 2D position, size or direction.
- [Vector3](Vector3) — A 3D vector of floats: a position, direction, scale or Euler rotation.
- [Vector4](Vector4) — A 4D vector of floats, most often an RGBA color with components from 0 to 1.

## Audio

- [Accessibility](Accessibility) — Player audio preferences.
- [AcousticGeometryMode](AcousticGeometryMode) — How a mesh takes part in ray-traced acoustics.
- [AcousticMaterial](AcousticMaterial) — What a surface is made of, for ray-traced acoustics and surface sounds.
- [Audio](Audio) — Gameplay audio.
- [AudioCategory](AudioCategory) — A volume category the player can set, mapped to an FMOD bus in Project Settings.
- [AudioDynamicRange](AudioDynamicRange) — How much the mix's loud and quiet parts are evened out.
- [AudioZoneShape](AudioZoneShape) — The shape of an [`AudioZoneComponent`](AudioZoneComponent).
- [Dialogue](Dialogue) — Scene-owned dialogue.
- [DialogueHandle](DialogueHandle) — A line of dialogue that was asked to play.
- [DialogueQueueMode](DialogueQueueMode) — What [`Dialogue.Speak`](Dialogue) does while another line is playing.
- [Music](Music) — Scene-owned interactive music.
- [MusicSync](MusicSync) — When [`Music.QueueTransition`](Music) switches to the new music.

## Debugging

- [Gizmos](Gizmos) — Debug lines drawn in the world while the game runs, in the editor and in the player (not in Dist builds).
- [Log](Log) — Writes messages to the editor's Log panel and the log file.

## Attributes

- [EditorAssignableAttribute](EditorAssignableAttribute) — Marks a class whose fields the engine stores as an ID (an entity or an asset).
- [HeaderAttribute](HeaderAttribute) — Draws a bold label above the field, to group the fields that follow.
- [RangeAttribute](RangeAttribute) — Draws a numeric field as a slider clamped to [[`RangeAttribute.Min`](RangeAttribute), [`RangeAttribute.Max`](RangeAttribute)].
- [ShowInEditorAttribute](ShowInEditorAttribute) — Shows a private or protected field in the inspector (public fields show already).
- [TooltipAttribute](TooltipAttribute) — Shows help text when the field's label is hovered in the inspector.

## Other

- [CursorMode](CursorMode) — How the mouse cursor behaves over the game window, for [`Input.SetCursorMode`](Input).
- [EventInstance](EventInstance) — An owned Studio event.
- [ForceMode](ForceMode) — How [`RigidBodyComponent.AddForce`](RigidBodyComponent) treats its vector.
- [GamepadTrigger](GamepadTrigger) — The left or right trigger, for adaptive trigger effects.
- [GamepadType](GamepadType) — Which family a gamepad belongs to, to pick its button prompts.
- [SoundCue](SoundCue) — A sound playing near the listener, for drawing visual sound cues.
- [Subtitle](Subtitle) — A subtitle, caption or description line, for drawing your own subtitle UI.
- [TriggerEffect](TriggerEffect) — A DualSense adaptive-trigger effect, built with [`TriggerEffect.Resistance`](TriggerEffect), [`TriggerEffect.Weapon`](TriggerEffect), [`TriggerEffect.Vibration`](TriggerEffect) or [`TriggerEffect.Off`](TriggerEffect).
- [TriggerEffectType](TriggerEffectType) — The kind of a [`TriggerEffect`](TriggerEffect).

