#!/bin/sh

set -e

if [ -n "${STARENGINE_DIR+set}" ]
	then
		true
	else
		export STARENGINE_DIR=$(realpath .)
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
		export VULKAN_SDK=$(realpath StarEngine/vendor/VulkanSDK/x86_64)
fi

# Build Coral.Managed and Script Core
	premake5 vs2022 --file=StarEngine-ScriptCore/premake5-dotnet.lua
	dotnet build -c $BUILD_CONFIG --property WarningLevel=0 StarEngine-ScriptCore/StarEngine-ScriptCore.sln

# Copy Coral Files
	CORAL_DIR=$STARENGINE_DIR/StarEngine/vendor/Coral
	STAREDITOR_DIR=$STARENGINE_DIR/StarEditor
	DOTNET_DIR=$STAREDITOR_DIR/DotNet

	mkdir -p $DOTNET_DIR

	cp $CORAL_DIR/Coral.Managed/Coral.Managed.runtimeconfig.json $DOTNET_DIR/Coral.Managed.runtimeconfig.json
	cp $CORAL_DIR/Build/$BUILD_CONFIG/Coral.Managed.dll $DOTNET_DIR/Coral.Managed.dll
	cp $CORAL_DIR/Build/$BUILD_CONFIG/Coral.Managed.pdb $DOTNET_DIR/Coral.Managed.pdb
	cp $CORAL_DIR/Build/$BUILD_CONFIG/Coral.Managed.deps.json $DOTNET_DIR/Coral.Managed.deps.json

# Build Sandbox
	premake5 vs2022 --file=StarEditor/SandboxProject/premake5.lua
	dotnet build -c $BUILD_CONFIG --property WarningLevel=0 StarEditor/SandboxProject/Sandbox.sln

# Build Hazel
	premake5 gmake2 --cc=clang --verbose
	make config=$(echo "$BUILD_CONFIG" | tr '[:upper:]' '[:lower:]') "$@"
