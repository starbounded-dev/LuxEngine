#!/bin/sh
# Linux counterpart to Win-GenProjects.bat.
#
# The "vs2022" action is not Windows-only here: premake emits an SDK-style .csproj
# (<Project Sdk="Microsoft.NET.Sdk">) that `dotnet build` consumes on any platform.
# The gmake2 action is deliberately NOT used for C# - it shells out to csc, which the
# .NET SDK does not put on PATH.
#
# The editor's ScriptBuilder runs `dotnet build` on the generated LuxSample.csproj.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

# Only vendor/bin/premake5.exe is committed, so the Linux binary is either a local build at the
# repo root or the pinned one that scripts/Linux-Build.sh downloads into vendor/bin.
if [ -x "$ROOT/premake5" ]; then
	PREMAKE="$ROOT/premake5"
elif [ -x "$ROOT/vendor/bin/premake5" ]; then
	PREMAKE="$ROOT/vendor/bin/premake5"
else
	echo "No Linux premake5 found (looked in $ROOT and $ROOT/vendor/bin)."
	echo "Run ./scripts/Linux-Build.sh once - it downloads a pinned build."
	exit 1
fi

cd "$SCRIPT_DIR"
"$PREMAKE" vs2022

echo "Generated LuxSample.csproj. Build it with:"
echo "  dotnet build -c Release \"$SCRIPT_DIR/LuxSample.csproj\""
