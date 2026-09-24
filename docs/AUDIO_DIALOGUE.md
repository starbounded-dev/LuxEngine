# Phase 11: Dialogue and subtitles

Dialogue is a main-thread scene service. FMOD Studio plays speech; game scripts render subtitles.
It works in the editor's Play mode and exported Lux-Runtime projects.

## Set up FMOD and the editor

1. In FMOD Studio, create a finite dialogue event with one **asynchronous programmer instrument**.
   Enable **Cut** on the instrument so stopping the event stops speech. Disable looping and
   persistence. The event must report as a one-shot. Add a spatializer for positional speakers/barks.
2. Create an audio table on a bank and add the recorded speech files. Use distinct audio-table keys
   for every language, for example `en_guard_greeting` and `fr_guard_greeting`, when loading multiple
   languages together. Keys must match FMOD's generated table keys exactly; they are not filenames.
   If language banks use identical keys, load only the selected language bank. Stop dialogue before
   unloading/replacing that bank; bank invalidation cancels pending and active speech.
3. Assign the event to a bank. Build the FMOD project. In Lux Project Settings > Audio, configure
   the FMOD project and bank output directory as described in [FMOD setup](../Editor/LuxSampleProject/FMOD_SETUP.md).
   Rebuild/load the banks so the speech event appears in the picker. Runtime exports include the
   project's configured built banks; no FMOD authoring installation is needed on the player machine.
4. Content Browser > New > **Dialogue Table** creates a `.ldialogue` asset. Select it under
   Project Settings > Audio > **Dialogue Table** and set **Dialogue Language** (default `en`).
5. Expand **Dialogue Lines**, enter a stable key such as `guard.greeting`, and click Add Dialogue
   Line. Choose the speech event, priority and whether other lines may interrupt it.
6. Add a translation for each language. Enter text, optional localized speaker name and the FMOD
   audio-table key. An empty speaker name uses the entity name. Every line needs a translation in
   the table's Default Language. Click **Save Dialogue Table**, then save the project settings.
7. Enter Play. Subscribe to the subtitle events before calling Speak. The table is loaded at scene
   startup; stop and re-enter Play after changing it. The assigned table is packed into runtime exports.

An ordinary finite FMOD event also works: leave Audio Table Key empty and use its authored audio.
This is useful for small projects and for testing subtitle rendering before recording voiceovers.
Localized text and audio keys are resolved together; an unavailable locale falls back to the table's
Default Language. Changing language affects new requests, not already queued lines.

## C# example

```csharp
using Lux;

public class GuardDialogue : Entity
{
    private DialogueHandle line;

    void OnCreate()
    {
        Dialogue.SubtitleShown += ShowSubtitle;
        Dialogue.SubtitleHidden += HideSubtitle;
        Dialogue.SetQueueMode(DialogueQueueMode.Queue);
        line = Dialogue.Speak("guard.greeting", this);
    }

    private void ShowSubtitle(Subtitle subtitle)
    {
        // Feed your game's UI with subtitle.Text, SpeakerName and Handle.ID.
        // SpeakerEntityID and SpeakerPosition allow tracking a moving speaker.
        // IsOffScreen is evaluated against the primary scene camera when emitted.
    }

    private void HideSubtitle(Subtitle subtitle)
    {
        // Remove only the UI entry identified by subtitle.Handle.ID.
    }
}
```

`Dialogue.Bark(key, speaker)` plays a separate positional line. `line.Stop()` requests an authored
fade; `Dialogue.StopAll()` stops all speech immediately. `line.IsActive` includes queued lines;
`Dialogue.IsSpeaking(speaker)` includes current dialogue and barks but excludes queued requests.
A zero handle means rejection, an unavailable event/key, or an intentionally suppressed bark.
Failures are described in the Audio log. Scene/assembly reset clears managed event subscribers and
hides any remaining managed subtitles; do not carry handles across scenes.

## Scheduling and subtitle timing

- **Queue:** one foreground line at a time, highest priority first, FIFO within a priority.
- **Interrupt:** a new line interrupts only when the current line is interruptible and its priority
  is no higher than the new line. Otherwise the new line queues. Validation failure keeps the current line.
- **DropIfBusy:** reject new foreground speech while a line is active.
- The foreground queue holds at most 64 pending lines. Barks bypass it, require interruptible 3D
  events, and allow up to 32 simultaneous speakers. The same nearby bark key is suppressed for the
  configured cooldown; a speaker already talking cannot start a bark. Cooldown history is bounded.
- A shown event follows successful playback (actual sound playback for programmer sounds). A hidden
  event follows completion, explicit stop, replacement, speaker removal, bank invalidation or teardown.
  Pair entries by handle; multiple barks may have subtitles simultaneously. Callbacks run on the main
  thread during the scene update and can enqueue or stop dialogue.
- Duration is the source sound's reported length, or zero when unavailable. It is advisory: authored
  pitch, delays, envelopes and interruptions can change audible time. Use SubtitleHidden to remove text.
- Scene pause pauses dialogue, fades and bark cooldowns. Stop requests still hide their subtitles.
- Subtitle rendering, layout, styling and accessibility controls belong to the game/UI. No text is
  drawn by the audio service. Position/offscreen fields are event snapshots; use SpeakerEntityID to
  follow a moving entity. Without a primary scene camera, IsOffScreen is false.

## Verification

`python3 tests/audio/run.py --output /tmp/lux-audio-tests` builds a disposable FMOD fixture and runs
production dialogue, event and serialization code against the real FMOD SDK using its silent output.
Programmer-sound checks use the SDK's supplied `studio/examples/media` banks. The SDK and examples
must be installed under the repository's configured FMOD vendor directory.

Run `python3 tests/audio/run_managed.py` after building ScriptCore to check managed subtitle payloads,
subscription reset and late-hide deduplication. It uses the cached .NET 9 reference packs from that
build and can run on a newer installed .NET runtime.

New source files require regeneration. On Linux, generate native projects with
`./premake5 gmake2` and managed projects with `./premake5 --os=linux vs2022`, then rebuild Core,
ScriptCore, Editor and Lux-Runtime. On Windows use `scripts\Win-GenProjects.bat`.
