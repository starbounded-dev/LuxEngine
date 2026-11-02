#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025-2026 starbounded-dev
# Build-time deployment of the runtime's resolved vendored ELF dependencies.
set -euo pipefail

if [ "$#" -ne 2 ]; then
	echo "Usage: Stage-LinuxRuntimeLibraries.sh <runtime-executable> <runtime-lib-directory>" >&2
	exit 2
fi

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
repository_directory=$(CDPATH= cd -- "$script_directory/.." && pwd -P)
if ! runtime_binary=$(readlink -f -- "$1"); then
	echo "Runtime dependency deployment: cannot resolve executable: $1" >&2
	exit 1
fi
if [ ! -f "$runtime_binary" ]; then
	echo "Runtime dependency deployment: executable not found: $1" >&2
	exit 1
fi
mkdir -p -- "$2"
stage_directory=$(CDPATH= cd -- "$2" && pwd -P)

# Prefer current vendored inputs to previously staged copies on incremental builds.
vendor_directories=()
search_path=""
for candidate in "$repository_directory/Core/vendor/assimp/bin/linux" \
				"$repository_directory/Core/vendor/NvidiaAftermath/lib/x64/linux" \
				"${VULKAN_SDK:-}/lib"; do
	if [ -d "$candidate" ] && [ "$candidate" != "/lib" ]; then
		physical_directory=$(CDPATH= cd -- "$candidate" && pwd -P)
		vendor_directories+=("$physical_directory")
		search_path="${search_path:+$search_path:}$physical_directory"
	fi
done
search_path="${search_path:+$search_path:}$stage_directory"
if ! dependencies=$(LD_LIBRARY_PATH="$search_path${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$runtime_binary" 2>&1); then
	printf 'Runtime dependency deployment: cannot inspect %s\n%s\n' "$runtime_binary" "$dependencies" >&2
	exit 1
fi
if [[ "$dependencies" == *"=> not found"* ]]; then
	printf 'Runtime dependency deployment: unresolved dependency in %s\n%s\n' "$runtime_binary" "$dependencies" >&2
	exit 1
fi

while IFS= read -r entry; do
	[[ "$entry" == *" => "* ]] || continue
	resolved=${entry#* => }
	resolved=${resolved% \(0x*}
	[[ "$resolved" == /* ]] || continue
	canonical=$(readlink -f -- "$resolved")
	for vendor_directory in "${vendor_directories[@]}"; do
		case "$canonical" in
			"$vendor_directory"/*)
				destination="$stage_directory/$(basename -- "$resolved")"
				if [ "$canonical" != "$destination" ]; then
					cp -fL --remove-destination -- "$resolved" "$destination"
				fi
				printf 'Staged runtime dependency: %s\n' "$destination"
				break
				;;
		esac
	done
done <<< "$dependencies"

# Reject source-tree paths masking a broken package. System desktop libraries remain system
# dependencies; SDK/vendor binaries must resolve from the staged lib directory.
if ! staged_dependencies=$(LD_LIBRARY_PATH="$stage_directory" ldd "$runtime_binary" 2>&1); then
	printf 'Runtime dependency deployment: staged dependency check failed\n%s\n' "$staged_dependencies" >&2
	exit 1
fi
if [[ "$staged_dependencies" == *"=> not found"* ]]; then
	printf 'Runtime dependency deployment: staged dependency is missing\n%s\n' "$staged_dependencies" >&2
	exit 1
fi
while IFS= read -r entry; do
	[[ "$entry" == *" => "* ]] || continue
	resolved=${entry#* => }
	resolved=${resolved% \(0x*}
	[[ "$resolved" == /* ]] || continue
	canonical=$(readlink -f -- "$resolved")
	case "$canonical" in "$stage_directory"/*) continue ;; esac
	for vendor_directory in "${vendor_directories[@]}"; do
		case "$canonical" in
			"$vendor_directory"/*)
				printf 'Runtime dependency deployment: still resolves outside staged lib: %s\n' "$resolved" >&2
				exit 1
				;;
		esac
	done
done <<< "$staged_dependencies"
echo "Runtime vendored dependencies are staged and resolve without build-tree library paths."
