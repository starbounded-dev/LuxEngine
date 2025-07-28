#!/bin/sh

export STARENGINE_DIR=`realpath .`
export VULKAN_SDK=`realpath StarEngine/vendor/VulkanSDK/x86_64`
export LD_LIBRARY_PATH="$VULKAN_SDK/lib:$STARENGINE_DIR/StarEngine/vendor/assimp/bin/linux:$STARENGINE_DIR/StarEngine/vendor/NvidiaAftermath/lib/x64/linux"

cd StarEditor
$_EXEC $STARENGINE_DIR/bin/Debug-linux-x86_64/StarEditor/HazeStarEditorlnut $@
