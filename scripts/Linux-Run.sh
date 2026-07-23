#!/bin/sh

export LUX_DIR=$(realpath .)
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
export VULKAN_SDK=$(realpath Core/vendor/VulkanSDK/x86_64)
export VK_LAYER_PATH="$VULKAN_SDK/share/vulkan/explicit_layer.d"
export PATH="$VULKAN_SDK/bin:$PATH"
export LD_LIBRARY_PATH="$VULKAN_SDK/lib:$LUX_DIR/Core/vendor/assimp/bin/linux:$LUX_DIR/Core/vendor/NvidiaAftermath/lib/x64/linux"

cd Editor
"$LUX_DIR/bin/$BUILD_CONFIG-linux-x86_64/Editor/Editor" "$@"
