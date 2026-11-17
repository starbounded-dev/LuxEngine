// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Animation/Animation.h"
#include "Lux/Animation/Skeleton.h"
#include "Lux/Core/Base.h"

#include <filesystem>
#include <vector>

namespace Lux
{
	// Reads a rigged model file (glTF first; anything Assimp reads) into a skeleton and its animation
	// clips. The skeleton is every bone node that a mesh references plus the nodes between them, with
	// nodes above the topmost bone left to the mesh. outSkeleton stays null when the file has no bones,
	// outClips stays empty when it has no animations, and clip tracks index into outSkeleton (tracks
	// for nodes outside it are dropped). Needs no GPU. Returns false only if the file cannot be read.
	//
	// shortcut: no unit scale (FBX centimetres); the mesh importer applies aiProcess_GlobalScale to the
	// node tree above the bones, so a scaled FBX needs the same factor here when FBX support lands.
	bool ImportSkeletonAndAnimations(const std::filesystem::path& path, Ref<Skeleton>& outSkeleton, std::vector<Ref<Animation>>& outClips);
}
