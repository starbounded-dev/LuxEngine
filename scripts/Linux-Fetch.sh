#!/bin/sh

# TODO: Mark x perm.
set -x
set -e

# Thanks to https://stackoverflow.com/a/11114547/13771204
SCRIPT=$(realpath "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

if [ -n "${LUX_DIR+set}" ]
	then
		true
	else
		export LUX_DIR=$(realpath $SCRIPTPATH/..)
fi

if [ -n "${VULKAN_VERSION+set}" ]
	then
		true
	else
		export VULKAN_VERSION=1.4.304.0
fi

if [ -n "${PREMAKE_VERSION+set}" ]
	then
		true
	else
		export PREMAKE_VERSION=5.0.0-beta4
fi

VENDOR=$(realpath $LUX_DIR/Core/vendor/)

## Premake
	if [ -n "${BUILD_PREMAKE+set}" ]
		then
			RET=$(pwd)
			curl -Lo premake.zip https://github.com/premake/premake-core/releases/download/v$PREMAKE_VERSION/premake-$PREMAKE_VERSION-src.zip
			cd $LUX_DIR && unzip $LUX_DIR/premake.zip
			cd $LUX_DIR/premake-$PREMAKE_VERSION-src
			sh Bootstrap.sh
			mv bin/release/premake5 $LUX_DIR/premake5
			cd $RET
		else
			curl -L https://github.com/premake/premake-core/releases/download/v$PREMAKE_VERSION/premake-$PREMAKE_VERSION-linux.tar.gz | tar -x -z -f - -C $LUX_DIR
	fi

	chmod +x $LUX_DIR/premake5

## Vulkan SDK
	curl -L https://sdk.lunarg.com/sdk/download/$VULKAN_VERSION/linux/vulkansdk-linux-x86_64-$VULKAN_VERSION.tar.xz | tar -x -J -f - -C $VENDOR
	mv $VENDOR/$VULKAN_VERSION $VENDOR/VulkanSDK
