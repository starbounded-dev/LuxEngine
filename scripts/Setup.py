import os
import subprocess
import CheckPython

# Make sure everything we need is installed
CheckPython.ValidatePackages()

import Vulkan
import Utils
import colorama
from colorama import Fore
from colorama import Back
from colorama import Style

colorama.init()

# Change from Scripts directory to root
os.chdir('../')

# Set LUX_DIR environment variable to current LUX root directory
print(f"{Style.BRIGHT}{Back.GREEN}Setting LUX_DIR to {os.getcwd()}{Style.RESET_ALL}")
subprocess.call(["setx", "LUX_DIR", os.getcwd()])
os.environ['LUX_DIR'] = os.getcwd()

if (not Vulkan.CheckVulkanSDK()):
    print("Vulkan SDK not installed.")
    exit()
    
if (Vulkan.CheckVulkanSDKDebugLibs()):
    print(f"{Style.BRIGHT}{Back.GREEN}Vulkan SDK debug libs located.{Style.RESET_ALL}")

subprocess.call(["git", "lfs", "pull"])
subprocess.call(["git", "submodule", "update", "--init", "--recursive"])

if not os.path.exists("Editor/DotNet/"):
    os.makedirs("Editor/DotNet/")

print(f"{Style.BRIGHT}{Back.GREEN}Generating Visual Studio 2022 solution.{Style.RESET_ALL}")
subprocess.call(["vendor/bin/premake5.exe", "vs2022"])

os.chdir('Editor/SandboxProject')
subprocess.call(["../../vendor/bin/premake5.exe", "vs2022"])