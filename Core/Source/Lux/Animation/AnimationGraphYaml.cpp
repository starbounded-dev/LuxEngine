// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AnimationGraph.h"

#include "Lux/Serialization/Yaml.h"

#include <cstring>

namespace Lux
{
	namespace
	{
		constexpr int k_Version = 1;

		const char* TypeName(AnimationParameterType type)
		{
			switch (type)
			{
			case AnimationParameterType::Bool: return "Bool";
			case AnimationParameterType::Trigger: return "Trigger";
			default: return "Float";
			}
		}

		bool ParseType(const std::string& text, AnimationParameterType& type)
		{
			if (text == "Float") type = AnimationParameterType::Float;
			else if (text == "Bool") type = AnimationParameterType::Bool;
			else if (text == "Trigger") type = AnimationParameterType::Trigger;
			else return false;
			return true;
		}

		const char* OpName(AnimationConditionOp op)
		{
			switch (op)
			{
			case AnimationConditionOp::Less: return "Less";
			case AnimationConditionOp::Equal: return "Equal";
			case AnimationConditionOp::NotEqual: return "NotEqual";
			case AnimationConditionOp::True: return "True";
			case AnimationConditionOp::False: return "False";
			case AnimationConditionOp::Triggered: return "Triggered";
			default: return "Greater";
			}
		}

		bool ParseOp(const std::string& text, AnimationConditionOp& op)
		{
			static constexpr std::pair<const char*, AnimationConditionOp> k_Ops[] = {
				{ "Greater", AnimationConditionOp::Greater }, { "Less", AnimationConditionOp::Less }, { "Equal", AnimationConditionOp::Equal },
				{ "NotEqual", AnimationConditionOp::NotEqual }, { "True", AnimationConditionOp::True }, { "False", AnimationConditionOp::False },
				{ "Triggered", AnimationConditionOp::Triggered } };
			for (const auto& [name, value] : k_Ops)
			{
				if (text == name)
				{
					op = value;
					return true;
				}
			}
			return false;
		}

	}

	std::string AnimationGraph::ToYAML() const
	{
		Yaml::Writer out;
		out << Yaml::BeginMap << Yaml::Key << "AnimationGraph" << Yaml::BeginMap;
		out << Yaml::Key << "Version" << Yaml::Value << k_Version;
		out << Yaml::Key << "Parameters" << Yaml::Value << Yaml::BeginSeq;
		for (const AnimationGraphParameter& parameter : Parameters)
		{
			out << Yaml::BeginMap;
			out << Yaml::Key << "Name" << Yaml::Value << parameter.Name;
			out << Yaml::Key << "Type" << Yaml::Value << TypeName(parameter.Type);
			if (parameter.Default != 0.0f)
				out << Yaml::Key << "Default" << Yaml::Value << parameter.Default;
			out << Yaml::EndMap;
		}
		out << Yaml::EndSeq;
		out << Yaml::Key << "DefaultState" << Yaml::Value << (DefaultState < States.size() ? States[DefaultState].Name : std::string());
		out << Yaml::Key << "States" << Yaml::Value << Yaml::BeginSeq;
		for (const AnimationGraphState& state : States)
		{
			out << Yaml::BeginMap;
			out << Yaml::Key << "Name" << Yaml::Value << state.Name;
			out << Yaml::Key << "Clip" << Yaml::Value << state.Clip;
			if (state.Speed != 1.0f)
				out << Yaml::Key << "Speed" << Yaml::Value << state.Speed;
			if (!state.Loop)
				out << Yaml::Key << "Loop" << Yaml::Value << false;
			out << Yaml::EndMap;
		}
		out << Yaml::EndSeq;
		out << Yaml::Key << "Transitions" << Yaml::Value << Yaml::BeginSeq;
		for (const AnimationGraphTransition& transition : Transitions)
		{
			out << Yaml::BeginMap;
			out << Yaml::Key << "From" << Yaml::Value << (transition.From < States.size() ? States[transition.From].Name : std::string("Any"));
			out << Yaml::Key << "To" << Yaml::Value << (transition.To < States.size() ? States[transition.To].Name : std::string());
			out << Yaml::Key << "Duration" << Yaml::Value << transition.Duration;
			if (transition.ExitTime >= 0.0f)
				out << Yaml::Key << "ExitTime" << Yaml::Value << transition.ExitTime;
			out << Yaml::Key << "Conditions" << Yaml::Value << Yaml::BeginSeq;
			for (const AnimationGraphCondition& condition : transition.Conditions)
			{
				out << Yaml::BeginMap;
				out << Yaml::Key << "Parameter" << Yaml::Value << condition.Parameter;
				out << Yaml::Key << "Op" << Yaml::Value << OpName(condition.Op);
				if (condition.Op != AnimationConditionOp::True && condition.Op != AnimationConditionOp::False && condition.Op != AnimationConditionOp::Triggered)
					out << Yaml::Key << "Value" << Yaml::Value << condition.Value;
				out << Yaml::EndMap;
			}
			out << Yaml::EndSeq << Yaml::EndMap;
		}
		out << Yaml::EndSeq;
		out << Yaml::EndMap << Yaml::EndMap;
		return out.c_str();
	}

	bool AnimationGraph::FromYAML(const std::string& text)
	{
		try
		{
			const auto root = Yaml::Load(text)["AnimationGraph"];
			if (!root || root["Version"].as<int>(k_Version) != k_Version)
				throw std::runtime_error("missing animation graph or unsupported version");

			AnimationGraph parsed;
			const auto parameters = root["Parameters"];
			for (size_t i = 0; parameters && i < parameters.size(); i++)
			{
				AnimationGraphParameter parameter;
				parameter.Name = parameters[i]["Name"].as<std::string>("");
				if (!ParseType(parameters[i]["Type"].as<std::string>("Float"), parameter.Type))
					throw std::runtime_error("unknown parameter type");
				parameter.Default = parameters[i]["Default"].as<float>(0.0f);
				parsed.Parameters.push_back(std::move(parameter));
				if (parsed.Parameters.size() > MaxParameters)
					throw std::runtime_error("too many parameters");
			}
			const auto states = root["States"];
			for (size_t i = 0; states && i < states.size(); i++)
			{
				AnimationGraphState state;
				state.Name = states[i]["Name"].as<std::string>("");
				state.Clip = states[i]["Clip"].as<uint64_t>(0);
				state.Speed = states[i]["Speed"].as<float>(1.0f);
				state.Loop = states[i]["Loop"].as<bool>(true);
				parsed.States.push_back(std::move(state));
				if (parsed.States.size() > MaxStates)
					throw std::runtime_error("too many states");
			}
			const auto stateIndex = [&](const std::string& name) { return parsed.FindState(name); };
			parsed.DefaultState = stateIndex(root["DefaultState"].as<std::string>(""));
			const auto transitions = root["Transitions"];
			for (size_t i = 0; transitions && i < transitions.size(); i++)
			{
				AnimationGraphTransition transition;
				const std::string from = transitions[i]["From"].as<std::string>("Any");
				transition.From = from == "Any" ? AnimationGraphTransition::AnyState : stateIndex(from);
				if (from != "Any" && transition.From == AnimationGraph::StateNone)
					throw std::runtime_error("a transition starts from the unknown state '" + from + "'");
				transition.To = stateIndex(transitions[i]["To"].as<std::string>(""));
				transition.Duration = transitions[i]["Duration"].as<float>(0.2f);
				transition.ExitTime = transitions[i]["ExitTime"].as<float>(-1.0f);
				const auto conditions = transitions[i]["Conditions"];
				for (size_t c = 0; conditions && c < conditions.size(); c++)
				{
					AnimationGraphCondition condition;
					condition.Parameter = conditions[c]["Parameter"].as<std::string>("");
					if (!ParseOp(conditions[c]["Op"].as<std::string>("Greater"), condition.Op))
						throw std::runtime_error("unknown condition operator");
					condition.Value = conditions[c]["Value"].as<float>(0.0f);
					transition.Conditions.push_back(std::move(condition));
					if (transition.Conditions.size() > MaxConditions)
						throw std::runtime_error("too many conditions");
				}
				parsed.Transitions.push_back(std::move(transition));
				if (parsed.Transitions.size() > MaxTransitions)
					throw std::runtime_error("too many transitions");
			}
			std::string error;
			if (!parsed.Validate(&error))
				throw std::runtime_error(error);

			Parameters = std::move(parsed.Parameters);
			States = std::move(parsed.States);
			Transitions = std::move(parsed.Transitions);
			DefaultState = parsed.DefaultState;
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR("Invalid animation graph: {}", error.what());
			return false;
		}
	}


	bool RunAnimationGraphSelfTests(std::vector<std::string>* failures)
	{
		bool passed = true;
		const auto check = [&](bool condition, const char* what)
		{
			if (!condition)
			{
				passed = false;
				if (failures)
					failures->push_back(std::string("animation graph: ") + what);
			}
		};

		AnimationGraph graph;
		graph.Parameters = { { "Speed", AnimationParameterType::Float, 0.5f }, { "Grounded", AnimationParameterType::Bool, 1.0f }, { "Jump", AnimationParameterType::Trigger, 0.0f } };
		graph.States = { { "Idle", 11, 1.0f, true }, { "Walk", 12, 1.5f, true }, { "Jump", 13, 1.0f, false } };
		graph.DefaultState = 1;
		graph.Transitions.push_back({ 0, 1, 0.25f, -1.0f, { { "Speed", AnimationConditionOp::Greater, 0.1f, 0 } } });
		graph.Transitions.push_back({ AnimationGraphTransition::AnyState, 2, 0.1f, -1.0f, { { "Jump", AnimationConditionOp::Triggered, 0.0f, 0 }, { "Grounded", AnimationConditionOp::True, 0.0f, 0 } } });
		graph.Transitions.push_back({ 2, 0, 0.3f, 0.9f, {} });
		std::string error;
		check(graph.Validate(&error), "a valid graph validates");

		const std::string text = graph.ToYAML();
		AnimationGraph back;
		check(back.FromYAML(text), "a written graph reads back");
		check(back.Validate(), "the graph read back validates");
		check(back.ToYAML() == text, "writing the graph read back gives the same text");
		check(back.States.size() == 3 && back.States[1].Speed == 1.5f && !back.States[2].Loop && back.States[1].Clip == 12, "states survive");
		check(back.DefaultState == 1 && back.Parameters.size() == 3 && back.Parameters[0].Default == 0.5f, "default state and parameters survive");
		check(back.Transitions.size() == 3 && back.Transitions[1].From == AnimationGraphTransition::AnyState && back.Transitions[2].ExitTime == 0.9f, "transitions survive");
		check(back.Transitions[1].Conditions.size() == 2 && back.Transitions[1].Conditions[0].Op == AnimationConditionOp::Triggered, "conditions survive");

		const auto rejected = [&](std::string edited, const char* what)
		{
			AnimationGraph probe;
			probe.States = { { "Keep", 1, 1.0f, true } };
			check(!probe.FromYAML(edited) && probe.States.size() == 1 && probe.States[0].Name == "Keep", what);
		};
		const auto replaced = [&](const char* from, const char* to)
		{
			std::string edited = text;
			const size_t at = edited.find(from);
			if (at != std::string::npos)
				edited.replace(at, std::strlen(from), to);
			else
				check(false, "self-test edit target missing");
			return edited;
		};
		rejected("not: yaml: [", "malformed YAML is refused");
		rejected("Other: 1\n", "a file without the AnimationGraph key is refused");
		rejected(replaced("Version: 1", "Version: 9"), "an unknown version is refused");
		rejected(replaced("Parameter: Speed", "Parameter: Nope"), "a condition on an unknown parameter is refused");
		rejected(replaced("Op: Triggered", "Op: Sideways"), "an unknown operator is refused");
		rejected(replaced("To: Walk", "To: Nowhere"), "a transition to an unknown state is refused");
		rejected(replaced("From: Idle", "From: Nowhere"), "a transition from an unknown state is refused");
		rejected(replaced("DefaultState: Walk", "DefaultState: Nowhere"), "an unknown default state is refused");
		rejected(replaced("Type: Trigger", "Type: Sparkle"), "an unknown parameter type is refused");
		rejected(replaced("Name: Walk", "Name: Idle"), "duplicate state names are refused");
		rejected(replaced("Op: True", "Op: Triggered"), "Triggered on a Bool parameter is refused");
		rejected(replaced("Duration: 0.25", "Duration: -1"), "a negative duration is refused");
		return passed;
	}
}
