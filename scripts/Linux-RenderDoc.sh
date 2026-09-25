#!/bin/sh
#
# Runs the editor inside RenderDoc, exactly as Linux-Run.sh would run it: same build check, same
# Vulkan environment. RenderDoc opens with the editor already launched and attached.
#
#   ./scripts/Linux-RenderDoc.sh              # prompts for a configuration
#   ./scripts/Linux-RenderDoc.sh debug        # non-interactive
#   LUX_SKIP_BUILD=1 ./scripts/Linux-RenderDoc.sh release
#   LUX_RENDERDOC_WAYLAND=1 ./scripts/Linux-RenderDoc.sh debug   # stay on Wayland (no F12 hotkey)
#
# The editor runs under XWayland by default so RenderDoc's F12 / Print Screen capture keys work.
# Captures land in RenderDoc's capture list; double-click one to open it. Requires RenderDoc
# (Arch: sudo pacman -S renderdoc) - it is a developer tool, not something games need.

set -e

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
LUX_RENDERDOC=1 exec "$SCRIPT_DIR/Linux-Run.sh" "$@"
