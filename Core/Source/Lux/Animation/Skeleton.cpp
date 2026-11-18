// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Skeleton.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Lux
{
	namespace
	{
		constexpr char k_Magic[4] = { 'L', 'Z', 'S', 'K' };
		constexpr uint32_t k_Version = 1;

		glm::mat4 Compose(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale)
		{
			return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
		}
	}

	uint32_t Skeleton::AddBone(std::string name, uint32_t parentIndex, const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale)
	{
		const uint32_t index = GetNumBones();
		if (name.empty() || index >= MaxBones || GetBoneIndex(name) != NullIndex)
			return NullIndex;
		if (parentIndex != NullIndex && parentIndex >= index)
			return NullIndex;

		m_BoneNames.push_back(std::move(name));
		m_ParentBoneIndices.push_back(parentIndex);
		m_BoneTranslations.push_back(translation);
		m_BoneRotations.push_back(rotation);
		m_BoneScales.push_back(scale);
		return index;
	}

	uint32_t Skeleton::GetBoneIndex(std::string_view name) const
	{
		for (uint32_t i = 0; i < m_BoneNames.size(); i++)
		{
			if (m_BoneNames[i] == name)
				return i;
		}
		return NullIndex;
	}

	void Skeleton::Clear()
	{
		m_BoneNames.clear();
		m_ParentBoneIndices.clear();
		m_BoneTranslations.clear();
		m_BoneRotations.clear();
		m_BoneScales.clear();
		m_ModelSpaceRestPose.clear();
		m_ModelSpaceRestPoseInverse.clear();
	}

	void Skeleton::CalculateDerivedData()
	{
		const uint32_t count = GetNumBones();
		m_ModelSpaceRestPose.resize(count);
		m_ModelSpaceRestPoseInverse.resize(count);
		for (uint32_t i = 0; i < count; i++)
		{
			glm::mat4 model = Compose(m_BoneTranslations[i], m_BoneRotations[i], m_BoneScales[i]);
			if (m_ParentBoneIndices[i] != NullIndex)
				model = m_ModelSpaceRestPose[m_ParentBoneIndices[i]] * model;
			m_ModelSpaceRestPose[i] = model;
			m_ModelSpaceRestPoseInverse[i] = glm::inverse(model);
		}
	}

	void Skeleton::Serialize(StreamWriter* writer) const
	{
		writer->WriteData(k_Magic, sizeof(k_Magic));
		writer->WriteRaw<uint32_t>(k_Version);
		writer->WriteRaw<uint32_t>(GetNumBones());
		for (uint32_t i = 0; i < GetNumBones(); i++)
		{
			writer->WriteString(m_BoneNames[i]);
			writer->WriteRaw<uint32_t>(m_ParentBoneIndices[i]);
			writer->WriteRaw<glm::vec3>(m_BoneTranslations[i]);
			writer->WriteRaw<glm::quat>(m_BoneRotations[i]);
			writer->WriteRaw<glm::vec3>(m_BoneScales[i]);
		}
	}

	bool Skeleton::Deserialize(StreamReader* reader)
	{
		Clear();

		char magic[4] = {};
		uint32_t version = 0;
		uint32_t count = 0;
		if (!reader->ReadData(magic, sizeof(magic)) || memcmp(magic, k_Magic, sizeof(magic)) != 0
			|| !reader->ReadRaw(version) || version != k_Version
			|| !reader->ReadRaw(count) || count > MaxBones)
			return false;

		for (uint32_t i = 0; i < count; i++)
		{
			std::string name;
			uint32_t parent = 0;
			glm::vec3 translation, scale;
			glm::quat rotation;
			if (!reader->ReadString(name) || !reader->ReadRaw(parent) || !reader->ReadRaw(translation)
				|| !reader->ReadRaw(rotation) || !reader->ReadRaw(scale)
				|| AddBone(std::move(name), parent, translation, rotation, scale) == NullIndex)
			{
				Clear();
				return false;
			}
		}
		CalculateDerivedData();
		return true;
	}
}
