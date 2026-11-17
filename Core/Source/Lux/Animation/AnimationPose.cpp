// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AnimationPose.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Lux
{
	namespace
	{
		// Index of the last key at or before `time` (0 if none), for an ascending key list.
		template<typename Key>
		size_t FindKey(const std::vector<Key>& keys, float time)
		{
			const auto it = std::upper_bound(keys.begin(), keys.end(), time, [](float t, const Key& key) { return t < key.Time; });
			return it == keys.begin() ? 0 : static_cast<size_t>(it - keys.begin()) - 1;
		}

		template<typename Key, typename Mix>
		auto SampleChannel(const std::vector<Key>& keys, float time, Mix mix)
		{
			if (keys.size() == 1 || time <= keys.front().Time)
				return keys.front().Value;
			if (time >= keys.back().Time)
				return keys.back().Value;
			const size_t i = FindKey(keys, time);
			const float span = keys[i + 1].Time - keys[i].Time;
			const float t = span > 0.0f ? (time - keys[i].Time) / span : 0.0f;
			return mix(keys[i].Value, keys[i + 1].Value, t);
		}
	}

	LocalPose MakeRestPose(const Skeleton& skeleton)
	{
		return { skeleton.GetBoneTranslations(), skeleton.GetBoneRotations(), skeleton.GetBoneScales() };
	}

	float WrapAnimationTime(float time, float duration, bool loop)
	{
		if (duration <= 0.0f)
			return 0.0f;
		if (!loop)
			return std::clamp(time, 0.0f, duration);
		const float wrapped = std::fmod(time, duration);
		return wrapped < 0.0f ? wrapped + duration : wrapped;
	}

	void SampleAnimation(const Animation& clip, float time, LocalPose& pose)
	{
		for (const BoneTrack& track : clip.GetTracks())
		{
			if (track.BoneIndex >= pose.Translations.size())
				continue;
			if (!track.Translations.empty())
				pose.Translations[track.BoneIndex] = SampleChannel(track.Translations, time, [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); });
			if (!track.Rotations.empty())
				pose.Rotations[track.BoneIndex] = SampleChannel(track.Rotations, time, [](const glm::quat& a, const glm::quat& b, float t) { return glm::normalize(glm::slerp(a, b, t)); });
			if (!track.Scales.empty())
				pose.Scales[track.BoneIndex] = SampleChannel(track.Scales, time, [](const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); });
		}
	}

	void BlendPoses(const LocalPose& a, const LocalPose& b, float weight, LocalPose& out)
	{
		weight = std::clamp(weight, 0.0f, 1.0f);
		const size_t count = std::min(a.Translations.size(), b.Translations.size());
		out.Translations.resize(count);
		out.Rotations.resize(count);
		out.Scales.resize(count);
		for (size_t i = 0; i < count; i++)
		{
			out.Translations[i] = glm::mix(a.Translations[i], b.Translations[i], weight);
			out.Rotations[i] = glm::normalize(glm::slerp(a.Rotations[i], b.Rotations[i], weight));
			out.Scales[i] = glm::mix(a.Scales[i], b.Scales[i], weight);
		}
	}

	std::vector<glm::mat4> BuildModelPose(const Skeleton& skeleton, const LocalPose& pose)
	{
		const uint32_t count = skeleton.GetNumBones();
		std::vector<glm::mat4> model(count, glm::mat4(1.0f));
		for (uint32_t i = 0; i < count && i < pose.Translations.size(); i++)
		{
			const glm::mat4 local = glm::translate(glm::mat4(1.0f), pose.Translations[i]) * glm::mat4_cast(pose.Rotations[i]) * glm::scale(glm::mat4(1.0f), pose.Scales[i]);
			const uint32_t parent = skeleton.GetParentBoneIndex(i);
			model[i] = parent == Skeleton::NullIndex ? local : model[parent] * local;
		}
		return model;
	}

	namespace
	{
		glm::vec3 BoneModelPosition(const Skeleton& skeleton, const Animation& clip, uint32_t bone, float time)
		{
			LocalPose pose = MakeRestPose(skeleton);
			SampleAnimation(clip, time, pose);
			return glm::vec3(BuildModelPose(skeleton, pose)[bone][3]);
		}
	}

	glm::vec3 RootMotionOffset(const Skeleton& skeleton, const Animation& clip, uint32_t bone, float time)
	{
		if (bone >= skeleton.GetNumBones())
			return glm::vec3(0.0f);
		const glm::vec3 offset = BoneModelPosition(skeleton, clip, bone, time) - BoneModelPosition(skeleton, clip, bone, 0.0f);
		return { offset.x, 0.0f, offset.z };
	}

	void RemoveRootMotion(const Skeleton& skeleton, uint32_t bone, const glm::vec3& offset, LocalPose& pose)
	{
		if (bone >= skeleton.GetNumBones() || bone >= pose.Translations.size())
			return;
		const uint32_t parent = skeleton.GetParentBoneIndex(bone);
		glm::mat3 parentRotation(1.0f);
		if (parent != Skeleton::NullIndex)
			parentRotation = glm::mat3(BuildModelPose(skeleton, pose)[parent]);
		pose.Translations[bone] -= glm::inverse(parentRotation) * offset;
	}
}
