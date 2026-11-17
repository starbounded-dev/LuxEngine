# Animation Graph

An animation graph is a small state machine that picks which clip a [Skeleton](skeleton) plays. You set parameters (a speed, a grounded flag, a jump trigger) and the graph moves between states, cross-fading between their clips.

Create one with **Create → Animation Graph** in the Content Browser, then assign it to the **Graph** field of a Skeleton. A new graph has one empty state; edit the file in a text editor to add parameters, states and transitions. A graph editor panel is planned.

## The file

```yaml
AnimationGraph:
  Version: 1
  Parameters:
    - Name: Speed
      Type: Float          # Float, Bool or Trigger
    - Name: Jump
      Type: Trigger
  DefaultState: Idle
  States:
    - Name: Idle
      Clip: 1234567890     # the asset handle of an animation file
    - Name: Walk
      Clip: 1234567891
      Speed: 1.5           # optional playback rate for this state
      Loop: true           # optional, true by default
  Transitions:
    - From: Idle           # a state name, or Any
      To: Walk
      Duration: 0.25       # cross-fade seconds
      ExitTime: 0.9        # optional: wait until this fraction of the clip has played
      Conditions:          # all must hold
        - Parameter: Speed
          Op: Greater      # Greater, Less, Equal, NotEqual, True, False, Triggered
          Value: 0.1
```

## Rules

- The first transition that matches, in file order, is taken, and only one is taken per frame.
- A **Trigger** parameter is set by a script and cleared when a transition that tests it (**Triggered**) is taken. Use **Triggered** on Trigger parameters only.
- State and parameter names must be unique. A graph with a mistake is not used, and the Console says what is wrong.
- Clips and the skeleton must come from the same model file. The graph's clips are included when you export a game.
