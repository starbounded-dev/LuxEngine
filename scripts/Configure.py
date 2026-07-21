"""Shared configuration front-end for Setup.py and Win-GenProjects.py.

Runs the two menus (optional settings, then generator), remembers the result,
and hands back a plain object the callers act on.
"""

import os
import subprocess
import sys

from colorama import Back, Fore, Style

import BuildOptions
import Menu


class Configuration:
    def __init__(self, generator, selected_keys):
        self.generator = generator
        self.selected = set(selected_keys)

    def enabled(self, key):
        return key in self.selected

    @property
    def premake_args(self):
        return BuildOptions.premake_args(self.selected)

    def describe(self):
        names = [
            option["label"]
            for option in BuildOptions.OPTIONS
            if option["key"] in self.selected
        ]
        return ", ".join(names) if names else "none"


def _installed_generators():
    """Map generator key -> True when that Visual Studio is actually installed.

    Uses vswhere, which ships with every VS 2017+ installer. If it isn't there we
    just don't annotate anything - this is a hint, never a gate.
    """
    if sys.platform != "win32":
        return {}

    program_files = os.environ.get("ProgramFiles(x86)") or os.environ.get("ProgramFiles")
    if not program_files:
        return {}

    vswhere = os.path.join(program_files, "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.exists(vswhere):
        return {}

    try:
        output = subprocess.check_output(
            [vswhere, "-all", "-products", "*", "-property", "catalog_productLineVersion", "-nologo"],
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (subprocess.SubprocessError, OSError):
        return {}

    # vswhere reports either the marketing year (2022) or the major version (18 = 2026).
    found = {line.strip() for line in output.splitlines() if line.strip()}
    return {
        "vs2022": "2022" in found or "17" in found,
        "vs2026": "2026" in found or "18" in found,
    }


def run(argv, title="Lux setup"):
    """Resolve a Configuration from the command line and/or the menus.
    Returns None if the user backed out."""
    saved = BuildOptions.load_config()
    cli_generator, cli_options, mode = BuildOptions.parse_cli(argv)

    if mode == "last":
        generator = cli_generator or saved["generator"] or BuildOptions.GENERATORS[0]["key"]
        config = Configuration(generator, saved["options"])
        print(
            f"{Style.BRIGHT}Reusing saved configuration:{Style.RESET_ALL} "
            f"{generator}, options: {config.describe()}"
        )
        return config

    if mode == "explicit":
        generator = cli_generator or saved["generator"] or BuildOptions.GENERATORS[0]["key"]
        config = Configuration(generator, cli_options)
        BuildOptions.save_config(generator, config.selected)
        return config

    # --- interactive ---------------------------------------------------------
    print("")
    print(f"{Style.BRIGHT}{Back.GREEN} {title} {Style.RESET_ALL}")

    items = BuildOptions.build_checklist_items(saved["options"])
    selected = Menu.checklist("Optional settings", items)
    if selected is None:
        return None

    installed = _installed_generators()
    generator_options = []
    for entry in BuildOptions.GENERATORS:
        note = None
        if installed.get(entry["key"]) is True:
            note = "(installed)"
        elif installed and installed.get(entry["key"]) is False:
            note = "(not detected)"
        generator_options.append(
            {"label": entry["label"], "description": entry["description"], "note": note}
        )

    # Default the cursor to whatever was used last, else the first installed one.
    default_index = 0
    if saved["generator"]:
        default_index = next(
            (i for i, e in enumerate(BuildOptions.GENERATORS) if e["key"] == saved["generator"]),
            0,
        )
    elif installed:
        default_index = next(
            (i for i, e in enumerate(BuildOptions.GENERATORS) if installed.get(e["key"])),
            0,
        )

    choice = Menu.select("Visual Studio version", generator_options, default=default_index)
    if choice is None:
        return None

    generator = BuildOptions.GENERATORS[choice]["key"]
    config = Configuration(generator, selected)
    BuildOptions.save_config(generator, config.selected)

    print("")
    print(f"{Style.BRIGHT}Generator:{Style.RESET_ALL} {generator}")
    print(f"{Style.BRIGHT}Options:{Style.RESET_ALL}   {config.describe()}")
    print("")

    return config


def run_premake(config, premake_path, cwd=None, include_options=True):
    """Invoke premake with the configured action, and optionally the feature flags.

    `include_options` is False for the sample project's C# solution: it has its own
    premake5.lua that declares none of our newoptions, and premake hard-errors on an
    option it doesn't recognise.

    The path is normalised to an absolute native path: a relative one breaks as
    soon as `cwd` is set, and CreateProcess doesn't reliably resolve forward
    slashes in a relative executable path either.
    """
    premake_path = os.path.normpath(os.path.abspath(premake_path))
    options = config.premake_args if include_options else []
    command = [premake_path] + options + [config.generator]
    print(f"{Style.DIM}$ {' '.join(command)}{Style.RESET_ALL}")
    return subprocess.call(command, cwd=cwd)


def warn_missing_discord_sdk(config, root):
    """The Discord SDK is fetched manually, so catch its absence before premake
    generates a project that can't compile."""
    if not config.enabled("discord"):
        return True

    include_dir = os.path.join(root, "Core", "vendor", "discord_social_sdk", "include")
    # Check both: a hand-trimmed copy of the archive very easily ends up with discordpp.h
    # but not cdiscord.h, which fails the build with a confusing error much later.
    missing = [
        name
        for name in ("discordpp.h", "cdiscord.h")
        if not os.path.exists(os.path.join(include_dir, name))
    ]
    if not missing:
        return True

    print("")
    print(f"{Style.BRIGHT}{Back.RED} Discord SDK incomplete {Style.RESET_ALL}")
    print(f"Missing from {include_dir}: {', '.join(missing)}")
    print("See Core/vendor/discord_social_sdk/DISCORD_SDK_SETUP.md.")
    print(f"{Fore.YELLOW}Continuing anyway - the generated project will not compile until it exists.{Style.RESET_ALL}")
    print("")
    return False
