// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Lux
{
	enum class AnimationParameterType : uint8_t
	{
		Float,
		Bool,    // stored as 0 or 1
		Trigger  // set by gameplay, cleared when a transition that tests it is taken
	};

	struct AnimationGraphParameter
	{
		std::string Name;
		AnimationParameterType Type = AnimationParameterType::Float;
		float Default = 0.0f;
	};

	enum class AnimationConditionOp : uint8_t
	{
		Greater,
		Less,
		Equal,
		NotEqual,
		True,      // a Bool or Float parameter is not 0
		False,
		Triggered  // a Trigger parameter is set; taking the transition clears it
	};

	struct AnimationGraphCondition
	{
		std::string Parameter;
		AnimationConditionOp Op = AnimationConditionOp::Greater;
		float Value = 0.0f;
		uint32_t ParameterIndex = 0; // resolved by AnimationGraph::Validate, not stored in the file
	};

	struct AnimationGraphState
	{
		std::string Name;
		AssetHandle Clip = 0;
		float Speed = 1.0f;
		bool Loop = true;
	};

	struct AnimationGraphTransition
	{
		static constexpr uint32_t AnyState = ~0u; // From: transitions out of whichever state is current
		uint32_t From = AnyState;
		uint32_t To = 0;
		float Duration = 0.2f;  // cross-fade seconds
		float ExitTime = -1.0f; // normalized time in the current clip the transition waits for; < 0 means no wait
		std::vector<AnimationGraphCondition> Conditions; // all must hold
	};

	// A state machine over clips: parameters gameplay sets, states that play one clip each, and transitions that
	// fire when their conditions hold. Stored as YAML (`.lanimgraph`) so it diffs and merges; the same text goes
	// into asset packs. Drives SkeletonComponent playback; carries no pose of its own.
	class AnimationGraph : public Asset
	{
	public:
		static constexpr uint32_t MaxParameters = 256, MaxStates = 256, MaxTransitions = 4096, MaxConditions = 32;

		static AssetType GetStaticType() { return AssetType::AnimationGraph; }
		AssetType GetAssetType() const override { return GetStaticType(); }

		std::vector<AnimationGraphParameter> Parameters;
		std::vector<AnimationGraphState> States;
		std::vector<AnimationGraphTransition> Transitions;
		uint32_t DefaultState = 0;

		uint32_t FindParameter(std::string_view name) const; // ParameterNone if absent
		uint32_t FindState(std::string_view name) const;     // StateNone if absent
		static constexpr uint32_t ParameterNone = ~0u, StateNone = ~0u;

		// Names unique, indices in range, conditions refer to parameters of a fitting type, numbers finite;
		// resolves each condition's ParameterIndex. A graph that fails is not used. `error` says why.
		bool Validate(std::string* error = nullptr);

		std::string ToYAML() const;
		bool FromYAML(const std::string& text); // leaves *this untouched and returns false on bad input
	};

	// Round trip and bad-input checks of the YAML form; appends one line per failure. Run by the golden capture's
	// self-test, since the YAML reader is not part of the headless animation tests.
	bool RunAnimationGraphSelfTests(std::vector<std::string>* failures);

	// What running a graph changes: the current state and the parameter values. Runtime only.
	struct AnimationGraphRuntime
	{
		bool Initialized = false;
		uint32_t CurrentState = 0;
		std::vector<float> Values; // one per parameter
	};

	// Starts the graph in its default state with every parameter at its default.
	void ResetGraph(const AnimationGraph& graph, AnimationGraphRuntime& runtime);

	// Gameplay-facing setters. They return false for an unknown name or a parameter of another type.
	bool SetGraphFloat(const AnimationGraph& graph, AnimationGraphRuntime& runtime, std::string_view name, float value);
	bool SetGraphBool(const AnimationGraph& graph, AnimationGraphRuntime& runtime, std::string_view name, bool value);
	bool SetGraphTrigger(const AnimationGraph& graph, AnimationGraphRuntime& runtime, std::string_view name);
	bool GetGraphFloat(const AnimationGraph& graph, const AnimationGraphRuntime& runtime, std::string_view name, float& value);

	struct AnimationGraphStep
	{
		bool Transitioned = false;
		uint32_t State = 0;      // the state to play now
		float FadeSeconds = 0.0f;
	};

	// Takes the first transition (in file order) out of the current state, or from Any, whose exit time has been
	// reached and whose conditions all hold. `normalizedTime` is how far the current clip has played (time over
	// duration, 0 to 1 within a loop). Clears the triggers the taken transition tested. At most one transition
	// per call, so a chain of transitions plays one state per update.
	AnimationGraphStep UpdateGraph(const AnimationGraph& graph, AnimationGraphRuntime& runtime, float normalizedTime);
}
