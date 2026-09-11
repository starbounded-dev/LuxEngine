# FMOD demo and project setup

The ready-made scene is `Assets/Scenes/FMODDemo.luxscene`. It uses your existing
`event:/Fart` from `Assets/Audio/SampleProject/SampleProject.fspro`.
The engine already builds with FMOD Core, FMOD Studio, and Vercidium Audio (VA).
You do not need a Unity/Unreal integration package or another audio backend toggle.

## Run the supplied scene

1. Start the rebuilt editor from the repository root:
   ```sh
   LUX_SKIP_BUILD=1 ./scripts/Linux-Run.sh release
   ```
2. Open `Editor/LuxSampleProject/LuxSample.luxproj` if it is not already open.
3. If the editor was running while the scripts were built, stop Play and choose
   **Edit > Reload C# Assembly** (`Ctrl+R`). This reloads the compiled DLL; it does not compile source.
4. Open `Assets/Scenes/FMODDemo.luxscene` from the Content Browser, or use the scene-open dialog.
5. Press **Play**, then click the game viewport to give it keyboard focus. The source plays once
   on startup. The console should report `FMOD Demo ready`.
6. Hold the right mouse button and use **W/A/S/D** to move the listener. **Q/E** move down/up,
   and **Left Shift** increases speed. Walk around the wall and compare near/far positions.

| Key | Action |
| --- | --- |
| K | Restart the Audio Source component's event |
| L | Fire an additional one-shot at the emitter position |
| I | Start/restart an independently owned event instance |
| P | Pause/resume the component's event |
| O | Stop the component and independently owned instance |

One-shots finish naturally; O cannot cancel them. `L` requires an event authored as a finite
one-shot. For looping events, use K or I. Stop Play before rebuilding/reloading banks.

The camera already has `LuxSample.FlyCamera` and an Audio Listener (index 0, weight 1).
The emitter already has `LuxSample.FmodAudioDemo` and an Audio Source. The script's `PlayOnStart`
field controls startup playback; the component's Play On Awake is off to prevent duplicate starts.
The floor and wall have mesh colliders for VA's geometry collection.

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

## VA occlusion and reverb

The wall supplies geometry to VA, but a Studio event must author how VA affects its sound.
Lux supplies optional event parameters named **Occlusion** and **ReverbSend**, each from 0 to 1.
Add those parameters to your event and automate a low-pass/level reduction and reverb send,
respectively. Rebuild its bank afterward. Without that authoring, 3D distance attenuation can work
while the wall does not audibly muffle the event. The K/component path receives scene acoustics;
the L and I paths demonstrate independent playback and do not automatically register VA targets.

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
3. Copy the entire exported folder to the destination machine. Banks are loose files under `Assets`; FMOD and VA libraries ship beside the executable (under `lib` on Linux). Studio itself is not required there.
4. Launch the exported game. Required banks load before script `OnCreate`. This demo's asset-relative `Audio.LoadBank` paths remain valid and loading an already loaded bank succeeds.

If Studio output is outside the project's Assets folder, export places it in `Assets/Audio/Banks`; use `Audio/Banks/<name>.bank` for explicit runtime loads, or rely on startup loading and fire events directly. Dist exports disable live update; Debug/Release follow the project's setting. Older runtime packages must be re-exported to include banks.

## Migrating raw-file audio

Audio Source components now play only FMOD Studio events. If an old scene references a raw audio asset, Lux retains its asset handle and loop flag for migration and displays a warning. Import that sound into FMOD Studio, create an event, assign it to a bank, build banks, and select the event in Lux. Set looping on the Studio timeline. C# `AudioSourceComponent.Play`, `Stop`, volume, pitch and parameter controls continue to work with the assigned event.

## Acoustic materials (Phase 7)

1. Select an entity with a **Mesh Collider** and choose its **Acoustic Material**.
2. Optionally add **Audio Surface** to that entity. Its material takes precedence over the collider
   tag. On an entity without a mesh collider, it is metadata only; it does not create geometry.
3. In **Project Settings → Audio → Acoustic Materials**, expand a material to see its VA base
   preset. Enable **Override Preset** to edit LF/HF absorption, scattering, transmission distances
   (metres), and energy loss through thin/open surfaces. Disabling the override restores the preset.
4. Save project settings, then start a fresh Play session. Geometry and material settings are
   captured at Play start. They are included in runtime exports; moving geometry comes later.
5. Use Studio event parameters `Occlusion` and `ReverbSend` to author the audible response. VA
   measurements alone do not add an FMOD filter or reverb bus. The Audio Debugger shows VA results.

Carpet and Rubber start from VA's Cloth preset; Plastic and WoodThin from WoodIndoor; Default from
Concrete; Plaster from Gyprock; Soil from Mud; Wood from WoodOutdoor; Ceramic from Tile; Foliage from
Leaf. These are starting presets, not measured coefficients for every physical material. Overrides
are independent even when two tags share a base preset.

Scripts can read `GetComponent<MeshColliderComponent>().Material` (including a surface override)
or `GetComponent<AudioSurfaceComponent>().Material`. Footstep and impact playback will use these
tags in Phase 9; changing acoustic geometry during Play belongs to Phase 13.
