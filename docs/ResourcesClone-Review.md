# ResourcesClone review

Reviewed `Editor/ResourcesClone` against the active M: checkout on 2026-11-04.
The folder is an untracked LuckyEngine/Hazel resource archive, not a ResourceClone C++ API.
There are 534 files. No production source references the archive directory.

## Reused

The archive has a self-contained `Meshes/Default` set. Lux already offers seven of these
primitives in `SceneHierarchyPanel::DrawEntityCreateMenu` and five in `MaterialPreview`, but
`EditorLayer::CreateProject` previously created an empty mesh-source directory. Consequently
new projects could not use those primitives or material preview shapes.

The editor now bundles the seven supported glTF/bin pairs and seeds new projects before
opening/importing them. The files come from the already tracked sample project: six pairs
are byte-identical to the archive, and Capsule keeps Lux's existing sample version rather
than introducing the archive's different geometry. Total bundled geometry is under 300 KiB.
There is no new library, installation requirement, asset type, shader binding or render pass.
Existing destination files are preserved and copy failures stop creation with a diagnostic.
Only geometry is copied; asset registries, imported materials and cached handles are not.

## Reviewed but left in the archive

| Category | Finding / fit |
| --- | --- |
| Renderer and textures | BRDF LUT and error texture already match the active resources. |
| Shaders (116 files) | Different renderer ABI: archive `PointLightData` is a std430 storage buffer with 1024 entries at set 1 / binding 6; Lux uses a std140 uniform buffer with 1000 entries and a different PointLight layout. Direct replacement corrupts CPU/GPU agreement. Lux already has its own QTangent, clustered culling, mesh shaders, exposure, FFT bloom, shadows and spatial postprocessing. |
| PathTracing / PTTaa | Require absent integration and conflict with the repository's rules against temporal rendering and mining deleted ray-tracing work. |
| Script-project templates | Useful patterns include generated-source exclusions, editable user build settings, and early installation checks. These premake templates target `Hazel-ScriptCore.dll`, `.hproj`, `--hazel-dir` and a generated Lua file. Lux creates SDK-style .csproj files directly and already keeps generated intermediates outside Source. Copying them would introduce a second incompatible build path. |
| AgentDocs and robot scripts | Describe LuckyEngine-specific tool APIs, MuJoCo components, motion graphs and robot policies. They are not documentation or examples for Lux's current API. |
| Animation / robot graph templates | The archive uses `.hanimgraph` / `.hmograph`; Lux recognizes `.lanimgraph` and has no MotionGraph asset type. No compatible import path was established, so these templates were left aside. |
| Fonts and editor art | Much is already identical. New emoji/fallback fonts need font-atlas support, licensing/size review and actual UI integration; Lucky branding and platform metadata do not belong in Lux packaging. |
| Managed binaries and ShaderPack.hsp | Compiled artifacts for another engine; retain Lux's builds and shader pack format. |
| Octahedron.glb and meadow HDR | No current primitive-menu integration for Octahedron; no need to ship an additional environment map to fix the demonstrated gap. |

## Validation

`tests/editor/run_project_templates.py` compiles and runs the production copy helper,
checks that every glTF's external buffers exist with the declared size, exercises fresh and
repeated initialization, preserves authored geometry, and checks missing inputs and blocked
output paths. Outputs stay under `bin/project-template-tests` on the selected checkout.
Verified on Windows: the production-helper regression executable passed, project generation
completed, and the native Debug solution build passed (Core, ScriptCore, Editor and Lux-Runtime).
`Editor.exe` was checked to postdate both changed production inputs. The build log is
`bin/project-template-tests/debug-build.log`. The local `/cr` review found no remaining issues
in this change. Existing staged and unrelated working-tree changes were preserved.

Visual confirmation in the editor remains a separate check: create a project, use Create 3D > Meshes, and switch Material Editor preview shapes.

No web research was needed: the reuse decision follows the supplied archive, the existing
tracked geometry, and the current project-creation/import code. No archive instruction text
was treated as project guidance.
