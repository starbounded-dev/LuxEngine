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

# Build Coral.Managed and Script Core
	premake5 vs2022 --file=Core/ScriptCore/premake5-dotnet.lua
	dotnet build -c $BUILD_CONFIG --property WarningLevel=0 Core/ScriptCore/Core-ScriptCore.sln

# Copy Coral Files
	CORAL_DIR=$LUX_DIR/Core/vendor/Coral
	LUX_DIR=$LUX_DIR
	DOTNET_DIR=$LUX_DIR/DotNet

	mkdir -p $DOTNET_DIR

	cp $CORAL_DIR/Coral.Managed/Coral.Managed.runtimeconfig.json $DOTNET_DIR/Coral.Managed.runtimeconfig.json
	cp $CORAL_DIR/Build/$BUILD_CONFIG/Coral.Managed.dll $DOTNET_DIR/Coral.Managed.dll
	cp $CORAL_DIR/Build/$BUILD_CONFIG/Coral.Managed.pdb $DOTNET_DIR/Coral.Managed.pdb
	cp $CORAL_DIR/Build/$BUILD_CONFIG/Coral.Managed.deps.json $DOTNET_DIR/Coral.Managed.deps.json

# Build Lux
	premake5 gmake --cc=clang --verbose
	make config=$(echo "$BUILD_CONFIG" | tr '[:upper:]' '[:lower:]') "$@"
