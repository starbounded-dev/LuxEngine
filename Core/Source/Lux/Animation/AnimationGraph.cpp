// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AnimationGraph.h"

#include <cmath>
#include <unordered_set>

namespace Lux
{
	namespace
	{
		bool Holds(const AnimationGraphCondition& condition, float value)
		{
			switch (condition.Op)
			{
			case AnimationConditionOp::Greater: return value > condition.Value;
			case AnimationConditionOp::Less: return value < condition.Value;
			case AnimationConditionOp::Equal: return std::abs(value - condition.Value) < 1e-5f;
			case AnimationConditionOp::NotEqual: return std::abs(value - condition.Value) >= 1e-5f;
			case AnimationConditionOp::True:
			case AnimationConditionOp::Triggered: return value != 0.0f;
			case AnimationConditionOp::False: return value == 0.0f;
			}
			return false;
		}
	}

	uint32_t AnimationGraph::FindParameter(std::string_view name) const
	{
		for (uint32_t i = 0; i < Parameters.size(); i++)
		{
			if (Parameters[i].Name == name)
				return i;
		}
		return ParameterNone;
	}

	uint32_t AnimationGraph::FindState(std::string_view name) const
	{
		for (uint32_t i = 0; i < States.size(); i++)
		{
			if (States[i].Name == name)
				return i;
		}
		return StateNone;
	}

	bool AnimationGraph::Validate(std::string* error, uint32_t* invalidTransition)
	{
		if (invalidTransition)
			*invalidTransition = StateNone;
		const auto fail = [&](std::string message)
		{
			if (error)
				*error = std::move(message);
			return false;
		};
		if (States.empty())
			return fail("a graph needs at least one state");
		if (Parameters.size() > MaxParameters || States.size() > MaxStates || Transitions.size() > MaxTransitions)
			return fail("too many parameters, states or transitions");
		if (DefaultState >= States.size())
			return fail("the default state does not exist");

		std::unordered_set<std::string> names;
		for (const AnimationGraphParameter& parameter : Parameters)
		{
			if (parameter.Name.empty() || !names.insert(parameter.Name).second)
				return fail("parameter names must be non-empty and unique");
			if (!std::isfinite(parameter.Default))
				return fail("parameter '" + parameter.Name + "' has a non-finite default");
		}
		names.clear();
		for (const AnimationGraphState& state : States)
		{
			if (state.Name.empty() || !names.insert(state.Name).second)
				return fail("state names must be non-empty and unique");
			if (!std::isfinite(state.Speed))
				return fail("state '" + state.Name + "' has a non-finite speed");
		}
		for (uint32_t i = 0; i < Transitions.size(); ++i)
		{
			AnimationGraphTransition& transition = Transitions[i];
			if (invalidTransition)
				*invalidTransition = i;
			if (transition.To >= States.size() || (transition.From != AnimationGraphTransition::AnyState && transition.From >= States.size()))
				return fail("a transition names a state that does not exist");
			if (!std::isfinite(transition.Duration) || transition.Duration < 0.0f || !std::isfinite(transition.ExitTime))
				return fail("a transition has a negative or non-finite duration or exit time");
			if (transition.Conditions.size() > MaxConditions)
				return fail("a transition has too many conditions");
			for (AnimationGraphCondition& condition : transition.Conditions)
			{
				condition.ParameterIndex = FindParameter(condition.Parameter);
				if (condition.ParameterIndex == ParameterNone)
					return fail("a condition names the unknown parameter '" + condition.Parameter + "'");
				const AnimationParameterType type = Parameters[condition.ParameterIndex].Type;
				if ((condition.Op == AnimationConditionOp::Triggered) != (type == AnimationParameterType::Trigger))
					return fail("parameter '" + condition.Parameter + "' and its condition disagree: Triggered is for Trigger parameters only");
				if (!std::isfinite(condition.Value))
					return fail("a condition has a non-finite value");
			}
		}
		if (invalidTransition)
			*invalidTransition = StateNone;
		return true;
	}

	void ResetGraph(const AnimationGraph& graph, AnimationGraphRuntime& runtime)
	{
		runtime.Initialized = true;
		runtime.CurrentState = graph.DefaultState < graph.States.size() ? graph.DefaultState : 0;
		runtime.Values.clear();
		for (const AnimationGraphParameter& parameter : graph.Parameters)
			runtime.Values.push_back(parameter.Default);
	}

	namespace
	{
		bool SetValue(const AnimationGraph& graph, AnimationGraphRuntime& runtime, std::string_view name, AnimationParameterType type, float value)
		{
			const uint32_t index = graph.FindParameter(name);
			if (index == AnimationGraph::ParameterNone || graph.Parameters[index].Type != type || index >= runtime.Values.size())
				return false;
			runtime.Values[index] = value;
			return true;
		}
	}

	bool SetGraphFloat(const AnimationGraph& graph, AnimationGraphRuntime& runtime, std::string_view name, float value)
	{
		return std::isfinite(value) && SetValue(graph, runtime, name, AnimationParameterType::Float, value);
	}

	bool SetGraphBool(const AnimationGraph& graph, AnimationGraphRuntime& runtime, std::string_view name, bool value)
	{
		return SetValue(graph, runtime, name, AnimationParameterType::Bool, value ? 1.0f : 0.0f);
	}

	bool SetGraphTrigger(const AnimationGraph& graph, AnimationGraphRuntime& runtime, std::string_view name)
	{
		return SetValue(graph, runtime, name, AnimationParameterType::Trigger, 1.0f);
	}

	bool GetGraphFloat(const AnimationGraph& graph, const AnimationGraphRuntime& runtime, std::string_view name, float& value)
	{
		const uint32_t index = graph.FindParameter(name);
		if (index == AnimationGraph::ParameterNone || index >= runtime.Values.size())
			return false;
		value = runtime.Values[index];
		return true;
	}

	AnimationGraphStep UpdateGraph(const AnimationGraph& graph, AnimationGraphRuntime& runtime, float normalizedTime)
	{
		AnimationGraphStep step;
		step.State = runtime.CurrentState;
		if (runtime.CurrentState >= graph.States.size() || runtime.Values.size() != graph.Parameters.size())
			return step;

		for (const AnimationGraphTransition& transition : graph.Transitions)
		{
			if (transition.From != AnimationGraphTransition::AnyState && transition.From != runtime.CurrentState)
				continue;
			if (transition.To == runtime.CurrentState)
				continue;
			if (transition.ExitTime >= 0.0f && normalizedTime < transition.ExitTime)
				continue;
			bool all = true;
			for (const AnimationGraphCondition& condition : transition.Conditions)
			{
				if (condition.ParameterIndex >= runtime.Values.size() || !Holds(condition, runtime.Values[condition.ParameterIndex]))
				{
					all = false;
					break;
				}
			}
			if (!all)
				continue;
			for (const AnimationGraphCondition& condition : transition.Conditions)
			{
				if (condition.Op == AnimationConditionOp::Triggered)
					runtime.Values[condition.ParameterIndex] = 0.0f;
			}
			runtime.CurrentState = transition.To;
			step.Transitioned = true;
			step.State = transition.To;
			step.FadeSeconds = transition.Duration;
			return step;
		}
		return step;
	}
}
