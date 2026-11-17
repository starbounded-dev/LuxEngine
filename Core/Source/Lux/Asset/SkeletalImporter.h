// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Animation/Animation.h"
#include "Lux/Animation/Skeleton.h"
#include "Lux/Core/Base.h"
#include "Lux/Renderer/Mesh.h"

#include <assimp/postprocess.h>

#include <filesystem>
#include <vector>

struct aiScene;

namespace Lux
{
	// How the mesh importer reads a model. Skin weights are indexed by the vertex ids this produces
	// (JoinIdenticalVertices and OptimizeMeshes renumber them), so anything that reads skin data
	// separately must use the same flags.
	constexpr uint32_t k_MeshImportFlags =
		aiProcess_CalcTangentSpace |
		aiProcess_Triangulate |
		aiProcess_SortByPType |
		aiProcess_GenNormals |
		aiProcess_GenUVCoords |
		aiProcess_OptimizeMeshes |
		aiProcess_JoinIdenticalVertices |
		aiProcess_LimitBoneWeights |
		aiProcess_ValidateDataStructure |
		aiProcess_GlobalScale;

	// Reads every mesh's bone weights from a scene imported with k_MeshImportFlags. `meshBaseVertex[i]`
	// is where scene mesh i's first vertex sits in the combined vertex array of `totalVertices`.
	// influences comes back one per vertex (weights sum to 1; a vertex no bone touches keeps zero
	// weights) and boneInfo lists each distinct bone once, by name, with its inverse bind pose; both
	// stay empty when no mesh has bones. aiProcess_LimitBoneWeights already keeps each vertex's four heaviest bones. Returns false, leaving
	// the outputs empty, when `meshBaseVertex` does not match the scene's meshes.
	bool ExtractSkinWeights(const aiScene& scene, const std::vector<uint32_t>& meshBaseVertex, uint32_t totalVertices,
		std::vector<BoneInfluence>& influences, std::vector<BoneInfo>& boneInfo);

	// Reads a rigged model file (glTF first; anything Assimp reads) into a skeleton and its animation
	// clips. The skeleton is every bone node that a mesh references plus all their ancestors up to the
	// scene root (which animation leaves at their rest pose), so its model space is the scene's and
	// matches the meshes' inverse bind poses. outSkeleton stays null when the file has no bones,
	// outClips stays empty when it has no animations, and clip tracks index into outSkeleton (tracks
	// for nodes outside it are dropped). Needs no GPU. Returns false only if the file cannot be read.
	//
	// shortcut: no unit scale (FBX centimetres); the mesh importer applies aiProcess_GlobalScale to the
	// root node, which this skeleton includes, so a scaled FBX needs the same factor here when FBX
	// support lands.
	bool ImportSkeletonAndAnimations(const std::filesystem::path& path, Ref<Skeleton>& outSkeleton, std::vector<Ref<Animation>>& outClips);
}
