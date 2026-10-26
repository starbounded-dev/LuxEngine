# Exporting a Game

Exporting builds a folder that runs your game without the editor: the player executable, your scenes and assets packed into an asset pack, and your compiled scripts.

## Before you export

Open **Project Settings** and expand **Runtime Export**:

1. Set the **Startup Scene**, the scene the game opens first.
2. The checklist shows what is ready. If **Lux-Runtime.exe** is missing, click **Build Runtime** once.
3. If the game has scripts, **Build Scripts** compiles them.

## Export

Choose **File → Export Runtime…**, check the game name and output folder, and start the export. The editor builds whatever is missing, then writes the package while it shows the progress. You can cancel while it is still building.

The game is written to a folder named `<Game>-<Platform>`. The editor only replaces a folder it exported before; it never overwrites another folder with that name. Copy the folder anywhere and run the executable inside it.
