# FMOD backend (optional)

Only for builds generated with `--audio-backend=fmod` (the setup checklist's **FMOD audio backend**).
The default build plays Sound assets with the built-in engine and needs none of this; see
`Editor/LuxSampleProject/AUDIO_DEMO.md`. The FMOD backend and this page go away with
`docs/AUDIO_NATIVE_BACKEND_PLAN.md` Phase 10.

In an FMOD build, `Assets/Scenes/AudioDemo.luxscene` plays the same emitters from the FMOD events
beside their Sounds (`event:/Fart` and `MusicAmbiance` in
`Assets/Audio/SampleProject/SampleProject.fspro`), and the demo's keys work the same.

## Link an FMOD Studio project to Lux

1. Use FMOD Studio **2.03.x**, matching the installed FMOD Engine SDK series (2.03.14).
   For this sample, open `Assets/Audio/SampleProject/SampleProject.fspro` in FMOD Studio.
2. Create/select an event and put an audio instrument in it. Confirm it plays inside Studio.
   For position/distance testing, author it as a 3D event with a Spatializer on its output.
3. Right-click the event in Studio's Events browser and choose **Assign to Bank > Browse > Master**.
   Unassigned events are not included in bank builds.
4. In Studio, choose **File > Build...**. With the normal Desktop platform output, verify that
   `Build/Desktop/Master.bank` and `Build/Desktop/Master.strings.bank` exist beside the `.fspro`.
   If you use additional content banks, build those too.
5. In Lux, open **View > Project Settings**, then its **Audio** section. Enter:

   | Setting | Value for this sample | Relative to |
   | --- | --- | --- |
   | Studio Project (.fspro) | `Audio/SampleProject/SampleProject.fspro` | The Lux project's `Assets` directory |
   | Bank Output | `Build/Desktop` | The directory containing the `.fspro` |

   **Bank Output is a directory, not `Master.bank`.** These values are already configured for the sample.
6. Save the project settings and reopen the Lux project. Lux loads the banks on project open.
   The Audio section should show loaded banks and events. **View > Audio Debugger** also shows audio status.
7. To let Lux build banks when you press Play, enable **Rebuild Banks On Play**. **Build Banks Now**
   requires FMOD Studio's command-line executable, `fmodstudiocl`. If Lux cannot find it, either
   build manually in Studio or launch Lux with `LUX_FMOD_STUDIO_CL` set to its absolute executable path.
8. In the demo scene, select **FMOD Emitter - Fart**. Change the script's **EventPath** to your
   event's full path (for example, `event:/SFX/Explosion`). Change **MasterBank** and **StringsBank**
   if using a different project. The script's bank paths are relative to **Assets**, unlike Bank Output.
   If the event lives in another content bank, add an `Audio.LoadBank(...)` call for that file before
   `Audio.CreateInstance(...)`. The script sets the component event on startup.
9. Build changed C# source, then reload the assembly in Lux while stopped:
   ```sh
   Editor/LuxSampleProject/Assets/Scripts/Linux-GenProjects.sh
   dotnet build Editor/LuxSampleProject/Assets/Scripts/LuxSample.csproj -c Release
   ```
   The demo scripts are already generated and built. Re-run generation after adding new script files.
10. Open the demo scene and press Play. FMOD playback uses built banks; the Studio application does
    not need to remain open for ordinary playback.

FMOD's official [quick-start tutorial](https://www.fmod.com/docs/2.03/studio/quick-start-tutorial.html)
explains assigning an event and building the Master/strings banks.

## Optional live connection to Studio

Enable **Live Update** in Lux's Project Settings > Audio, save, then reopen the project so FMOD
initializes with live update enabled. Start Play. In FMOD Studio choose **File > Connect to Game...**
and connect to `localhost` (`127.0.0.1`, default port 9264). This is for tuning the running mix;
regular event playback only needs the bank files. Rebuild banks to preserve authored changes for
future runs. Live update is disabled in Dist builds.

See FMOD's [connection instructions](https://qa.fmod.com/t/how-to-use-the-profiler-in-fmod-studio/11138).

## Occlusion and reverb

The wall supplies geometry to Soundwave, but a Studio event must author how acoustics affect its sound.
Lux supplies optional event parameters named **Occlusion** and **ReverbSend**, each from 0 to 1.
Add those parameters to your event and automate a low-pass/level reduction and reverb send,
respectively. Rebuild its bank afterward. Without that authoring, 3D distance attenuation can work
while the wall does not audibly muffle the event. The K/component path receives scene acoustics;
the L and I paths demonstrate independent playback and receive no scene acoustics.

## If it is silent

- Check the Lux console for the actual bank path/event error.
- `event:/...` paths need the strings bank and must match the authored event exactly.
- The event must be assigned to a bank, and that bank must be built and loaded.
- Press K after a short sound finishes. Make sure the viewport has focus.
- Check the FMOD event/master bus volume and the operating system's audio output.
- Reopen Play after replacing banks so the script recreates its event instance.

## Exporting a standalone game

1. In FMOD Studio, build **all banks** for the platform selected by Lux's **Bank Output** directory (normally `Build/Desktop`).
2. In Lux, use **File > Export Runtime…**. Missing or stale banks stop the export and report what needs fixing.
3. Copy the entire exported folder to the destination machine. Banks are loose files under `Assets`; FMOD libraries ship beside the executable (under `lib` on Linux). Studio itself is not required there.
4. Launch the exported game. Required banks load before script `OnCreate`. This demo's asset-relative `Audio.LoadBank` paths remain valid and loading an already loaded bank succeeds.

If Studio output is outside the project's Assets folder, export places it in `Assets/Audio/Banks`; use `Audio/Banks/<name>.bank` for explicit runtime loads, or rely on startup loading and fire events directly. Dist exports disable live update; Debug/Release follow the project's setting. Older runtime packages must be re-exported to include banks.

## Migrating raw-file audio

Audio Source components now play only FMOD Studio events. If an old scene references a raw audio asset, Lux retains its asset handle and loop flag for migration and displays a warning. Import that sound into FMOD Studio, create an event, assign it to a bank, build banks, and select the event in Lux. Set looping on the Studio timeline. C# `AudioSourceComponent.Play`, `Stop`, volume, pitch and parameter controls continue to work with the assigned event.

## Acoustic materials (Phase 7)

1. Select an entity with a **Mesh Collider** and choose its **Acoustic Material**.
2. Optionally add **Audio Surface** to that entity. Its material takes precedence over the collider
   tag. On an entity without a mesh collider, it is metadata only; it does not create geometry.
3. In **Project Settings → Audio → Acoustic Materials**, expand a material. Enable **Override Preset** to edit LF/HF absorption, scattering, transmission distances
   (metres), and energy loss through thin/open surfaces. Disabling the override restores the preset.
4. Save project settings, then start a fresh Play session. Geometry and material settings are
   captured at Play start. They are included in runtime exports; moving geometry comes later.
5. Use Studio event parameters `Occlusion` and `ReverbSend` to author the audible response.
   Soundwave's numbers alone do not add an FMOD filter or reverb bus. The Audio Debugger shows them.

The defaults are starting presets, not measured coefficients for every physical material.
Overrides are independent per tag, even where two tags share the same default values.

Scripts can read `GetComponent<MeshColliderComponent>().Material` (including a surface override)
or `GetComponent<AudioSurfaceComponent>().Material`. Footstep and impact playback will use these
tags in Phase 9; changing acoustic geometry during Play belongs to Phase 13.
