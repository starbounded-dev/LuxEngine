#!/bin/sh
#
# One-shot Linux build: submodules, C# projects, native engine, and the sample game's scripts.
# Safe to re-run; every step is idempotent.
#
#   ./scripts/Linux-Build.sh              # prompts for a configuration
#   ./scripts/Linux-Build.sh release      # non-interactive
#   JOBS=1 ./scripts/Linux-Build.sh dist  # serial build
#
# Extra arguments are forwarded to the first make invocation.

set -e

# Resolve the repo root from this script's own location so the script works from any directory.
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
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 1)}

step() { echo; echo "==> $1"; }

# ---------------------------------------------------------------------------
step "Checking prerequisites"

missing=""
for tool in dotnet make clang pkg-config curl tar; do
	command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
if [ -n "$missing" ]; then
	echo "Missing required tool(s):$missing"
	echo "On Arch: sudo pacman -S --needed dotnet-sdk make clang pkgconf curl tar"
	exit 1
fi

if ! pkg-config --exists gtk+-3.0; then
	echo "gtk+-3.0 development files not found (needed by NFD-Extended for file dialogs)."
	echo "On Arch: sudo pacman -S --needed gtk3"
	exit 1
fi

# ---------------------------------------------------------------------------
step "Syncing submodules"

if [ -d .git ] && command -v git >/dev/null 2>&1; then
	git submodule update --init --recursive
else
	echo "Not a git checkout - skipping."
fi

if [ -n "${VULKAN_SDK+set}" ]
	then
		true
	else
		export VULKAN_SDK="$LUX_DIR/Core/vendor/VulkanSDK/x86_64"
fi

if [ ! -d "$VULKAN_SDK" ]; then
	echo "Vulkan SDK not found at: $VULKAN_SDK"
	echo "Set VULKAN_SDK to a valid SDK, or check out the vendored one."
	exit 1
fi

# ---------------------------------------------------------------------------
step "Locating premake5"

# Only the Windows binary (vendor/bin/premake5.exe) is committed, and the repo gitignores any
# file named "premake5" - so a clean checkout has no Linux premake at all. Fetch a pinned build
# on first run and verify it, rather than depending on whatever happens to be on the machine.
PREMAKE_VERSION=5.0.0-beta4
PREMAKE_SHA256=4356ab7cdec6085183d68fb240089376eacdc2fb751ffbd8063d797ae43abeb3

if [ -x "$LUX_DIR/premake5" ]; then
	PREMAKE="$LUX_DIR/premake5"
	echo "Using $PREMAKE"
elif [ -x "$LUX_DIR/vendor/bin/premake5" ]; then
	PREMAKE="$LUX_DIR/vendor/bin/premake5"
	echo "Using $PREMAKE"
else
	echo "No Linux premake5 found - downloading $PREMAKE_VERSION"
	pm_tmp=$(mktemp -d)
	pm_url="https://github.com/premake/premake-core/releases/download/v$PREMAKE_VERSION/premake-$PREMAKE_VERSION-linux.tar.gz"

	if ! curl -sSL --retry 3 --max-time 180 -o "$pm_tmp/premake.tar.gz" "$pm_url"; then
		echo "Failed to download premake5 from: $pm_url"
		rm -rf "$pm_tmp"
		exit 1
	fi

	tar xzf "$pm_tmp/premake.tar.gz" -C "$pm_tmp" premake5
	pm_sha=$(sha256sum "$pm_tmp/premake5" | awk '{print $1}')
	if [ "$pm_sha" != "$PREMAKE_SHA256" ]; then
		echo "premake5 checksum mismatch - refusing to use it."
		echo "  expected $PREMAKE_SHA256"
		echo "  actual   $pm_sha"
		rm -rf "$pm_tmp"
		exit 1
	fi

	mkdir -p "$LUX_DIR/vendor/bin"
	mv "$pm_tmp/premake5" "$LUX_DIR/vendor/bin/premake5"
	chmod +x "$LUX_DIR/vendor/bin/premake5"
	rm -rf "$pm_tmp"
	PREMAKE="$LUX_DIR/vendor/bin/premake5"
	echo "Installed $PREMAKE"
fi

# ---------------------------------------------------------------------------
step "Generating C# projects"

# premake's gmake2 C# generator shells out to `csc`, which the .NET SDK does not put on PATH,
# so use the vs2022 action instead - it emits SDK-style .csproj files that `dotnet build`
# consumes on any platform. --os=linux is required: without it os.target() reports "windows"
# and Coral's nethost probe looks for win-* runtime packs and aborts.
"$PREMAKE" --os=linux vs2022

# ---------------------------------------------------------------------------
step "Building managed assemblies ($BUILD_CONFIG)"

# Coral.Managed is the C# host assembly; ScriptCore is the scripting API that games reference.
dotnet build -c Release --property WarningLevel=0 \
	Core/vendor/Coral/Coral.Managed/Coral.Managed-Static.csproj -o Editor/DotNet
dotnet build -c "$BUILD_CONFIG" --property WarningLevel=0 ScriptCore/ScriptCore.csproj

# Core's postbuild step expects Coral's artifacts here.
mkdir -p Core/vendor/Coral/Build/Release
cp -f Editor/DotNet/Coral.Managed.dll Core/vendor/Coral/Build/Release/
cp -f Editor/DotNet/Coral.Managed.runtimeconfig.json Core/vendor/Coral/Build/Release/ 2>/dev/null || true
cp -f Editor/DotNet/Coral.Managed.deps.json Core/vendor/Coral/Build/Release/ 2>/dev/null || true

# ---------------------------------------------------------------------------
step "Building engine ($BUILD_CONFIG, -j$JOBS)"

# ScriptCore is skipped by the gmake action on Linux (built above via dotnet).
"$PREMAKE" gmake2 --cc=clang
make -j"$JOBS" config="$CONFIG" Dependencies Dependencies/Renderer "$@"
make -j"$JOBS" -C Core -f Makefile config="$CONFIG"
make -j"$JOBS" -C Editor -f Makefile config="$CONFIG"
make -j"$JOBS" -C Lux-Runtime -f Makefile config="$CONFIG"

# ---------------------------------------------------------------------------
# The sample game's scripts are a standalone premake workspace, so the root generation above
# does not cover them. Without this the editor reports "Script project file not found" on a
# fresh clone, because the .csproj is generated output and is not checked in.
SAMPLE_SCRIPTS="$LUX_DIR/Editor/LuxSampleProject/Assets/Scripts"
if [ -f "$SAMPLE_SCRIPTS/premake5.lua" ]; then
	step "Building sample project scripts ($BUILD_CONFIG)"
	"$SAMPLE_SCRIPTS/Linux-GenProjects.sh" >/dev/null
	dotnet build -c "$BUILD_CONFIG" --property WarningLevel=0 "$SAMPLE_SCRIPTS/LuxSample.csproj"
fi

# ---------------------------------------------------------------------------
echo
echo "==> Done ($BUILD_CONFIG)"
echo "    Editor:  ./scripts/Linux-Run.sh $CONFIG"
echo "    Runtime: ./scripts/Linux-RunRuntime.sh $CONFIG"
