"""Small interactive terminal menus (checklist + single-select).

Deliberately dependency-free apart from colorama, which the setup scripts already
require. Redraws in place using ANSI cursor movement rather than clearing the
screen, so the menu doesn't flicker and scrollback isn't destroyed.

Falls back to plain text prompts when stdin isn't a TTY (CI, piped input), so the
same code path works unattended.
"""

import sys

from colorama import Fore, Style

# --- key reading -------------------------------------------------------------

KEY_UP = "up"
KEY_DOWN = "down"
KEY_ENTER = "enter"
KEY_SPACE = "space"
KEY_QUIT = "quit"
KEY_OTHER = "other"


def _read_key_windows():
    import msvcrt

    ch = msvcrt.getch()
    if ch in (b"\x00", b"\xe0"):          # arrow keys arrive as a two-byte sequence
        arrow = msvcrt.getch()
        return {b"H": KEY_UP, b"P": KEY_DOWN}.get(arrow, KEY_OTHER)
    if ch in (b"\r", b"\n"):
        return KEY_ENTER
    if ch == b" ":
        return KEY_SPACE
    if ch in (b"\x1b", b"q", b"Q"):        # Esc / q
        return KEY_QUIT
    if ch == b"\x03":                       # Ctrl+C
        raise KeyboardInterrupt
    if ch in (b"w", b"W", b"k"):
        return KEY_UP
    if ch in (b"s", b"S", b"j"):
        return KEY_DOWN
    return KEY_OTHER


def _read_key_posix():
    import termios
    import tty

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        ch = sys.stdin.read(1)
        if ch == "\x1b":
            # Could be a bare Esc or the start of an arrow sequence; the following
            # two reads are what distinguish them.
            seq = sys.stdin.read(2)
            if seq == "[A":
                return KEY_UP
            if seq == "[B":
                return KEY_DOWN
            return KEY_QUIT
        if ch in ("\r", "\n"):
            return KEY_ENTER
        if ch == " ":
            return KEY_SPACE
        if ch in ("q", "Q"):
            return KEY_QUIT
        if ch == "\x03":
            raise KeyboardInterrupt
        if ch in ("w", "W", "k"):
            return KEY_UP
        if ch in ("s", "S", "j"):
            return KEY_DOWN
        return KEY_OTHER
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)


def _read_key():
    if sys.platform == "win32":
        return _read_key_windows()
    return _read_key_posix()


def is_interactive():
    """True when we can actually drive a live menu."""
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        return False
    if sys.platform == "win32":
        try:
            import msvcrt  # noqa: F401
        except ImportError:
            return False
    else:
        try:
            import termios  # noqa: F401
            import tty      # noqa: F401
        except ImportError:
            return False
    return True


# --- rendering ---------------------------------------------------------------

def _title(text):
    print(f"{Style.BRIGHT}{Fore.CYAN}{text}{Style.RESET_ALL}")


def _hint(text):
    print(f"{Style.DIM}{text}{Style.RESET_ALL}")


def _clear_lines(count):
    """Move the cursor back up `count` lines, clearing each one."""
    for _ in range(count):
        sys.stdout.write("\x1b[1A\x1b[2K")
    sys.stdout.flush()


# --- checklist ---------------------------------------------------------------

def checklist(title, items, hint=None):
    """Multi-select menu.

    `items` is a list of dicts with 'label', optional 'description', and
    'checked'. Returns the list of keys that ended up checked, or None if the
    user backed out. A "Continue" row sits at the bottom and is what commits
    the selection.
    """
    if not items:
        return []

    if not is_interactive():
        # Non-interactive: honour the defaults rather than hanging on input.
        return [item["key"] for item in items if item.get("checked")]

    header(title, hint or "Up/Down to move, Space or Enter to tick, Enter on Continue when done.")

    state = [bool(item.get("checked")) for item in items]
    cursor = 0
    drawn = 0

    while True:
        if drawn:
            _clear_lines(drawn)
        drawn = 0

        for index, item in enumerate(items):
            selected = index == cursor
            box = "[x]" if state[index] else "[ ]"
            arrow = ">" if selected else " "
            label = item["label"]
            description = item.get("description", "")

            if selected:
                line = f"{Fore.CYAN}{Style.BRIGHT}{arrow} {box} {label}{Style.RESET_ALL}"
            elif state[index]:
                line = f"{arrow} {Fore.GREEN}{box}{Style.RESET_ALL} {label}"
            else:
                line = f"{arrow} {Style.DIM}{box}{Style.RESET_ALL} {label}"

            if description:
                line += f"  {Style.DIM}- {description}{Style.RESET_ALL}"
            print(line)
            drawn += 1

        print("")
        drawn += 1

        if cursor == len(items):
            print(f"{Fore.CYAN}{Style.BRIGHT}> [ Continue ]{Style.RESET_ALL}")
        else:
            print(f"  {Style.DIM}[ Continue ]{Style.RESET_ALL}")
        drawn += 1

        key = _read_key()

        if key == KEY_UP:
            cursor = (cursor - 1) % rows_count(items)
        elif key == KEY_DOWN:
            cursor = (cursor + 1) % rows_count(items)
        elif key == KEY_SPACE:
            if cursor < len(items):
                state[cursor] = not state[cursor]
        elif key == KEY_ENTER:
            if cursor == len(items):
                return [item["key"] for item, on in zip(items, state) if on]
            # Enter on an option toggles it, so the flow the user asked for
            # (tick with Enter, then Enter on Continue) works as well as Space.
            state[cursor] = not state[cursor]
        elif key == KEY_QUIT:
            return None


def rows_count(items):
    return len(items) + 1  # options + Continue


# --- single select -----------------------------------------------------------

def select(title, options, hint=None, default=0):
    """Single-select menu. `options` is a list of dicts with 'label',
    optional 'description' and 'note'. Returns the chosen index, or None."""
    if not options:
        return None

    if not is_interactive():
        return default

    header(title, hint or "Up/Down to move, Enter to select.")

    cursor = max(0, min(default, len(options) - 1))
    drawn = 0

    while True:
        if drawn:
            _clear_lines(drawn)
        drawn = 0

        for index, option in enumerate(options):
            selected = index == cursor
            arrow = ">" if selected else " "
            label = option["label"]

            if selected:
                line = f"{Fore.CYAN}{Style.BRIGHT}{arrow} {label}{Style.RESET_ALL}"
            else:
                line = f"{arrow} {label}"

            note = option.get("note")
            if note:
                line += f"  {Fore.GREEN}{note}{Style.RESET_ALL}"

            description = option.get("description")
            if description:
                line += f"  {Style.DIM}- {description}{Style.RESET_ALL}"

            print(line)
            drawn += 1

        key = _read_key()

        if key == KEY_UP:
            cursor = (cursor - 1) % len(options)
        elif key == KEY_DOWN:
            cursor = (cursor + 1) % len(options)
        elif key in (KEY_ENTER, KEY_SPACE):
            return cursor
        elif key == KEY_QUIT:
            return None


# --- headers -----------------------------------------------------------------

def header(title, hint):
    print("")
    _title(title)
    _hint(hint)
    print("")
