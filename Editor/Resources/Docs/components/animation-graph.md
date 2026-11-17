# Animation Graph

An animation graph is a small state machine that picks which clip a [Skeleton](skeleton) plays. You set parameters (a speed, a grounded flag, a jump trigger) and the graph moves between states, cross-fading between their clips.

Create one with **Create → Animation Graph** in the Content Browser, then assign it to the **Graph** field of a Skeleton. Double-click the graph asset to open it in Node Canvas.

- Choose **Add State**, select the state, and assign an animation clip. **Set as Entry** chooses the starting state.
- Drag from a state's output to another state's input to create a transition. **Any State** can transition out of whichever state is active.
- Select a link to edit its cross-fade, optional exit time, and conditions. **Earlier** and **Later** change the order in which transitions are checked.
- Use **Parameters** to add Float, Bool, and Trigger values. **Save** validates the draft and updates the graph used by the scene.

In Edit mode, select a Skeleton that uses this graph and expand **Preview Selected Skeleton** in Node Canvas. Play or scrub the clip and change the live parameters to test the saved graph. Preview edits affect only the preview.

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
