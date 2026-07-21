import os
import sys

import CheckPython

# Make sure everything we need is installed
CheckPython.ValidatePackages()

import colorama
from colorama import Back, Style

import Configure

colorama.init()

PREMAKE = "vendor/bin/premake5.exe"

# Change from Scripts directory to root
os.chdir('../')
ROOT = os.getcwd()

config = Configure.run(sys.argv[1:], title="Lux project generation")
if config is None:
    print("Cancelled.")
    sys.exit(1)

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

print(f"{Style.BRIGHT}{Back.GREEN}Done.{Style.RESET_ALL}")
