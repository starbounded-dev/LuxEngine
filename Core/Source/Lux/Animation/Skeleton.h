// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
//
// The skeleton data model follows the layout of Lucky Engine's Hazel-derived Skeleton
// (Apache-2.0): bones in parent-before-child order, with a local rest pose per bone.

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Serialization/StreamReader.h"
#include "Lux/Serialization/StreamWriter.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Lux
{
	// A hierarchy of bones (joints) and their rest pose. Bones are stored parent-before-child, so a
	// bone's parent index is always lower than its own and a single forward pass visits parents first.
	// Several roots are allowed (parent == NullIndex).
	class Skeleton : public Asset
	{
	public:
		static constexpr uint32_t NullIndex = ~0u;
		static constexpr uint32_t MaxBones = 4096; // bounds what a file may claim; the skinning pass caps lower

		static AssetType GetStaticType() { return AssetType::Skeleton; }
		AssetType GetAssetType() const override { return GetStaticType(); }

		// Returns the new bone's index, or NullIndex when parentIndex is not an earlier bone, the
		// name is empty or already used, or MaxBones is reached. Call CalculateDerivedData() when done.
		uint32_t AddBone(std::string name, uint32_t parentIndex, const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale);

		// Model-space rest matrices and their inverses. Required before the Get*ModelSpace* accessors.
		void CalculateDerivedData();

		bool IsEmpty() const { return m_BoneNames.empty(); }
		uint32_t GetNumBones() const { return static_cast<uint32_t>(m_BoneNames.size()); }
		uint32_t GetBoneIndex(std::string_view name) const; // NullIndex if absent

		const std::string& GetBoneName(uint32_t boneIndex) const { return m_BoneNames[boneIndex]; }
		uint32_t GetParentBoneIndex(uint32_t boneIndex) const { return m_ParentBoneIndices[boneIndex]; }
		const std::vector<std::string>& GetBoneNames() const { return m_BoneNames; }
		const std::vector<uint32_t>& GetParentBoneIndices() const { return m_ParentBoneIndices; }

		// Rest pose in bone space (relative to the parent).
		const std::vector<glm::vec3>& GetBoneTranslations() const { return m_BoneTranslations; }
		const std::vector<glm::quat>& GetBoneRotations() const { return m_BoneRotations; }
		const std::vector<glm::vec3>& GetBoneScales() const { return m_BoneScales; }

		// Rest pose in model space (relative to the skeleton root), and its inverse.
		const std::vector<glm::mat4>& GetModelSpaceRestPose() const { return m_ModelSpaceRestPose; }
		const std::vector<glm::mat4>& GetModelSpaceRestPoseInverse() const { return m_ModelSpaceRestPoseInverse; }

		// "LZSK" blob shared by the .lskel file and the asset pack. Deserialize returns false and leaves
		// the skeleton empty on a malformed stream.
		void Serialize(StreamWriter* writer) const;
		bool Deserialize(StreamReader* reader);

	private:
		void Clear();

		std::vector<std::string> m_BoneNames;
		std::vector<uint32_t> m_ParentBoneIndices;
		std::vector<glm::vec3> m_BoneTranslations;
		std::vector<glm::quat> m_BoneRotations;
		std::vector<glm::vec3> m_BoneScales;
		std::vector<glm::mat4> m_ModelSpaceRestPose;
		std::vector<glm::mat4> m_ModelSpaceRestPoseInverse;
	};
}
