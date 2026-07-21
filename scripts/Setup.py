import os
import subprocess
import sys

import CheckPython

# Make sure everything we need is installed
CheckPython.ValidatePackages()

import colorama
from colorama import Back, Style

import Configure
import Vulkan

colorama.init()

PREMAKE = "vendor/bin/premake5.exe"

# Change from Scripts directory to root
os.chdir('../')
ROOT = os.getcwd()

config = Configure.run(sys.argv[1:], title="Lux setup")
if config is None:
    print("Cancelled.")
    sys.exit(1)

# Set LUX_DIR environment variable to current LUX root directory
print(f"{Style.BRIGHT}{Back.GREEN}Setting LUX_DIR to {ROOT}{Style.RESET_ALL}")
subprocess.call(["setx", "LUX_DIR", ROOT])
os.environ['LUX_DIR'] = ROOT

if config.enabled("skip-vulkan-check"):
    print(f"{Style.DIM}Skipping Vulkan SDK check.{Style.RESET_ALL}")
else:
    if not Vulkan.CheckVulkanSDK():
        print("Vulkan SDK not installed.")
        sys.exit(1)

    if Vulkan.CheckVulkanSDKDebugLibs():
        print(f"{Style.BRIGHT}{Back.GREEN}Vulkan SDK debug libs located.{Style.RESET_ALL}")

if config.enabled("skip-submodules"):
    print(f"{Style.DIM}Skipping git lfs pull and submodule update.{Style.RESET_ALL}")
else:
    subprocess.call(["git", "lfs", "pull"])
    subprocess.call(["git", "submodule", "update", "--init", "--recursive"])

Configure.warn_missing_discord_sdk(config, ROOT)

if not os.path.exists("Editor/DotNet/"):
    os.makedirs("Editor/DotNet/")

print(f"{Style.BRIGHT}{Back.GREEN}Generating {config.generator} solution.{Style.RESET_ALL}")
result = Configure.run_premake(config, PREMAKE)
if result != 0:
    print(f"{Style.BRIGHT}{Back.RED}Project generation failed.{Style.RESET_ALL}")
    sys.exit(result)

if not config.enabled("skip-scripts"):
    scripts_dir = os.path.join("Editor", "LuxSampleProject", "Assets", "Scripts")
    Configure.run_premake(config, os.path.join(ROOT, PREMAKE), cwd=scripts_dir, include_options=False)

print(f"{Style.BRIGHT}{Back.GREEN}Setup complete.{Style.RESET_ALL}")
