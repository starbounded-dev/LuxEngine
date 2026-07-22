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
	else
		export BUILD_CONFIG=Debug
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

# Build Lux
	premake5 gmake --cc=clang --verbose
	make config=$(echo "$BUILD_CONFIG" | tr '[:upper:]' '[:lower:]') "$@"
