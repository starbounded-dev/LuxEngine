# Exporting a Game

Exporting builds a folder that runs your game without the editor: the player executable, your scenes and assets packed into an asset pack, and your compiled scripts.

## Before you export

Open **Project Settings** and expand **Runtime Export**:

1. Set the **Startup Scene**, the scene the game opens first.
2. The checklist shows what is ready. If **Lux-Runtime.exe** is missing, click **Build Runtime** once.
3. If the game has scripts, **Build Scripts** compiles them.

## Export settings

The same settings appear in **Project Settings → Runtime Export** and in the export window:

| Setting | What it does |
|---|---|
| **Game Name** | The window title and the name of the exported folder and executable. |
| **Window Width**, **Window Height** | The window size the game opens with (at least 320 × 240). |
| **Fullscreen** | Opens the game fullscreen instead of in a window. |
| **VSync** | Waits for the display's refresh, which avoids tearing and caps the frame rate. |
| **Icon** | An optional PNG or JPEG for the game window. |
| **Target Config** | **Debug** keeps every check and is slow; **Release** is optimized and still carries debugging aids; **Dist** is the build to ship, without debugging files (`.pdb`). |

The **Export Preflight** list shows what the export needs and whether it is there: the startup scene, the player executable for the chosen Target Config, the asset pack (created during export), the engine resources, the compiled scripts (**stale** means the scripts changed since they were built) and the .NET runtime.

If the project uses FMOD audio, the export checks every bank and stops if one is missing or older than the FMOD Studio project; build the banks first (see [Authoring Audio in FMOD Studio](audio-authoring)).

## Export

Choose **File → Export Runtime…**, check the game name and output folder, and start the export. The editor builds whatever is missing, then writes the package while it shows the progress. You can cancel while it is still building.

The game is written to a folder named `<Game>-<Platform>`. The editor only replaces a folder it exported before; it never overwrites another folder with that name. Copy the folder anywhere and run the executable inside it.
