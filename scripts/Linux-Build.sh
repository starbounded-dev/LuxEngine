#!/bin/sh

set -e

if [ -n "${LUX_DIR+set}" ]
	then
		true
	else
		export LUX_DIR=$(realpath .)
fi

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

if [ -n "${VULKAN_SDK+set}" ]
	then
		true
	else
		export VULKAN_SDK=$(realpath Core/vendor/VulkanSDK/x86_64)
fi

# Build Coral.Managed (the C# host assembly) into Editor/DotNet, and the ScriptCore
# assembly into Editor/Resources/Scripts. Both are SDK-style net9.0 projects built with
# the dotnet CLI. ScriptCore references Coral.Managed, so building it also builds Coral.Managed.
	dotnet build -c Release --property WarningLevel=0 \
		Core/vendor/Coral/Coral.Managed/Coral.Managed-Static.csproj -o Editor/DotNet
	dotnet build -c $BUILD_CONFIG --property WarningLevel=0 ScriptCore/ScriptCore.csproj

# Ensure Coral build artifacts exist where the Core postbuild step expects them
	mkdir -p Core/vendor/Coral/Build/Release
	cp -f Editor/DotNet/Coral.Managed.dll Core/vendor/Coral/Build/Release/
	cp -f Editor/DotNet/Coral.Managed.runtimeconfig.json Core/vendor/Coral/Build/Release/ 2>/dev/null || true
	cp -f Editor/DotNet/Coral.Managed.deps.json Core/vendor/Coral/Build/Release/ 2>/dev/null || true

# Build Lux (skip the gmake ScriptCore target — it was already built above via dotnet)
	./premake5 gmake2 --cc=clang
	CONFIG=$(echo "$BUILD_CONFIG" | tr '[:upper:]' '[:lower:]')
	make config=$CONFIG Dependencies Dependencies/Renderer "$@"
	make -C Core -f Makefile config=$CONFIG
	make -C Editor -f Makefile config=$CONFIG
