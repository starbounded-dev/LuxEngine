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

# Set STAR_DIR environment variable to current StarEngine root directory
print(f"{Style.BRIGHT}{Back.GREEN}Setting STAR_DIR to {os.getcwd()}{Style.RESET_ALL}")
subprocess.call(["setx", "STAR_DIR", os.getcwd()])
os.environ['STAR_DIR'] = os.getcwd()

if (not Vulkan.CheckVulkanSDK()):
    print("Vulkan SDK not installed.")
    exit()
    
if (Vulkan.CheckVulkanSDKDebugLibs()):
    print(f"{Style.BRIGHT}{Back.GREEN}Vulkan SDK debug libs located.{Style.RESET_ALL}")

subprocess.call(["git", "lfs", "pull"])
subprocess.call(["git", "submodule", "update", "--init", "--recursive"])

if not os.path.exists("StarEditor/DotNet/"):
    os.makedirs("StarEditor/DotNet/")

print(f"{Style.BRIGHT}{Back.GREEN}Generating Visual Studio 2022 solution.{Style.RESET_ALL}")
subprocess.call(["vendor/bin/premake5.exe", "vs2022"])

os.chdir('StarEditor/SandboxProject')
subprocess.call(["../../vendor/bin/premake5.exe", "vs2022"])