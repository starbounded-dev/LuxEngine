// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Animator.h"

#include "Lux/Animation/AnimationPose.h"

#include <algorithm>

namespace Lux
{
	namespace
	{
		glm::vec3 Horizontal(const glm::vec3& v)
		{
			return { v.x, 0.0f, v.z };
		}
	}

	void ResetAnimator(AnimatorState& state, float startTime, bool playing)
	{
		state = AnimatorState();
		state.Initialized = true;
		state.Playing = playing;
		state.Time = startTime;
	}

	void SyncAnimatorGraph(AnimatorState& state, const Ref<AnimationGraph>& graph, float startTime)
	{
		if (state.Graph.Initialized && state.GraphSource == graph && state.GraphRevision == graph->GetRevision())
			return;
		if (state.Graph.Initialized)
		{
			StartCrossFade(state, 0, 0.0f);
			state.Time = startTime;
		}
		ResetGraph(*graph, state.Graph);
		state.GraphClip = graph->States[state.Graph.CurrentState].Clip;
		state.GraphSource = graph;
		state.GraphRevision = graph->GetRevision();
	}

	void CollectAnimationEvents(const Animation& clip, float before, float after, int wrapped, bool includeStart, std::vector<FiredAnimationEvent>& out)
	{
		const std::vector<AnimationEvent>& events = clip.GetEvents();
		if (events.empty())
			return;
		const float duration = clip.GetDuration();
		const auto fire = [&](const AnimationEvent& event) { out.push_back({ event.Name, event.Argument, event.Sound }); };
		const bool forward = wrapped > 0 || (wrapped == 0 && after >= before);
		if (forward)
		{
			// (a, b] in play order; the first advance also takes an event exactly at its start.
			const auto span = [&](float a, float b, bool inclusiveA)
			{
				for (const AnimationEvent& event : events)
				{
					if ((event.Time > a || (inclusiveA && event.Time >= a)) && event.Time <= b)
						fire(event);
				}
			};
			if (wrapped > 0)
			{
				span(before, duration, false);
				span(0.0f, after, true);
			}
			else
			{
				span(before, after, includeStart);
			}
		}
		else
		{
			// [a, b) played from the end backwards.
			const auto span = [&](float a, float b, bool inclusiveB)
			{
				for (auto it = events.rbegin(); it != events.rend(); ++it)
				{
					if (it->Time >= a && (it->Time < b || (inclusiveB && it->Time <= b)))
						fire(*it);
				}
			};
			if (wrapped < 0)
			{
				span(0.0f, before, false);
				span(after, duration, true);
			}
			else
			{
				span(after, before, includeStart);
			}
		}
	}

	void StartCrossFade(AnimatorState& state, AssetHandle fromClip, float seconds)
	{
		state.FadeFromClip = seconds > 0.0f ? fromClip : AssetHandle(0);
		state.FadeFromTime = state.Time;
		state.FadeElapsed = 0.0f;
		state.FadeDuration = seconds;
		state.Time = 0.0f;
		state.OffsetValid = false;
		state.EventsStarted = false; // the new clip's events at time 0 fire on its first advance
	}

	uint32_t FindRootMotionBone(const Skeleton& skeleton, const Animation& clip)
	{
		for (const BoneTrack& track : clip.GetTracks())
		{
			if (!track.Translations.empty() && track.BoneIndex < skeleton.GetNumBones())
				return track.BoneIndex;
		}
		return Skeleton::NullIndex;
	}

	glm::vec3 AdvanceAnimator(AnimatorState& state, const AnimatorSettings& settings, const Skeleton& skeleton,
		const Animation& clip, AssetHandle clipHandle, const Animation* fadeFrom, float dt)
	{
		const float duration = clip.GetDuration();
		const bool rootMotion = settings.RootBone < skeleton.GetNumBones();

		if (!state.OffsetValid || state.CachedClip != clipHandle || state.CachedRootBone != settings.RootBone)
		{
			state.CachedClip = clipHandle;
			state.CachedRootBone = settings.RootBone;
			state.OffsetValid = true;
			if (rootMotion)
			{
				const glm::vec3 start = Horizontal(BoneModelPosition(skeleton, clip, settings.RootBone, 0.0f));
				state.StartPosition = start;
				state.LoopTravel = Horizontal(BoneModelPosition(skeleton, clip, settings.RootBone, duration)) - start;
			}
			state.LastOffset = glm::vec3(0.0f);
			state.ModelPose.clear(); // the first evaluation below seeds LastOffset
		}
		const bool seed = state.ModelPose.empty();

		// Advance time and the fade.
		state.FiredEvents.clear();
		float travelAdjust = 0.0f; // +1 after a forward loop wrap, -1 after a backward one
		if (state.Playing && dt > 0.0f && duration > 0.0f)
		{
			const float before = state.Time;
			state.Time += dt * settings.Speed;
			if (settings.Loop)
			{
				state.Time = WrapAnimationTime(state.Time, duration, true);
				if (settings.Speed > 0.0f && state.Time < before)
					travelAdjust = 1.0f;
				else if (settings.Speed < 0.0f && state.Time > before)
					travelAdjust = -1.0f;
			}
			else
			{
				state.Time = std::clamp(state.Time, 0.0f, duration);
			}
			CollectAnimationEvents(clip, before, state.Time, static_cast<int>(travelAdjust), !state.EventsStarted, state.FiredEvents);
			state.EventsStarted = true;
			if (state.FadeDuration > 0.0f)
			{
				state.FadeElapsed += dt;
				state.FadeFromTime += dt * settings.Speed;
				if (state.FadeElapsed >= state.FadeDuration)
					state.FadeFromClip = 0;
			}
		}
		else if (state.FadeDuration > 0.0f && state.FadeElapsed >= state.FadeDuration)
		{
			state.FadeFromClip = 0;
		}

		// Evaluate the pose.
		LocalPose pose = MakeRestPose(skeleton);
		SampleAnimation(clip, WrapAnimationTime(state.Time, duration, settings.Loop), pose);
		if (fadeFrom && state.FadeFromClip && state.FadeDuration > 0.0f && state.FadeElapsed < state.FadeDuration)
		{
			LocalPose old = MakeRestPose(skeleton);
			SampleAnimation(*fadeFrom, WrapAnimationTime(state.FadeFromTime, fadeFrom->GetDuration(), true), old);
			const float t = state.FadeElapsed / state.FadeDuration;
			BlendPoses(old, pose, t * t * (3.0f - 2.0f * t), pose);
		}
		state.ModelPose = BuildModelPose(skeleton, pose);

		glm::vec3 delta(0.0f);
		if (rootMotion)
		{
			const glm::vec3 offset = Horizontal(glm::vec3(state.ModelPose[settings.RootBone][3])) - state.StartPosition;
			if (seed)
			{
				state.LastOffset = offset; // the first frame has no previous position: no movement, wrapped or not
				travelAdjust = 0.0f;
			}
			delta = offset - state.LastOffset + travelAdjust * state.LoopTravel;
			state.LastOffset = offset;
			RemoveRootMotion(skeleton, settings.RootBone, offset, pose);
			state.ModelPose = BuildModelPose(skeleton, pose);
		}
		return delta;
	}
}
