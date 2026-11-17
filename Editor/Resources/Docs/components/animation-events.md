# Animation Events

An animation event marks a moment in a clip: a footstep, the frame a sword connects. While a scene runs and the clip plays, each event fires once as the playhead passes it. The entity's script is told, and the event can play a sound at the entity.

Events are not edited in the editor yet. They live in a text file beside the model, named after it: for `Hero.fbx`, write `Hero.events.yaml`. They are baked into the animation files when you choose **Extract Skeleton and Animations** on the model, so run that again after editing the file.

```yaml
AnimationEvents:
  Version: 1
  Clips:
    - Clip: Walk              # the clip's name in the model
      Events:
        - Time: 0.25          # seconds into the clip
          Name: Footstep
          Argument: left      # optional text your script receives
          Sound: 1234567890   # optional: a Sound asset handle to play at the entity
        - Time: 0.75
          Name: Footstep
          Argument: right
```

## In a script

```csharp
protected override void OnAnimationEvent(string name, string argument)
{
    if (name == "Footstep")
        Log.Info($"step {argument}");
}
```

## Rules

- An event fires once per pass. A looping clip fires it every loop; playing backwards fires events in reverse order.
- Nothing fires while the animation is held (Play On Start off, or a paused scene), or in the editor preview.
- Events of a clip that is fading out do not fire.
- A clip name the file mentions but the model lacks, or an event after the end of its clip, is reported in the Console and skipped.
