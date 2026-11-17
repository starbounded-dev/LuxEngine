// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Animation/Animation.h"
#include "Lux/Animation/AnimationGraph.h"
#include "Lux/Animation/Skeleton.h"

#include <glm/glm.hpp>

#include <vector>

namespace Lux
{
	// What a SkeletonComponent author sets: how a clip plays. Not state; never changed by playback.
	struct AnimatorSettings
	{
		float Speed = 1.0f;
		bool Loop = true;
		// Bone whose horizontal travel is the root motion; Skeleton::NullIndex means the pose keeps it.
		uint32_t RootBone = Skeleton::NullIndex;
	};

	// Everything playback changes while a scene runs. Runtime only: never serialized, rebuilt on every start.
	struct AnimatorState
	{
		bool Initialized = false;
		bool PlayMode = false; // the mode it was last advanced in (editor preview or a running scene)
		bool Playing = true;
		float Time = 0.0f;     // seconds into the current clip

		// When the component runs an animation graph: its state machine, and the clip its current state plays.
		AnimationGraphRuntime Graph;
		AssetHandle GraphClip = 0;

		// The clip being faded out, while FadeElapsed < FadeDuration.
		AssetHandle FadeFromClip = 0;
		float FadeFromTime = 0.0f;
		float FadeElapsed = 0.0f;
		float FadeDuration = 0.0f;

		// The pose the last advance evaluated: model-space transform of every skeleton bone. What the renderer's
		// skin palette and "read bone transform" read; empty until the first advance.
		std::vector<glm::mat4> ModelPose;

		// Root motion bookkeeping, valid for CachedClip and CachedRootBone.
		AssetHandle CachedClip = 0;
		uint32_t CachedRootBone = Skeleton::NullIndex;
		glm::vec3 StartPosition = glm::vec3(0.0f); // the root bone's horizontal model position at time 0
		glm::vec3 LoopTravel = glm::vec3(0.0f);    // how far the clip carries it from its start to its end
		glm::vec3 LastOffset = glm::vec3(0.0f);    // its offset from the start position at the last advance
		bool OffsetValid = false;
	};

	// Starts a clip from `startTime`, playing or held there.
	void ResetAnimator(AnimatorState& state, float startTime, bool playing);

	// Starts a new clip from its beginning and keeps fading out `fromClip` (at `fromTime`) over `seconds`.
	void StartCrossFade(AnimatorState& state, AssetHandle fromClip, float seconds);

	// The first bone with a position track, the root motion bone when none is named.
	uint32_t FindRootMotionBone(const Skeleton& skeleton, const Animation& clip);

	// Moves the animator `dt` seconds along `clip` (when playing), cross-fades from `fadeFrom` if a fade is
	// running, and evaluates the pose into state.ModelPose (root motion taken out of it when settings.RootBone
	// is set). Returns the root bone's horizontal model-space travel since the last advance: the entity's
	// movement, which the caller applies or ignores. Safe with dt == 0, which just re-evaluates.
	glm::vec3 AdvanceAnimator(AnimatorState& state, const AnimatorSettings& settings, const Skeleton& skeleton,
		const Animation& clip, AssetHandle clipHandle, const Animation* fadeFrom, float dt);
}
