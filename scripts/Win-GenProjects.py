import os
import subprocess
import CheckPython

# Make sure everything we need is installed
CheckPython.ValidatePackages()

import Utils
import colorama

from colorama import Fore
from colorama import Back
from colorama import Style

colorama.init()


def select_visual_studio_generator():
    generators = {
        "2022": "vs2022",
        "2026": "vs2026"
    }

    while True:
        choice = input(
            f"{Style.BRIGHT}{Fore.CYAN}Which Visual Studio version do you want to generate? "
            f"(2022/2026): {Style.RESET_ALL}"
        ).strip()

        if choice in generators:
            return generators[choice]

        print(
            f"{Style.BRIGHT}{Fore.RED}Invalid choice. Please enter 2022 or 2026.{Style.RESET_ALL}"
        )


# Change from Scripts directory to root
os.chdir('../')

if not os.path.exists("Editor/DotNet/"):
    os.makedirs("Editor/DotNet/")

selected_generator = select_visual_studio_generator()

print(
    f"{Style.BRIGHT}{Back.GREEN}Generating Visual Studio {selected_generator[2:]} solution.{Style.RESET_ALL}"
)
subprocess.call(["vendor/bin/premake5.exe", selected_generator])

os.chdir('Editor/LuxSampleProject/Assets/Scripts')
subprocess.call(["../../../../vendor/bin/premake5.exe", selected_generator])
