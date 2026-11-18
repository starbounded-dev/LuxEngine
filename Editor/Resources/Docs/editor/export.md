# Exporting a Game

Exporting builds a folder that runs your game without the editor: the player executable, your scenes and assets packed into an asset pack, and your compiled scripts.

## Before you export

Open **Project Settings** and expand **Runtime Export**:

1. Set the **Startup Scene**, the scene the game opens first.
2. The checklist shows what is ready. If you downloaded LuxEngine as a zip, the player is already included (in the `Runtime` folder next to the editor) and **Build Runtime** is greyed out: it needs the engine source. If you build the engine yourself and **Lux-Runtime.exe** is missing, click **Build Runtime** once.
3. If the game has scripts, **Build Scripts** compiles them. This needs the .NET 10 SDK, which the editor already requires for scripts.

## Export settings

The same settings appear in **Project Settings → Runtime Export** and in the export window:

| Setting | What it does |
|---|---|
| **Game Name** | The window title and the name of the exported folder and executable. On Windows it is also the name Explorer and Task Manager show for the executable. |
| **Version** | Up to four numbers separated by dots, such as `1.2.0`. Shown in the executable's file properties on Windows. |
| **Window Width**, **Window Height** | The window size the game opens with (at least 320 × 240). |
| **Fullscreen** | Opens the game fullscreen instead of in a window. |
| **VSync** | Waits for the display's refresh, which avoids tearing and caps the frame rate. |
| **Icon** | An optional PNG or JPEG. It is the game window's icon and, on Windows, the executable's icon in Explorer and the task bar. A square image at least 256 × 256 looks best. |
| **Target Config** | **Debug** keeps every check and is slow; **Release** is optimized and still carries debugging aids; **Dist** is the build to ship, without debugging files (`.pdb`). The zip ships only the **Dist** player, so an export from it always uses **Dist**. |

The **Export Preflight** list shows what the export needs and whether it is there: the startup scene, the player executable for the chosen Target Config, the asset pack (created during export), the engine resources, the compiled scripts (**stale** means the scripts changed since they were built) and the .NET runtime.

Every Sound, Sound Bank and audio clip goes into the asset pack. The export checks them first and stops on an error, such as a Sound with no clip (see [Authoring Audio](audio-authoring)).

## Export

Choose **File → Export Runtime…**, check the game name and output folder, and start the export. The editor builds whatever is missing, then writes the package while it shows the progress. You can cancel while it is still building.

The game is written to a folder named `<Game>-<Platform>`. The editor only replaces a folder it exported before; it never overwrites another folder with that name. Copy the folder anywhere and run the executable inside it. It finds its files whichever folder it is started from, so a shortcut or a launcher works.

## What players need

Nothing else. A game with scripts carries its own copy of the .NET runtime in its `DotNet/Runtime` folder, so players do not install .NET. (The export copies the runtime installed on your machine, so this adds roughly 70 MB for a game that has scripts; a game without scripts is much smaller.) Players do need a graphics driver with Vulkan.
