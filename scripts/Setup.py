import os
import sys
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


def choose_visual_studio_generator():
    options = ["vs2022", "vs2026"]

    if len(sys.argv) > 1:
        selected = sys.argv[1].lower().strip()
        if selected in options:
            return selected

    try:
        import msvcrt
    except ImportError:
        while True:
            choice = input("Choose Visual Studio generator (vs2022/vs2026): ").strip().lower()
            if choice in options:
                return choice
            print(f"{Fore.RED}Invalid choice. Please enter vs2022 or vs2026.{Style.RESET_ALL}")

    selected_index = 0

    def render_menu():
        os.system("cls")
        print(f"{Style.BRIGHT}{Back.GREEN}Choose Visual Studio generator{Style.RESET_ALL}")
        print()
        print("Use the arrow keys to move, then press Enter to confirm.")
        print()

        for index, option in enumerate(options):
            prefix = ">" if index == selected_index else " "
            if index == selected_index:
                print(f"{Fore.CYAN}{Style.BRIGHT}{prefix} {option}{Style.RESET_ALL}")
            else:
                print(f"  {option}")

    while True:
        render_menu()
        key = msvcrt.getch()

        if key in (b"\r", b"\n"):
            os.system("cls")
            return options[selected_index]

        if key in (b"\x00", b"\xe0"):
            arrow = msvcrt.getch()
            if arrow == b"H":  # Up
                selected_index = (selected_index - 1) % len(options)
            elif arrow == b"P":  # Down
                selected_index = (selected_index + 1) % len(options)


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

generator = choose_visual_studio_generator()

print(f"{Style.BRIGHT}{Back.GREEN}Generating {generator} solution.{Style.RESET_ALL}")
subprocess.call(["vendor/bin/premake5.exe", generator])

os.chdir('Editor/LuxSampleProject/Assets/Scripts')
subprocess.call(["../../../../vendor/bin/premake5.exe", generator])
