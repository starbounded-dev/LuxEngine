# LuxEngine [![License](https://img.shields.io/github/license/starbounded-dev/luxengine.svg)](https://github.com/starbounded-dev/luxengine/blob/main/LICENSE) [![Build LuxEngine](https://github.com/starbounded-dev/LuxEngine/actions/workflows/main.yml/badge.svg)](https://github.com/starbounded-dev/LuxEngine/actions/workflows/main.yml)

![LuxEngine](/Resources/Branding/StarStudioLogo.png?raw=true "LuxEngine")

LuxEngine is primarily an early-stage interactive application and rendering engine for Windows. Currently not much is implemented.

***

## Getting Started
Visual Studio 2022 is recommended, LuxEngine is officially untested on other development environments whilst we focus on a Windows build.

<ins>**1. Downloading the repository:**</ins>

Start by cloning the repository with `git clone --recursive https://github.com/starbounded-dev/LuxEngine`.

If the repository was cloned non-recursively previously, use `git submodule update --init` to clone the necessary submodules.

<ins>**2. Configuring the dependencies:**</ins>

1. Run the [Setup.bat](https://github.com/starbounded-dev/LuxEngine/blob/master/scripts/Setup.bat) file found in `scripts` folder. This will download the required prerequisites for the project if they are not present yet.
2. One prerequisite is the Vulkan SDK. If it is not installed, the script will execute the `VulkanSDK.exe` file, and will prompt the user to install the SDK.
3. After installation, run the [Setup.bat](https://github.com/starbounded-dev/LuxEngine/blob/master/scripts/Setup.bat) file again. If the Vulkan SDK is installed properly, it will then download the Vulkan SDK Debug libraries. (This may take a longer amount of time)
4. After downloading and unzipping the files, the [Win-GenProjects.bat](https://github.com/starbounded-dev/LuxEngine/blob/master/scripts/Win-GenProjects.bat) script file will get executed automatically, which will then generate a Visual Studio solution file for user's usage.

If changes are made, or if you want to regenerate project files, rerun the [Win-GenProjects.bat](https://github.com/starbounded-dev/LuxEngine/blob/master/scripts/Win-GenProjects.bat) script file found in `scripts` folder.

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
