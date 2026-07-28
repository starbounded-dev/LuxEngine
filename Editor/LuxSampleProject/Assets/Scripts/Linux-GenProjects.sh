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
PREMAKE="$(cd "$SCRIPT_DIR/../../../.." && pwd)/premake5"

if [ ! -x "$PREMAKE" ]; then
	echo "premake5 not found or not executable at: $PREMAKE"
	exit 1
fi

cd "$SCRIPT_DIR"
"$PREMAKE" vs2022

echo "Generated LuxSample.csproj. Build it with:"
echo "  dotnet build -c Release \"$SCRIPT_DIR/LuxSample.csproj\""
