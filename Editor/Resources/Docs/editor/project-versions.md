# Project Versions and Upgrades

Every project, scene, prefab and material file records the format it was saved in (`FormatVersion` on its first line), and the project file also records which Lux saved it. Lux uses the number to decide what to do when you open a project made by another version.

## Opening an older project

A project made before 2027.1 has no version in its files. When you open it, or any project saved by an older Lux, Lux offers to **Upgrade and Open**:

- Before anything changes, every file that will change is copied to the project's `.luxbackup` folder, in a sub-folder named for the date and time, keeping its folder layout. To go back, copy the files back.
- Then each file is rewritten. The upgrade only adds or changes what the new format needs: your scene and asset content stays as it was.
- **Cancel** opens nothing and changes nothing.

Once upgraded, an older Lux may not open the project. A project that is already current opens at once, and a second open never makes another backup.

## Opening a newer project

A project, scene or material saved by a newer Lux is refused with a message that names the file, its format version and the Lux that wrote it. Update Lux to open it; nothing is changed. A game exported by a newer Lux is refused the same way by an older player.

## Automated runs

Scripts and test setups that start the editor can answer for you with the `LUX_PROJECT_UPGRADE` environment variable: `yes` upgrades without asking, `no` refuses an older project, `ask` shows the dialog. Without it, runs started with `LUX_NO_WATCHDOG` or `LUX_USER_DATA_DIR` upgrade without asking.

The backups are ordinary folders: delete `.luxbackup` when you no longer need them. Keep the folder out of version control if you use it for the project.
