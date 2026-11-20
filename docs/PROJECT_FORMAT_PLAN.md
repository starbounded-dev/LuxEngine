# Project format versions and upgrade path (M8, issue #44)

Point-in-time plan. Checkpoints are the board's sub-issues, in dependency order: **X3 #149** (versions in the files, upgrade steps, tests against saved old projects), then **L2 #146** (the editor's open flow: refuse a newer project, offer an upgrade with a backup).

## Decisions

- **Every text format gets a number.** `FormatVersion: N` is the first top-level key of a `.luxproj`, `.luxscene`, `.lprefab`, `.lmat`, `.lsound`, `.lbank`, `.lsheet` and `.lanimgraph`. The project file also records `EngineVersion` (the Lux that last saved it), for messages only: decisions use the number.
- **No key means version 0**: every file written before 2027.1. Version 1 is the format 2027.1 writes; going from 0 to 1 changes no data, it only stamps the number, so a 2027.1 editor reads version 0 and 1 the same way. Old projects keep loading even if nobody upgrades them.
- **A newer file is refused, never half-read.** A project, scene, prefab, material, sound, bank, sprite sheet or animation graph whose `FormatVersion` is above what this build knows fails to load with a message that names the file, its version and the engine that wrote it.
- **Upgrades are steps**: an ordered table of (kind, from version, function from text to text). The upgrade walks a file through every step from its version to the current one. Step 0 to 1 inserts the key; the next format change adds a step beside it. Text in, text out keeps comments, ordering and line endings.
- **The upgrade is explicit and backed up.** Opening a project with older files offers **Upgrade**: every file that will change is first copied to `<project>/.luxbackup/<date-time>/` with its relative path, then rewritten atomically. Declining opens nothing. Unattended runs (`LUX_PROJECT_UPGRADE=yes`, or the isolated test harnesses: `LUX_NO_WATCHDOG`, `LUX_USER_DATA_DIR`) upgrade without asking; `LUX_PROJECT_UPGRADE=no` refuses.
- **The runtime player is not upgraded.** An export is made by the matching editor; a player that meets a newer project or pack refuses with the same message.

## Checkpoints

1. X3: `Lux/Project/FormatVersion` (constants, read, write, newer check), the eight serializers write and check it, `ProjectUpgrade` (inspect, back up, step table, upgrade), headless checks against saved version 0 projects (`tests/project-format`) and against a fake newer file.
2. L2: the editor's open flow (message for a newer project, upgrade dialog, backup report), the runtime's refusal, help page, an editor self-test.
