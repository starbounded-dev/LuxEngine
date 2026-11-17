// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SkeletalImporter.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <string>
#include <unordered_set>

namespace Lux
{
	namespace
	{
		bool IsBoneNode(const aiNode* node, const std::unordered_set<std::string>& boneNames)
		{
			return boneNames.contains(node->mName.C_Str());
		}

		bool HasBoneInSubtree(const aiNode* node, const std::unordered_set<std::string>& boneNames)
		{
			if (IsBoneNode(node, boneNames))
				return true;
			for (uint32_t i = 0; i < node->mNumChildren; i++)
			{
				if (HasBoneInSubtree(node->mChildren[i], boneNames))
					return true;
			}
			return false;
		}

		// Adds `node` and its descendants. Nodes above the topmost bone are skipped (the mesh already
		// carries them); inside the skeleton, a node joins when it or something below it is a bone.
		void AddSkeletonNodes(Skeleton& skeleton, const aiNode* node, uint32_t parentIndex, bool insideSkeleton,
			const std::unordered_set<std::string>& boneNames)
		{
			insideSkeleton = insideSkeleton || IsBoneNode(node, boneNames);
			uint32_t index = parentIndex;
			if (insideSkeleton && HasBoneInSubtree(node, boneNames))
			{
				aiVector3D scale, position;
				aiQuaternion rotation;
				node->mTransformation.Decompose(scale, rotation, position);
				index = skeleton.AddBone(node->mName.C_Str(), parentIndex, { position.x, position.y, position.z },
					glm::normalize(glm::quat(rotation.w, rotation.x, rotation.y, rotation.z)), { scale.x, scale.y, scale.z });
				if (index == Skeleton::NullIndex)
				{
					LUX_CORE_WARN("SkeletalImporter: skipped bone node '{}' (empty or duplicate name, or too many bones)", node->mName.C_Str());
					return;
				}
			}
			for (uint32_t i = 0; i < node->mNumChildren; i++)
				AddSkeletonNodes(skeleton, node->mChildren[i], index, insideSkeleton, boneNames);
		}

		template<typename Key, typename AiKey, typename Convert>
		void ReadChannel(std::vector<Key>& out, const AiKey* keys, uint32_t count, double ticksPerSecond, Convert convert)
		{
			out.reserve(count);
			for (uint32_t i = 0; i < count; i++)
			{
				Key key;
				key.Time = static_cast<float>(keys[i].mTime / ticksPerSecond);
				key.Value = convert(keys[i].mValue);
				out.push_back(key);
			}
			std::stable_sort(out.begin(), out.end(), [](const Key& a, const Key& b) { return a.Time < b.Time; });
		}
	}

	bool ImportSkeletonAndAnimations(const std::filesystem::path& path, Ref<Skeleton>& outSkeleton, std::vector<Ref<Animation>>& outClips)
	{
		outSkeleton = nullptr;
		outClips.clear();

		Assimp::Importer importer;
		importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
		const aiScene* scene = importer.ReadFile(path.string(), aiProcess_Triangulate | aiProcess_LimitBoneWeights | aiProcess_ValidateDataStructure);
		if (!scene || !scene->mRootNode)
		{
			LUX_CORE_ERROR("SkeletalImporter: failed to read '{}': {}", path.string(), importer.GetErrorString());
			return false;
		}

		std::unordered_set<std::string> boneNames;
		for (uint32_t m = 0; m < scene->mNumMeshes; m++)
		{
			for (uint32_t b = 0; b < scene->mMeshes[m]->mNumBones; b++)
				boneNames.insert(scene->mMeshes[m]->mBones[b]->mName.C_Str());
		}

		if (!boneNames.empty())
		{
			Ref<Skeleton> skeleton = Ref<Skeleton>::Create();
			AddSkeletonNodes(*skeleton, scene->mRootNode, Skeleton::NullIndex, false, boneNames);
			skeleton->CalculateDerivedData();
			if (!skeleton->IsEmpty())
				outSkeleton = skeleton;
		}

		if (!outSkeleton)
			return true; // clips need a skeleton to index into

		for (uint32_t a = 0; a < scene->mNumAnimations; a++)
		{
			const aiAnimation* source = scene->mAnimations[a];
			const double ticksPerSecond = source->mTicksPerSecond > 0.0 ? source->mTicksPerSecond : 25.0;

			Ref<Animation> clip = Ref<Animation>::Create();
			clip->SetName(source->mName.length > 0 ? source->mName.C_Str() : "Animation_" + std::to_string(a));

			float duration = static_cast<float>(source->mDuration / ticksPerSecond);
			uint32_t dropped = 0;
			for (uint32_t c = 0; c < source->mNumChannels; c++)
			{
				const aiNodeAnim* channel = source->mChannels[c];
				const uint32_t boneIndex = outSkeleton->GetBoneIndex(channel->mNodeName.C_Str());
				if (boneIndex == Skeleton::NullIndex)
				{
					dropped++;
					continue;
				}

				BoneTrack track;
				track.BoneIndex = boneIndex;
				ReadChannel(track.Translations, channel->mPositionKeys, channel->mNumPositionKeys, ticksPerSecond,
					[](const aiVector3D& v) { return glm::vec3(v.x, v.y, v.z); });
				ReadChannel(track.Rotations, channel->mRotationKeys, channel->mNumRotationKeys, ticksPerSecond,
					[](const aiQuaternion& q) { return glm::normalize(glm::quat(q.w, q.x, q.y, q.z)); });
				ReadChannel(track.Scales, channel->mScalingKeys, channel->mNumScalingKeys, ticksPerSecond,
					[](const aiVector3D& v) { return glm::vec3(v.x, v.y, v.z); });

				if (!track.Translations.empty()) duration = glm::max(duration, track.Translations.back().Time);
				if (!track.Rotations.empty()) duration = glm::max(duration, track.Rotations.back().Time);
				if (!track.Scales.empty()) duration = glm::max(duration, track.Scales.back().Time);
				clip->GetTracksMutable().push_back(std::move(track));
			}
			clip->SetDuration(duration);

			if (dropped > 0)
				LUX_CORE_WARN("SkeletalImporter: animation '{}' in '{}' animates {} node(s) outside the skeleton; those tracks were dropped", clip->GetName(), path.filename().string(), dropped);
			outClips.push_back(clip);
		}

		LUX_CORE_INFO("SkeletalImporter: '{}' - {} bones, {} animation(s)", path.filename().string(), outSkeleton->GetNumBones(), outClips.size());
		return true;
	}
}
