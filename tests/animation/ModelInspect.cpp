// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
//
// Prints what the engine's skeletal importer makes of a model file (any format Assimp reads, FBX binary
// included): the skeleton, the clips, and the skin weights read the way the mesh importer reads them.
// A tool for new rigs, not a test: run it with tests/animation/inspect_model.py.

#include "lpch.h"
#include <spdlog/sinks/stdout_color_sinks.h>

#include "Lux/Animation/SkinPalette.h"
#include "Lux/Asset/SkeletalImporter.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>

namespace Lux
{
	std::shared_ptr<spdlog::logger> Log::s_CoreLogger = spdlog::stdout_color_mt("inspect");
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger = Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_EditorConsoleLogger = Log::s_CoreLogger;
}
using namespace Lux;

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::cerr << "usage: ModelInspect <model file>\n";
		return 2;
	}
	const std::filesystem::path path = argv[1];

	Ref<Skeleton> skeleton;
	std::vector<Ref<Animation>> clips;
	if (!ImportSkeletonAndAnimations(path, skeleton, clips))
	{
		std::printf("IMPORT FAILED\n");
		return 1;
	}

	// The scene again, with the mesh importer's flags: the vertices and bones it sees.
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path.string(), k_MeshImportFlags);
	if (!scene)
	{
		std::printf("MESH IMPORT FAILED: %s\n", importer.GetErrorString());
		return 1;
	}
	uint32_t vertices = 0;
	glm::vec3 low(1e30f), high(-1e30f);
	std::vector<uint32_t> bases;
	for (uint32_t m = 0; m < scene->mNumMeshes; m++)
	{
		bases.push_back(vertices);
		vertices += scene->mMeshes[m]->mNumVertices;
		for (uint32_t v = 0; v < scene->mMeshes[m]->mNumVertices; v++)
		{
			const aiVector3D& p = scene->mMeshes[m]->mVertices[v];
			low = glm::min(low, glm::vec3(p.x, p.y, p.z));
			high = glm::max(high, glm::vec3(p.x, p.y, p.z));
		}
	}
	std::printf("meshes %u, vertices %u, bounds (%.3f %.3f %.3f) .. (%.3f %.3f %.3f)\n", scene->mNumMeshes, vertices, low.x, low.y, low.z, high.x, high.y, high.z);

	// Scene metadata (FBX keeps its unit scale here) and the top of the node tree.
	if (scene->mMetaData)
	{
		for (unsigned int i = 0; i < scene->mMetaData->mNumProperties; i++)
		{
			const aiString& key = scene->mMetaData->mKeys[i];
			const aiMetadataEntry& entry = scene->mMetaData->mValues[i];
			if (entry.mType == AI_DOUBLE)
				std::printf("meta %s = %g\n", key.C_Str(), *static_cast<double*>(entry.mData));
			else if (entry.mType == AI_FLOAT)
				std::printf("meta %s = %g\n", key.C_Str(), *static_cast<float*>(entry.mData));
			else if (entry.mType == AI_INT32)
				std::printf("meta %s = %d\n", key.C_Str(), *static_cast<int32_t*>(entry.mData));
		}
	}
	std::function<void(const aiNode*, int)> dumpNode = [&](const aiNode* node, int depth)
	{
		if (depth > 3)
			return;
		aiVector3D scale, position;
		aiQuaternion rotation;
		node->mTransformation.Decompose(scale, rotation, position);
		std::printf("%*snode '%s' T(%.3f %.3f %.3f) S(%.3f %.3f %.3f) meshes %u\n", depth * 2, "", node->mName.C_Str(), position.x, position.y, position.z, scale.x, scale.y, scale.z, node->mNumMeshes);
		aiMatrix4x4 world = node->mTransformation;
		for (const aiNode* up = node->mParent; up; up = up->mParent)
			world = up->mTransformation * world;
		std::printf("%*s  world rows (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f)\n", depth * 2, "", world.a1, world.a2, world.a3, world.b1, world.b2, world.b3, world.c1, world.c2, world.c3);
		for (unsigned int i = 0; i < node->mNumChildren; i++)
			dumpNode(node->mChildren[i], depth + 1);
	};
	dumpNode(scene->mRootNode, 0);

	std::vector<BoneInfluence> influences;
	std::vector<BoneInfo> meshBones;
	ExtractSkinWeights(*scene, bases, vertices, influences, meshBones);
	std::printf("skin: %zu mesh bones, %zu weighted vertices\n", meshBones.size(), influences.size());

	if (!skeleton)
	{
		std::printf("no skeleton\n");
		return 0;
	}

	uint32_t roots = 0, maxDepth = 0;
	for (uint32_t i = 0; i < skeleton->GetNumBones(); i++)
	{
		uint32_t depth = 0;
		for (uint32_t b = i; skeleton->GetParentBoneIndex(b) != Skeleton::NullIndex; b = skeleton->GetParentBoneIndex(b))
			depth++;
		roots += skeleton->GetParentBoneIndex(i) == Skeleton::NullIndex;
		maxDepth = std::max(maxDepth, depth);
	}
	std::printf("skeleton: %u bones, %u root(s), depth %u\n", skeleton->GetNumBones(), roots, maxDepth);
	for (uint32_t i = 0; i < skeleton->GetNumBones() && i < 12; i++)
	{
		const glm::vec3& t = skeleton->GetBoneTranslations()[i];
		const glm::vec3& s = skeleton->GetBoneScales()[i];
		std::printf("  [%u] '%s' parent %d  T(%.3f %.3f %.3f) S(%.3f %.3f %.3f)\n", i, skeleton->GetBoneName(i).c_str(),
			skeleton->GetParentBoneIndex(i) == Skeleton::NullIndex ? -1 : (int)skeleton->GetParentBoneIndex(i), t.x, t.y, t.z, s.x, s.y, s.z);
	}

	// Every mesh bone resolves in the skeleton, and the rest skinning matrices agree with each other.
	uint32_t unresolved = 0;
	for (const BoneInfo& bone : meshBones)
		unresolved += skeleton->GetBoneIndex(bone.Name) == Skeleton::NullIndex;
	const std::vector<glm::mat4> palette = BuildSkinPalette(*skeleton, BuildModelPose(*skeleton), meshBones);
	for (size_t i = 0; i < palette.size() && i < 6; i++)
	{
		const glm::mat4& m = palette[i];
		std::printf("  palette[%zu] '%s': col0 len %.4f col1 len %.4f col2 len %.4f  T(%.4f %.4f %.4f)  col0(%.3f %.3f %.3f)\n", i, meshBones[i].Name.c_str(), glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])), m[3][0], m[3][1], m[3][2], m[0][0], m[0][1], m[0][2]);
	}
	float worst = 0.0f;
	for (const glm::mat4& matrix : palette)
	{
		for (int c = 0; c < 4; c++)
			for (int r = 0; r < 4; r++)
				worst = std::max(worst, std::abs(matrix[c][r] - palette.front()[c][r]));
	}
	std::printf("mesh bones missing from the skeleton: %u; rest palette spread (0 = every bone agrees): %.5f\n", unresolved, worst);
	if (!palette.empty())
	{
		const glm::mat4& m = palette.front();
		std::printf("rest palette[0]: row0 (%.3f %.3f %.3f %.3f) row1 (%.3f %.3f %.3f %.3f) row2 (%.3f %.3f %.3f %.3f)\n",
			m[0][0], m[1][0], m[2][0], m[3][0], m[0][1], m[1][1], m[2][1], m[3][1], m[0][2], m[1][2], m[2][2], m[3][2]);
	}

	float weightSum = 1.0f, weightSumHigh = 0.0f;
	uint32_t unweighted = 0;
	for (const BoneInfluence& influence : influences)
	{
		const float sum = influence.Weights[0] + influence.Weights[1] + influence.Weights[2] + influence.Weights[3];
		unweighted += sum < 0.5f;
		weightSum = std::min(weightSum, sum);
		weightSumHigh = std::max(weightSumHigh, sum);
	}
	std::printf("weights: sum per vertex %.4f .. %.4f, %u unweighted vertices\n", weightSum, weightSumHigh, unweighted);

	std::printf("clips: %zu\n", clips.size());
	for (const Ref<Animation>& clip : clips)
	{
		size_t minKeys = SIZE_MAX, maxKeys = 0;
		for (const BoneTrack& track : clip->GetTracks())
		{
			const size_t keys = std::max({ track.Translations.size(), track.Rotations.size(), track.Scales.size() });
			minKeys = std::min(minKeys, keys);
			maxKeys = std::max(maxKeys, keys);
		}
		for (const BoneTrack& track : clip->GetTracks())
		{
			if (skeleton->GetBoneName(track.BoneIndex).find("Hips") == std::string::npos || track.Translations.empty())
				continue;
			const glm::vec3& rest = skeleton->GetBoneTranslations()[track.BoneIndex];
			const glm::vec3& key = track.Translations[track.Translations.size() / 2].Value;
			std::printf("    hips: rest T(%.4f %.4f %.4f)  mid key T(%.4f %.4f %.4f)\n", rest.x, rest.y, rest.z, key.x, key.y, key.z);
		}
		std::printf("  '%s': %.3f s, %zu tracks, %u keys, keys per track %zu..%zu\n", clip->GetName().c_str(), clip->GetDuration(),
			clip->GetTracks().size(), clip->GetNumKeys(), clip->GetTracks().empty() ? 0 : minKeys, maxKeys);
	}
	return 0;
}
