#!/bin/sh

export LUX_DIR=$(realpath .)
export BUILD_CONFIG=${BUILD_CONFIG:-Debug}
export VULKAN_SDK=$(realpath Core/vendor/VulkanSDK/x86_64)
export LD_LIBRARY_PATH="$VULKAN_SDK/lib:$LUX_DIR/Core/vendor/assimp/bin/linux:$LUX_DIR/Core/vendor/NvidiaAftermath/lib/x64/linux"

cd Editor
"$LUX_DIR/bin/$BUILD_CONFIG-linux-x86_64/Editor/Editor" "$@"
