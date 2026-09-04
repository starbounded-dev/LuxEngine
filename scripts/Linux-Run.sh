#!/bin/sh
#
# Runs the editor, first making sure the build is current.
#
#   ./scripts/Linux-Run.sh              # prompts for a configuration
#   ./scripts/Linux-Run.sh release      # non-interactive
#   LUX_SKIP_BUILD=1 ./scripts/Linux-Run.sh debug   # skip the rebuild check, run as-is
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

cd "$LUX_DIR/Editor"
"$LUX_DIR/bin/$BUILD_CONFIG-linux-x86_64/Editor/Editor" "$@"
