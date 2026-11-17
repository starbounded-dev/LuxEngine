// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AnimationScriptBindings.h"
#include "ScriptEngine.h"

#include "Lux/Animation/AnimationGraph.h"
#include "Lux/Animation/Animator.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Core/Application.h"
#include "Lux/Math/Math.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"

#include <Coral/Assembly.hpp>
#include <Coral/String.hpp>

#include <cmath>

namespace Lux
{
	namespace
	{
		// The entity's SkeletonComponent, or null (with an error logged) when it is gone, has none, or the call is
		// not on the main thread. Every binding below goes through this.
		SkeletonComponent* AnimatorOf(uint64_t id, Scene** sceneOut = nullptr)
		{
			if (!Application::IsMainThread())
			{
				LUX_CORE_ERROR_TAG("Animation", "The scripting animation API must be called on the main thread");
				return nullptr;
			}
			Scene* scene = ScriptEngine::GetInstance().GetCurrentScene().Raw();
			Entity entity = scene ? scene->TryGetEntityWithUUID(id) : Entity{};
			auto* component = entity ? entity.TryGetComponent<SkeletonComponent>() : nullptr;
			if (!component)
				LUX_CORE_ERROR_TAG("Animation", "Entity {} has no SkeletonComponent", id);
			if (sceneOut)
				*sceneOut = scene;
			return component;
		}

		// The component's graph with its runtime started, or null when it has no (valid) graph.
		Ref<AnimationGraph> Graph(SkeletonComponent& component)
		{
			if (!component.Graph)
				return nullptr;
			Ref<AnimationGraph> graph = AssetManager::GetAsset<AnimationGraph>(component.Graph);
			if (!graph || graph->States.empty())
				return nullptr;
			if (!component.State.Graph.Initialized)
			{
				ResetGraph(*graph, component.State.Graph);
				component.State.GraphClip = graph->States[component.State.Graph.CurrentState].Clip;
			}
			return graph;
		}

		float Animation_GetScalar(uint64_t id, int32_t field)
		{
			const auto* component = AnimatorOf(id);
			if (!component)
				return 0.0f;
			switch (field)
			{
				case 0: return component->State.Playing ? 1.0f : 0.0f;
				case 1: return component->State.Time;
				case 2: return component->Speed;
				case 3: return component->Loop ? 1.0f : 0.0f;
				default: return 0.0f;
			}
		}

		Coral::Bool32 Animation_SetScalar(uint64_t id, int32_t field, float value)
		{
			auto* component = AnimatorOf(id);
			if (!component || !std::isfinite(value))
				return false;
			switch (field)
			{
				case 0: component->State.Playing = value != 0.0f; return true;
				case 1: component->State.Time = std::max(0.0f, value); return true;
				case 2: component->Speed = value; return true;
				case 3: component->Loop = value != 0.0f; return true;
				default: return false;
			}
		}

		Coral::Bool32 Animation_CrossFade(uint64_t id, uint64_t clip, float seconds)
		{
			auto* component = AnimatorOf(id);
			if (!component || !std::isfinite(seconds) || seconds < 0.0f || !AssetManager::IsAssetHandleValid(clip)
				|| AssetManager::GetAssetType(clip) != AssetType::Animation)
				return false;
			if (component->Graph)
			{
				LUX_CORE_WARN_TAG("Animation", "Entity {} plays an animation graph; CrossFade is ignored (use PlayState)", id);
				return false;
			}
			component->CrossFadeTo(clip, seconds);
			return true;
		}

		Coral::Bool32 Animation_PlayState(uint64_t id, Coral::String stateName, float seconds)
		{
			auto* component = AnimatorOf(id);
			if (!component || !std::isfinite(seconds) || seconds < 0.0f)
				return false;
			const auto graph = Graph(*component);
			const uint32_t index = graph ? graph->FindState(std::string(stateName)) : AnimationGraph::StateNone;
			if (index == AnimationGraph::StateNone)
				return false;
			AnimatorState& state = component->State;
			if (index != state.Graph.CurrentState)
			{
				StartCrossFade(state, state.GraphClip, seconds);
				state.Graph.CurrentState = index;
				state.GraphClip = graph->States[index].Clip;
			}
			return true;
		}

		Coral::String Animation_GetState(uint64_t id)
		{
			auto* component = AnimatorOf(id);
			const auto graph = component ? Graph(*component) : nullptr;
			return Coral::String::New(graph ? graph->States[component->State.Graph.CurrentState].Name : std::string());
		}

		// kind: 0 float, 1 bool, 2 trigger
		Coral::Bool32 Animation_SetParameter(uint64_t id, Coral::String name, int32_t kind, float value)
		{
			auto* component = AnimatorOf(id);
			const auto graph = component ? Graph(*component) : nullptr;
			if (!graph)
				return false;
			AnimationGraphRuntime& runtime = component->State.Graph;
			const std::string parameter = name;
			switch (kind)
			{
				case 0: return SetGraphFloat(*graph, runtime, parameter, value);
				case 1: return SetGraphBool(*graph, runtime, parameter, value != 0.0f);
				case 2: return SetGraphTrigger(*graph, runtime, parameter);
				default: return false;
			}
		}

		float Animation_GetParameter(uint64_t id, Coral::String name)
		{
			auto* component = AnimatorOf(id);
			const auto graph = component ? Graph(*component) : nullptr;
			float value = 0.0f;
			if (graph)
				GetGraphFloat(*graph, component->State.Graph, std::string(name), value);
			return value;
		}

		// World position and rotation (Euler radians, as Transform reports them) of a bone, from the pose of the
		// last update. False when the bone is unknown or nothing has been evaluated yet.
		Coral::Bool32 Animation_GetBone(uint64_t id, Coral::String bone, glm::vec3* position, glm::vec3* rotation)
		{
			Scene* scene = nullptr;
			auto* component = AnimatorOf(id, &scene);
			if (!component || !position || !rotation)
				return false;
			Ref<::Lux::Skeleton> skeleton = AssetManager::GetAsset<::Lux::Skeleton>(component->SkeletonAsset);
			const uint32_t index = skeleton ? skeleton->GetBoneIndex(std::string(bone)) : ::Lux::Skeleton::NullIndex;
			const auto& pose = component->State.ModelPose;
			if (index == ::Lux::Skeleton::NullIndex || index >= pose.size())
				return false;
			const glm::mat4 world = scene->GetWorldSpaceTransformMatrix(scene->GetEntityByUUID(id)) * pose[index];
			glm::vec3 translation, scale;
			glm::quat orientation;
			if (!Math::DecomposeTransform(world, translation, orientation, scale))
				return false;
			*position = translation;
			*rotation = glm::eulerAngles(orientation);
			return true;
		}

		Coral::Bool32 Animation_Attach(uint64_t id, uint64_t child, Coral::String bone)
		{
			Scene* scene = nullptr;
			auto* component = AnimatorOf(id, &scene);
			if (!component || !scene || child == id || !scene->TryGetEntityWithUUID(child))
				return false;
			Ref<::Lux::Skeleton> skeleton = AssetManager::GetAsset<::Lux::Skeleton>(component->SkeletonAsset);
			const uint32_t index = skeleton ? skeleton->GetBoneIndex(std::string(bone)) : ::Lux::Skeleton::NullIndex;
			if (index == ::Lux::Skeleton::NullIndex)
				return false;
			for (AnimatorAttachment& attachment : component->State.Attachments)
			{
				if (attachment.Entity == child)
				{
					attachment.Bone = index;
					return true;
				}
			}
			component->State.Attachments.push_back({ child, index });
			return true;
		}

		Coral::Bool32 Animation_Detach(uint64_t id, uint64_t child)
		{
			auto* component = AnimatorOf(id);
			if (!component)
				return false;
			auto& list = component->State.Attachments;
			const size_t before = list.size();
			std::erase_if(list, [child](const AnimatorAttachment& attachment) { return attachment.Entity == child; });
			return list.size() != before;
		}
	}

	void AnimationScriptBindings::Register(Coral::ManagedAssembly& assembly)
	{
#define LUX_ANIMATION_CALL(Name) assembly.AddInternalCall("Lux.InternalCalls", #Name, reinterpret_cast<void*>(&Name))
		LUX_ANIMATION_CALL(Animation_GetScalar);
		LUX_ANIMATION_CALL(Animation_SetScalar);
		LUX_ANIMATION_CALL(Animation_CrossFade);
		LUX_ANIMATION_CALL(Animation_PlayState);
		LUX_ANIMATION_CALL(Animation_GetState);
		LUX_ANIMATION_CALL(Animation_SetParameter);
		LUX_ANIMATION_CALL(Animation_GetParameter);
		LUX_ANIMATION_CALL(Animation_GetBone);
		LUX_ANIMATION_CALL(Animation_Attach);
		LUX_ANIMATION_CALL(Animation_Detach);
#undef LUX_ANIMATION_CALL
	}
}
