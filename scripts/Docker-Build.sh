#!/bin/sh

# TODO(Emily): This file does not respect `HAZEL_DIR` atm.

set -e

NAME=starengine:latest

if [ -n "${BUILD_CONFIG+set}" ]
	then
		true
	else
		export BUILD_CONFIG=Debug
fi

if [ -n "${ARCH+set}" ]
	then
		true
	else
		export ARCH=$(uname -m)
fi

if [ -n "${VULKAN_VER+set}" ]
	then
		true
	else
		export VULKAN_VER=1.3.283.0
fi

docker build --build-arg BUILD_CONFIG=$BUILD_CONFIG --build-arg VULKAN_VER=$VULKAN_VER -t $NAME "$@" .

# Copy out the artifacts we want to keep
mkdir -p bin/$ARCH/bin
mkdir -p StarEditor/Resources/Scripts
mkdir -p StarEditor/SandboxProject/Assets/Scripts/Binaries
IMG=$(docker create $NAME)
WKS=$IMG:/workspace
	docker cp $WKS/bin/$BUILD_CONFIG-linux-$ARCH/StarEditor/StarEditor bin/StarEditor
	#docker cp $WKS/bin/$BUILD_CONFIG-linux-$ARCH/StarEngine-Launcher/StarEngine-Launcher bin/StarEngine-Launcher
	#docker cp $WKS/bin/$BUILD_CONFIG-linux-$ARCH/StarEngine-Runtime/StarEngine-Runtime bin/StarEngine-Runtime

	docker cp $WKS/$VULKAN_VER/$ARCH/lib/libdxcompiler.so bin/
	docker cp $WKS/$VULKAN_VER/$ARCH/bin/dxc bin/$ARCH/bin/
	docker cp $WKS/$VULKAN_VER/$ARCH/bin/dxc-3.7 bin/$ARCH/bin/
	#docker cp $WKS/StarEngine/vendor/NvidiaAftermath/lib/x64/linux/libGFSDK_Aftermath_Lib.so bin/
	#docker cp $WKS/StarEngine/vendor/NvidiaAftermath/lib/x64/linux/libGFSDK_Aftermath_Lib.x64.so bin/
	#docker cp $WKS/StarEngine/vendor/assimp/bin/linux/libassimp.so bin/
	#docker cp $WKS/StarEngine/vendor/assimp/bin/linux/libassimp.so.5 bin/

	docker cp $WKS/StarEditor/DotNet StarEditor/DotNet
	docker cp $WKS/StarEditor/Resources/Scripts/StarEngine-ScriptCore.dll StarEditor/Resources/Scripts/StarEngine-ScriptCore.dll

	docker cp $WKS/StarEditor/SandboxProject/Assets/Scripts/Binaries/Sandbox.dll StarEditor/SandboxProject/Assets/Scripts/Binaries/Sandbox.dll
docker rm $IMG
