# LuxEngine [![License](https://img.shields.io/github/license/starbounded-dev/luxengine.svg)](LICENSE) [![Build LuxEngine](https://github.com/starbounded-dev/LuxEngine/actions/workflows/main.yml/badge.svg)](https://github.com/starbounded-dev/LuxEngine/actions/workflows/main.yml)

![LuxEngine](/Resources/Branding/LuxEngineLogo.png?raw=true "LuxEngine")

LuxEngine is primarily an early-stage interactive application and rendering engine for Windows. Currently not much is implemented.

***

## Getting Started
Visual Studio 2022 is the recommended CI-compatible target. Visual Studio 2026 generation is also supported for local development when the v145 toolset is installed. LuxEngine is officially untested on other development environments while we focus on a Windows build.

<ins>**1. Downloading the repository:**</ins>

Start by cloning the repository with `git clone --recursive https://github.com/starbounded-dev/LuxEngine`.

If the repository was cloned non-recursively previously, use `git submodule update --init --recursive` to clone the necessary submodules.

<ins>**2. Configuring the dependencies:**</ins>

1. Run the [Setup.bat](scripts/Setup.bat) file found in the `scripts` folder. This validates Python packages, checks the Vulkan SDK, pulls Git LFS assets and submodules, and generates project files.
2. One prerequisite is the Vulkan SDK 1.4.x. If it is not installed, the script will download `VulkanSDK.exe` and prompt the user to install the SDK.
3. After installation, run [Setup.bat](scripts/Setup.bat) again. Debug builds require the Vulkan SDK shader debug libraries.
4. The setup script generates the root Visual Studio solution and the Sandbox project files. If changes are made, or if you want to regenerate project files, rerun the [Win-GenProjects.bat](scripts/Win-GenProjects.bat) script file found in the `scripts` folder.

***

## Continuous Integration
The [Build LuxEngine](.github/workflows/main.yml) workflow builds Debug, Release, and Dist on Windows Server 2025. It checks out LFS assets and submodules recursively, installs Python and the Vulkan SDK, generates Visual Studio 2022 project files with `scripts/Setup.py vs2022`, and builds `Lux.sln` for the `Mixed Platforms` solution platform. Debug and Release builds upload an `editor-<configuration>` artifact containing the built Editor output plus `Editor/imgui.ini`, `Editor/App.lsettings`, `Editor/SandboxProject`, `Editor/Resources`, and `Editor/mono`. MSBuild logs are uploaded for each configuration when the workflow runs.

***


## The Plan
The plan for LuxEngine is two-fold: to create a powerful 3D engine, but also to serve as an education tool for teaching game engine design and architecture. Because of this the development inside this repository is rather slow, since everything has to be taught and implemented by myself.

### Main features to come:
- Fast 2D rendering (UI, particles, sprites, etc.)
- High-fidelity Physically-Based 3D rendering (this will be expanded later, 2D to come first)
- Support for Mac, Linux, Android and iOS
    - Native rendering API support (DirectX, Vulkan, Metal)
- Fully featured viewer and editor applications
- Fully scripted interaction and behavior
- Integrated 3rd party 2D and 3D physics engine
- Procedural terrain and world generation
- Artificial Intelligence
- Audio system
