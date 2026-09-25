#!/bin/sh
#
# Runs the editor, first making sure the build is current.
#
#   ./scripts/Linux-Run.sh              # prompts for a configuration
#   ./scripts/Linux-Run.sh release      # non-interactive
#   LUX_SKIP_BUILD=1 ./scripts/Linux-Run.sh debug   # skip the rebuild check, run as-is
#   LUX_RENDERDOC=1 ./scripts/Linux-Run.sh debug    # launch it inside RenderDoc (see Linux-RenderDoc.sh)
#
# Extra arguments are forwarded to the Editor binary.

set -e

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
LUX_DIR=${LUX_DIR:-$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)}
export LUX_DIR
cd "$LUX_DIR"

if [ -n "${BUILD_CONFIG+set}" ]
	then
		true
	elif [ -n "$1" ]
	then
		case "$1" in
			debug|Debug)     export BUILD_CONFIG=Debug ;;
			release|Release) export BUILD_CONFIG=Release ;;
			dist|Dist)       export BUILD_CONFIG=Dist ;;
			*)
				echo "Unknown config: $1"
				echo "Usage: $0 [debug|release|dist]"
				exit 1
				;;
		esac
		shift
	else
		echo "Select build configuration:"
		echo "  1) Debug"
		echo "  2) Release"
		echo "  3) Dist"
		printf "Choice [1-3]: "
		read choice
		case "$choice" in
			1) export BUILD_CONFIG=Debug ;;
			2) export BUILD_CONFIG=Release ;;
			3) export BUILD_CONFIG=Dist ;;
			*)
				echo "Invalid choice"
				exit 1
				;;
		esac
fi

CONFIG=$(echo "$BUILD_CONFIG" | tr '[:upper:]' '[:lower:]')

# Linux-Build.sh is idempotent (make only rebuilds what changed), so this catches forgotten
# recompiles without meaningfully slowing down a launch where nothing moved. BUILD_CONFIG is
# already exported above, so it won't re-prompt.
if [ -z "${LUX_SKIP_BUILD+set}" ]; then
	"$LUX_DIR/scripts/Linux-Build.sh"
fi

# Honor a VULKAN_SDK the caller already set; otherwise use the bundled SDK and fail loudly if it is
# missing rather than exporting an empty path that silently breaks the layer/lib lookups below.
if [ -z "${VULKAN_SDK:-}" ]; then
	if ! VULKAN_SDK=$(realpath -e "$LUX_DIR/Core/vendor/VulkanSDK/x86_64"); then
		echo "Vulkan SDK not found at $LUX_DIR/Core/vendor/VulkanSDK/x86_64 — run scripts/Linux-Fetch.sh" >&2
		exit 1
	fi
	export VULKAN_SDK
fi
export VK_LAYER_PATH="$VULKAN_SDK/share/vulkan/explicit_layer.d"
export PATH="$VULKAN_SDK/bin:$PATH"
export LD_LIBRARY_PATH="$VULKAN_SDK/lib:$LUX_DIR/Core/vendor/assimp/bin/linux:$LUX_DIR/Core/vendor/NvidiaAftermath/lib/x64/linux"

EDITOR_BIN="$LUX_DIR/bin/$BUILD_CONFIG-linux-x86_64/Editor/Editor"
cd "$LUX_DIR/Editor"

if [ -z "${LUX_RENDERDOC:-}" ]; then
	exec "$EDITOR_BIN" "$@"
fi

# RenderDoc mode: open qrenderdoc with a capture-settings file that launches the editor straight
# away. The editor inherits everything set up above (Vulkan SDK, layers, library paths).
if ! command -v qrenderdoc >/dev/null 2>&1; then
	echo "qrenderdoc not found. Install RenderDoc (Arch: sudo pacman -S renderdoc)." >&2
	exit 1
fi

# RenderDoc cannot read the keyboard of a Wayland window, so its F12 / Print Screen capture keys
# only work under X11. Run under XWayland unless asked not to; the Launch tab's "Trigger Capture"
# button works either way.
if [ -z "${LUX_RENDERDOC_WAYLAND:-}" ] && [ -n "${DISPLAY:-}" ]; then
	unset WAYLAND_DISPLAY
	export XDG_SESSION_TYPE=x11
fi

json_escape() {
	printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

ARGS=""
for arg in "$@"; do
	ARGS="$ARGS $arg"
done
ARGS=${ARGS# }

CAPTURE_DIR="${XDG_RUNTIME_DIR:-/tmp}/luxengine"
mkdir -p "$CAPTURE_DIR"
SETTINGS="$CAPTURE_DIR/Editor-$CONFIG.cap"
cat > "$SETTINGS" <<EOF_CAP
{
    "rdocCaptureSettings": 1,
    "settings": {
        "autoStart": true,
        "commandLine": "$(json_escape "$ARGS")",
        "environment": [
        ],
        "executable": "$(json_escape "$EDITOR_BIN")",
        "inject": false,
        "numQueuedFrames": 0,
        "options": {
            "allowFullscreen": true,
            "allowVSync": true,
            "apiValidation": false,
            "captureAllCmdLists": false,
            "captureCallstacks": false,
            "captureCallstacksOnlyDraws": false,
            "debugOutputMute": true,
            "delayForDebugger": 0,
            "hookIntoChildren": false,
            "refAllResources": false,
            "softMemoryLimit": 0,
            "verifyBufferAccess": false
        },
        "queuedFrameCap": 0,
        "workingDir": "$(json_escape "$LUX_DIR/Editor")"
    }
}
EOF_CAP

echo "Launching $BUILD_CONFIG editor in RenderDoc ($SETTINGS)."
echo "Capture with F12 or Print Screen in the editor, or Trigger Capture in RenderDoc's Launch tab."
exec qrenderdoc "$SETTINGS"
