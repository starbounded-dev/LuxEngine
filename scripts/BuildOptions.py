"""Registry of the optional build settings offered by Setup / Win-GenProjects.

Adding a new toggle should only ever mean adding one entry to OPTIONS below.
Everything else - the checklist UI, the saved config, the command line flags -
is driven off this list.

Two kinds of option:

  premake  - passed through to premake5 as "--<flag>", so premake5.lua must
             declare a matching newoption.
  script   - changes what this script does; premake never sees it.
"""

import json
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(SCRIPT_DIR, ".luxsetup.json")

GENERATORS = [
    {
        "key": "vs2022",
        "label": "Visual Studio 2022",
        "description": "generates Lux.sln",
    },
    {
        "key": "vs2026",
        "label": "Visual Studio 2026",
        "description": "generates Lux.slnx",
    },
]

OPTIONS = [
    {
        "key": "discord",
        "kind": "premake",
        "flag": "discord",
        "label": "Discord Social SDK",
        "description": "Rich Presence; needs Core/vendor/discord_social_sdk",
        "default": False,
    },
    {
        "key": "no-tracy",
        "kind": "premake",
        "flag": "no-tracy",
        "label": "Disable Tracy profiler",
        "description": "smaller/faster build, no profiling instrumentation",
        "default": False,
    },
    {
        "key": "no-aftermath",
        "kind": "premake",
        "flag": "no-aftermath",
        "label": "Disable Nvidia Aftermath",
        "description": "skip the GPU crash dump tracker",
        "default": False,
    },
    {
        "key": "skip-submodules",
        "kind": "script",
        "label": "Skip submodule update",
        "description": "much faster re-runs; skip only if they're already current",
        "default": False,
    },
    {
        "key": "skip-vulkan-check",
        "kind": "script",
        "label": "Skip Vulkan SDK check",
        "description": "don't verify the SDK version before generating",
        "default": False,
    },
    {
        "key": "skip-scripts",
        "kind": "script",
        "label": "Skip C# script project",
        "description": "don't generate the sample project's script solution",
        "default": False,
    },
]

OPTIONS_BY_KEY = {option["key"]: option for option in OPTIONS}


def load_config():
    """Last run's choices, or empty defaults. Never raises - a corrupt or
    unreadable config just means we fall back to defaults."""
    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return {"generator": None, "options": []}

    if not isinstance(data, dict):
        return {"generator": None, "options": []}

    generator = data.get("generator")
    if generator not in [entry["key"] for entry in GENERATORS]:
        generator = None

    saved = data.get("options")
    options = [key for key in saved if key in OPTIONS_BY_KEY] if isinstance(saved, list) else []

    return {"generator": generator, "options": options}


def save_config(generator, selected_keys):
    try:
        with open(CONFIG_PATH, "w", encoding="utf-8") as handle:
            json.dump(
                {"generator": generator, "options": sorted(selected_keys)},
                handle,
                indent=2,
            )
            handle.write("\n")
    except OSError:
        # Persisting preferences is a convenience; never fail a generate over it.
        pass


def build_checklist_items(preselected):
    """Checklist rows, pre-ticked from `preselected` (falling back to defaults)."""
    items = []
    for option in OPTIONS:
        checked = option["key"] in preselected if preselected else option["default"]
        items.append(
            {
                "key": option["key"],
                "label": option["label"],
                "description": option["description"],
                "checked": checked,
            }
        )
    return items


def premake_args(selected_keys):
    """The "--flag" arguments premake should receive for this selection."""
    args = []
    for key in selected_keys:
        option = OPTIONS_BY_KEY.get(key)
        if option and option["kind"] == "premake":
            args.append(f"--{option['flag']}")
    return args


def parse_cli(argv):
    """Reads flags off the command line so the scripts stay automatable.

    Returns (generator, selected_keys, mode) where mode is one of:
      "interactive" - nothing specified, show the menus
      "last"        - --last, reuse the saved config with no prompts
      "explicit"    - flags given, use them verbatim
    """
    generator = None
    selected = []
    explicit = False
    use_last = False

    generator_keys = {entry["key"] for entry in GENERATORS}

    for raw in argv:
        arg = raw.lower().strip().lstrip("-")

        if arg in generator_keys:
            generator = arg
            explicit = True
        elif arg in ("last", "reuse"):
            use_last = True
        elif arg in OPTIONS_BY_KEY:
            selected.append(arg)
            explicit = True

    if use_last:
        return generator, selected, "last"
    return generator, selected, "explicit" if explicit else "interactive"
