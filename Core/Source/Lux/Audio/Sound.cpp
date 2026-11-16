// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Sound.h"

#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/StringUtils.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Lux
{
	namespace
	{
		constexpr int kVersion = 2;
		constexpr size_t kMaxNameBytes = 64, kMaxBusBytes = 256, kMaxLabels = 64, kMaxCurvePoints = 64;
		constexpr float kMaxVolume = 4.0f, kMaxPitch = 4.0f, kMaxVolumeVariationDb = 24.0f, kMaxPitchVariationSemitones = 12.0f;
		constexpr float kMinCutoffHz = 10.0f, kMaxCutoffHz = 24000.0f, kMaxCooldown = 60.0f;

		bool Fail(std::string* error, const std::string& message)
		{
			if (error)
				*error = message;
			return false;
		}

		bool ValidName(const std::string& name)
		{
			return !name.empty() && name.size() <= kMaxNameBytes && name.find('\0') == std::string::npos;
		}

		bool Finite(float value) { return std::isfinite(value); }

		bool ValidTargetValue(SoundCurveTarget target, float value)
		{
			switch (target)
			{
				case SoundCurveTarget::Volume:   return Finite(value) && value >= 0.0f && value <= kMaxVolume;
				case SoundCurveTarget::Pitch:    return Finite(value) && value > 0.0f && value <= kMaxPitch;
				case SoundCurveTarget::LowPass:
				case SoundCurveTarget::HighPass: return Finite(value) && value >= kMinCutoffHz && value <= kMaxCutoffHz;
				case SoundCurveTarget::Send:     return Finite(value) && value >= 0.0f && value <= 1.0f;
			}
			return false;
		}

		template<typename Enum, size_t Count>
		bool ParseName(const std::string& name, Enum& outValue, const char* (*toName)(Enum))
		{
			for (size_t i = 0; i < Count; ++i)
			{
				const auto value = static_cast<Enum>(i);
				if (name == toName(value))
				{
					outValue = value;
					return true;
				}
			}
			return false;
		}
	}

	const char* SoundAttenuationName(SoundAttenuation attenuation)
	{
		switch (attenuation)
		{
			case SoundAttenuation::None:    return "None";
			case SoundAttenuation::Inverse: return "Inverse";
			case SoundAttenuation::Linear:  return "Linear";
		}
		return "Inverse";
	}

	bool ParseSoundAttenuation(const std::string& name, SoundAttenuation& outAttenuation)
	{
		return ParseName<SoundAttenuation, 3>(name, outAttenuation, SoundAttenuationName);
	}

	const char* SoundNodeTypeName(SoundNodeType type)
	{
		switch (type)
		{
			case SoundNodeType::Clip:     return "Clip";
			case SoundNodeType::Random:   return "Random";
			case SoundNodeType::Sequence: return "Sequence";
			case SoundNodeType::Layer:    return "Layer";
			case SoundNodeType::Switch:   return "Switch";
		}
		return "Clip";
	}

	bool ParseSoundNodeType(const std::string& name, SoundNodeType& outType)
	{
		return ParseName<SoundNodeType, 5>(name, outType, SoundNodeTypeName);
	}

	const char* SoundCurveTargetName(SoundCurveTarget target)
	{
		switch (target)
		{
			case SoundCurveTarget::Volume:   return "Volume";
			case SoundCurveTarget::Pitch:    return "Pitch";
			case SoundCurveTarget::LowPass:  return "LowPass";
			case SoundCurveTarget::HighPass: return "HighPass";
			case SoundCurveTarget::Send:     return "Send";
		}
		return "Volume";
	}

	bool ParseSoundCurveTarget(const std::string& name, SoundCurveTarget& outTarget)
	{
		return ParseName<SoundCurveTarget, 5>(name, outTarget, SoundCurveTargetName);
	}

	const char* SoundStealPolicyName(SoundStealPolicy policy)
	{
		switch (policy)
		{
			case SoundStealPolicy::Oldest:   return "Oldest";
			case SoundStealPolicy::Quietest: return "Quietest";
			case SoundStealPolicy::None:     return "None";
		}
		return "Oldest";
	}

	bool ParseSoundStealPolicy(const std::string& name, SoundStealPolicy& outPolicy)
	{
		return ParseName<SoundStealPolicy, 3>(name, outPolicy, SoundStealPolicyName);
	}

	float SoundCurve::Evaluate(float value) const
	{
		if (Points.empty())
			return 0.0f;
		if (!Finite(value) || value <= Points.front().x)
			return Points.front().y;
		if (value >= Points.back().x)
			return Points.back().y;
		const auto upper = std::upper_bound(Points.begin(), Points.end(), value, [](float x, const glm::vec2& point) { return x < point.x; });
		const auto& b = *upper;
		const auto& a = *(upper - 1);
		return a.y + (b.y - a.y) * (value - a.x) / (b.x - a.x);
	}

	Ref<Sound> Sound::CreateForClip(AssetHandle clip)
	{
		auto sound = Ref<Sound>::Create();
		sound->Nodes.front().Clip = clip;
		return sound;
	}

	bool Sound::Loops() const
	{
		return std::any_of(Nodes.begin(), Nodes.end(), [](const SoundNode& node) { return node.Type == SoundNodeType::Clip && node.Loop; });
	}

	std::vector<AssetHandle> Sound::GetClips() const
	{
		std::vector<AssetHandle> clips;
		for (const auto& node : Nodes)
		{
			if (node.Type == SoundNodeType::Clip && node.Clip && std::find(clips.begin(), clips.end(), node.Clip) == clips.end())
				clips.push_back(node.Clip);
		}
		return clips;
	}

	const SoundParameter* Sound::FindParameter(const std::string& name) const
	{
		// Case-insensitive, so names typed in components and scripts
		// behave the same whatever their case.
		const auto it = std::find_if(Parameters.begin(), Parameters.end(), [&](const SoundParameter& parameter) { return Utils::String::EqualsIgnoreCase(parameter.Name, name); });
		return it != Parameters.end() ? &*it : nullptr;
	}

	std::vector<int> Sound::GetParents() const
	{
		std::vector<int> parents(Nodes.size(), -1);
		for (size_t index = 0; index < Nodes.size(); ++index)
		{
			for (const uint32_t child : Nodes[index].Children)
			{
				if (child < parents.size())
					parents[child] = static_cast<int>(index);
			}
		}
		return parents;
	}

	uint32_t Sound::AddNode(uint32_t parent, SoundNodeType type)
	{
		SoundNode node;
		node.Type = type;
		Nodes.push_back(std::move(node));
		const auto index = static_cast<uint32_t>(Nodes.size() - 1);
		Nodes[parent].Children.push_back(index);
		return index;
	}

	uint32_t Sound::WrapNode(uint32_t index, SoundNodeType type)
	{
		SoundNode container;
		container.Type = type;
		if (index == 0)
		{
			SoundNode root = Nodes[0];
			Nodes.push_back(std::move(root));
			container.Children = { static_cast<uint32_t>(Nodes.size() - 1) };
			Nodes[0] = std::move(container);
			return 0;
		}
		const int parent = GetParents()[index];
		container.Children = { index };
		Nodes.push_back(std::move(container));
		const auto wrapped = static_cast<uint32_t>(Nodes.size() - 1);
		if (parent >= 0)
		{
			auto& siblings = Nodes[static_cast<size_t>(parent)].Children;
			std::replace(siblings.begin(), siblings.end(), index, wrapped);
		}
		return wrapped;
	}

	void Sound::RemoveNode(uint32_t index)
	{
		if (index == 0 || index >= Nodes.size())
			return;
		const int parent = GetParents()[index];
		std::vector<bool> removed(Nodes.size(), false);
		std::vector<uint32_t> pending{ index };
		while (!pending.empty())
		{
			const uint32_t next = pending.back();
			pending.pop_back();
			if (next >= removed.size() || removed[next])
				continue;
			removed[next] = true;
			pending.insert(pending.end(), Nodes[next].Children.begin(), Nodes[next].Children.end());
		}
		if (parent >= 0)
			std::erase(Nodes[static_cast<size_t>(parent)].Children, index);

		std::vector<uint32_t> renumbered(Nodes.size(), 0);
		std::vector<SoundNode> kept;
		for (size_t node = 0; node < Nodes.size(); ++node)
		{
			if (removed[node])
				continue;
			renumbered[node] = static_cast<uint32_t>(kept.size());
			kept.push_back(std::move(Nodes[node]));
		}
		for (auto& node : kept)
		{
			std::erase_if(node.Children, [&](uint32_t child) { return child >= removed.size() || removed[child]; });
			for (auto& child : node.Children)
				child = renumbered[child];
		}
		Nodes = std::move(kept);
	}

	void Sound::RenameParameterUses(const std::string& from, const std::string& to)
	{
		for (auto& node : Nodes)
		{
			if (Utils::String::EqualsIgnoreCase(node.SwitchParameter, from))
				node.SwitchParameter = to;
			for (auto& curve : node.Curves)
			{
				if (Utils::String::EqualsIgnoreCase(curve.Parameter, from))
					curve.Parameter = to;
			}
		}
	}

	void Sound::RemoveParameterUses(const std::string& name)
	{
		for (auto& node : Nodes)
		{
			if (Utils::String::EqualsIgnoreCase(node.SwitchParameter, name))
				node.SwitchParameter.clear();
			std::erase_if(node.Curves, [&](const SoundCurve& curve) { return Utils::String::EqualsIgnoreCase(curve.Parameter, name); });
		}
	}

	bool Sound::Validate(std::string* error) const
	{
		if (Bus.empty() || Bus.size() > kMaxBusBytes || !Bus.starts_with("bus:/") || Bus.find('\0') != std::string::npos)
			return Fail(error, "the bus must be a bus:/ path of at most 256 bytes");
		if (!Finite(Volume) || Volume < 0.0f || Volume > kMaxVolume)
			return Fail(error, "volume must be finite and between 0 and 4");
		if (!Finite(Pitch) || Pitch <= 0.0f || Pitch > kMaxPitch)
			return Fail(error, "pitch must be finite, above 0 and at most 4");
		if (!Finite(Spatial.MinDistance) || !Finite(Spatial.MaxDistance) || Spatial.MinDistance <= 0.0f || Spatial.MaxDistance < Spatial.MinDistance)
			return Fail(error, "distances must be finite, with 0 < minimum <= maximum");
		if (!Finite(Spatial.Rolloff) || Spatial.Rolloff < 0.0f)
			return Fail(error, "rolloff must be finite and not negative");
		if (InstanceLimit > MaxInstanceLimit)
			return Fail(error, "the instance limit must be at most 256 (0 is unlimited)");
		if (!Finite(Cooldown) || Cooldown < 0.0f || Cooldown > kMaxCooldown)
			return Fail(error, "the cooldown must be between 0 and 60 seconds");
		if (!Finite(Tempo.BPM) || (Tempo.BPM != 0.0f && (Tempo.BPM < 20.0f || Tempo.BPM > 400.0f)))
			return Fail(error, "the tempo must be 0 (none) or 20-400 BPM");
		if (Tempo.BeatsPerBar < 1 || Tempo.BeatsPerBar > 32 || (Tempo.BeatUnit != 1 && Tempo.BeatUnit != 2 && Tempo.BeatUnit != 4 && Tempo.BeatUnit != 8 && Tempo.BeatUnit != 16))
			return Fail(error, "the time signature must be 1-32 beats over 1, 2, 4, 8 or 16");
		if (!Finite(Tempo.Offset) || Tempo.Offset < 0.0f || Tempo.Offset > 60.0f)
			return Fail(error, "the beat offset must be 0-60 seconds");
		if (Markers.size() > 256)
			return Fail(error, "a sound has at most 256 markers");
		for (const auto& marker : Markers)
			if (marker.Name.empty() || !Finite(marker.Seconds) || marker.Seconds < 0.0f)
				return Fail(error, "markers need a name and a time of 0 seconds or later");

		if (Parameters.size() > MaxParameters)
			return Fail(error, "a sound has at most 32 parameters");
		std::unordered_set<std::string> names;
		for (const auto& parameter : Parameters)
		{
			if (!ValidName(parameter.Name) || !names.insert(Utils::String::ToLowerCopy(parameter.Name)).second)
				return Fail(error, "parameter names must be unique ignoring case, 1 to 64 bytes");
			if (parameter.IsLabeled())
			{
				std::unordered_set<std::string> labels;
				if (parameter.Labels.size() > kMaxLabels)
					return Fail(error, "parameter '" + parameter.Name + "' has more than 64 labels");
				for (const auto& label : parameter.Labels)
				{
					if (!ValidName(label) || !labels.insert(label).second)
						return Fail(error, "parameter '" + parameter.Name + "' needs unique labels of 1 to 64 bytes");
				}
				if (parameter.Minimum != 0.0f || parameter.Maximum != static_cast<float>(parameter.Labels.size() - 1)
					|| parameter.Default < 0.0f || parameter.Default > parameter.Maximum || std::floor(parameter.Default) != parameter.Default)
					return Fail(error, "labelled parameter '" + parameter.Name + "' must range over its label indices with a label as default");
			}
			else if (!Finite(parameter.Minimum) || !Finite(parameter.Maximum) || parameter.Minimum >= parameter.Maximum
				|| !Finite(parameter.Default) || parameter.Default < parameter.Minimum || parameter.Default > parameter.Maximum)
				return Fail(error, "parameter '" + parameter.Name + "' needs a finite range with the default inside it");
		}

		if (Nodes.empty() || Nodes.size() > MaxNodes)
			return Fail(error, "a sound has 1 to 1024 nodes");
		// A tree rooted at node 0: every other node has exactly one parent and is reachable.
		std::vector<uint32_t> parents(Nodes.size(), 0);
		for (size_t index = 0; index < Nodes.size(); ++index)
		{
			const auto& node = Nodes[index];
			const std::string where = "node " + std::to_string(index);
			if (node.Name.size() > kMaxNameBytes || node.Name.find('\0') != std::string::npos)
				return Fail(error, where + " has a name longer than 64 bytes");
			if (node.Type == SoundNodeType::Clip && !node.Children.empty())
				return Fail(error, where + " is a clip and cannot have children");
			for (const uint32_t child : node.Children)
			{
				if (child == 0 || child >= Nodes.size() || ++parents[child] > 1)
					return Fail(error, where + " has a child that is the root, missing, or already parented");
			}
			if (!Finite(node.StartOffset) || node.StartOffset < 0.0f)
				return Fail(error, where + " needs a finite, non-negative start offset");
			if (node.HasLoopRegion && (!Finite(node.LoopStart) || !Finite(node.LoopEnd) || node.LoopStart < 0.0f || node.LoopStart >= node.LoopEnd))
				return Fail(error, where + " needs a loop region with 0 <= start < end");
			if (!Finite(node.Weight) || node.Weight < 0.0f)
				return Fail(error, where + " needs a finite, non-negative weight");
			if (!Finite(node.Volume) || node.Volume < 0.0f || node.Volume > kMaxVolume || !Finite(node.Pitch) || node.Pitch <= 0.0f || node.Pitch > kMaxPitch)
				return Fail(error, where + " needs volume 0 to 4 and pitch above 0 up to 4");
			if (!Finite(node.VolumeVariation) || node.VolumeVariation < 0.0f || node.VolumeVariation > kMaxVolumeVariationDb
				|| !Finite(node.PitchVariation) || node.PitchVariation < 0.0f || node.PitchVariation > kMaxPitchVariationSemitones)
				return Fail(error, where + " needs volume variation 0 to 24 dB and pitch variation 0 to 12 semitones");
			if (!node.SwitchParameter.empty())
			{
				const auto* parameter = FindParameter(node.SwitchParameter);
				if (!parameter || !parameter->IsLabeled())
					return Fail(error, where + " must switch on a labelled parameter of the sound");
			}
			for (const auto& curve : node.Curves)
			{
				if (!FindParameter(curve.Parameter))
					return Fail(error, where + " has a curve on a parameter the sound does not have");
				if (curve.Points.empty() || curve.Points.size() > kMaxCurvePoints)
					return Fail(error, where + " has a curve without points or with more than 64");
				for (size_t point = 0; point < curve.Points.size(); ++point)
				{
					if (!Finite(curve.Points[point].x) || !ValidTargetValue(curve.Target, curve.Points[point].y)
						|| (point > 0 && curve.Points[point].x <= curve.Points[point - 1].x))
						return Fail(error, where + " has a curve whose points are not in increasing order or out of range");
				}
			}
		}
		// Every node has at most one parent and none parents the root, so this walk terminates.
		std::vector<uint32_t> depth(Nodes.size(), 0);
		std::vector<uint32_t> pending{ 0 };
		depth[0] = 1;
		while (!pending.empty())
		{
			const uint32_t index = pending.back();
			pending.pop_back();
			if (depth[index] > MaxDepth)
				return Fail(error, "the tree is deeper than 64 levels");
			for (const uint32_t child : Nodes[index].Children)
			{
				depth[child] = depth[index] + 1;
				pending.push_back(child);
			}
		}
		if (std::find(depth.begin(), depth.end(), 0u) != depth.end())
			return Fail(error, "every node must be in the tree under node 0");
		return true;
	}

	std::string Sound::ToYAML() const
	{
		Yaml::Writer out;
		out << Yaml::BeginMap << Yaml::Key << "Sound" << Yaml::BeginMap;
		out << Yaml::Key << "Version" << Yaml::Value << kVersion;
		out << Yaml::Key << "Bus" << Yaml::Value << Bus;
		out << Yaml::Key << "Volume" << Yaml::Value << Volume;
		out << Yaml::Key << "Pitch" << Yaml::Value << Pitch;
		out << Yaml::Key << "InstanceLimit" << Yaml::Value << InstanceLimit;
		out << Yaml::Key << "Steal" << Yaml::Value << SoundStealPolicyName(Steal);
		out << Yaml::Key << "Cooldown" << Yaml::Value << Cooldown;
		out << Yaml::Key << "Stream" << Yaml::Value << Stream;
		if (Tempo.BPM > 0.0f)
		{
			out << Yaml::Key << "Tempo" << Yaml::Value << Yaml::Flow << Yaml::BeginMap;
			out << Yaml::Key << "BPM" << Yaml::Value << Tempo.BPM;
			out << Yaml::Key << "BeatsPerBar" << Yaml::Value << Tempo.BeatsPerBar;
			out << Yaml::Key << "BeatUnit" << Yaml::Value << Tempo.BeatUnit;
			out << Yaml::Key << "Offset" << Yaml::Value << Tempo.Offset;
			out << Yaml::EndMap;
		}
		if (!Markers.empty())
		{
			out << Yaml::Key << "Markers" << Yaml::Value << Yaml::BeginSeq;
			for (const auto& marker : Markers)
			{
				out << Yaml::Flow << Yaml::BeginMap;
				out << Yaml::Key << "Name" << Yaml::Value << marker.Name;
				out << Yaml::Key << "Seconds" << Yaml::Value << marker.Seconds;
				out << Yaml::EndMap;
			}
			out << Yaml::EndSeq;
		}
		out << Yaml::Key << "Spatial" << Yaml::BeginMap;
		out << Yaml::Key << "Enabled" << Yaml::Value << Spatial.Enabled;
		out << Yaml::Key << "Attenuation" << Yaml::Value << SoundAttenuationName(Spatial.Attenuation);
		out << Yaml::Key << "MinDistance" << Yaml::Value << Spatial.MinDistance;
		out << Yaml::Key << "MaxDistance" << Yaml::Value << Spatial.MaxDistance;
		out << Yaml::Key << "Rolloff" << Yaml::Value << Spatial.Rolloff;
		out << Yaml::EndMap;
		out << Yaml::Key << "Parameters" << Yaml::BeginSeq;
		for (const auto& parameter : Parameters)
		{
			out << Yaml::BeginMap;
			out << Yaml::Key << "Name" << Yaml::Value << parameter.Name;
			if (parameter.IsLabeled())
				out << Yaml::Key << "Labels" << Yaml::Value << parameter.Labels;
			else
			{
				out << Yaml::Key << "Minimum" << Yaml::Value << parameter.Minimum;
				out << Yaml::Key << "Maximum" << Yaml::Value << parameter.Maximum;
			}
			out << Yaml::Key << "Default" << Yaml::Value << parameter.Default;
			if (parameter.Global)
				out << Yaml::Key << "Global" << Yaml::Value << true;
			out << Yaml::EndMap;
		}
		out << Yaml::EndSeq;
		out << Yaml::Key << "Nodes" << Yaml::BeginSeq;
		for (const auto& node : Nodes)
		{
			out << Yaml::BeginMap;
			out << Yaml::Key << "Type" << Yaml::Value << SoundNodeTypeName(node.Type);
			if (!node.Name.empty())
				out << Yaml::Key << "Name" << Yaml::Value << node.Name;
			if (node.Type == SoundNodeType::Clip)
			{
				out << Yaml::Key << "Clip" << Yaml::Value << node.Clip;
				out << Yaml::Key << "StartOffset" << Yaml::Value << node.StartOffset;
				out << Yaml::Key << "Loop" << Yaml::Value << node.Loop;
				if (node.HasLoopRegion)
					out << Yaml::Key << "LoopRegion" << Yaml::Value << glm::vec2(node.LoopStart, node.LoopEnd);
			}
			else
				out << Yaml::Key << "Children" << Yaml::Value << node.Children;
			if (node.Type == SoundNodeType::Random)
				out << Yaml::Key << "AvoidRepeat" << Yaml::Value << node.AvoidRepeat;
			if (node.Type == SoundNodeType::Switch)
				out << Yaml::Key << "SwitchParameter" << Yaml::Value << node.SwitchParameter;
			out << Yaml::Key << "Weight" << Yaml::Value << node.Weight;
			out << Yaml::Key << "Volume" << Yaml::Value << node.Volume;
			out << Yaml::Key << "Pitch" << Yaml::Value << node.Pitch;
			out << Yaml::Key << "VolumeVariation" << Yaml::Value << node.VolumeVariation;
			out << Yaml::Key << "PitchVariation" << Yaml::Value << node.PitchVariation;
			if (!node.Curves.empty())
			{
				out << Yaml::Key << "Curves" << Yaml::BeginSeq;
				for (const auto& curve : node.Curves)
				{
					out << Yaml::BeginMap;
					out << Yaml::Key << "Parameter" << Yaml::Value << curve.Parameter;
					out << Yaml::Key << "Target" << Yaml::Value << SoundCurveTargetName(curve.Target);
					out << Yaml::Key << "Points" << Yaml::Value << curve.Points;
					out << Yaml::EndMap;
				}
				out << Yaml::EndSeq;
			}
			out << Yaml::EndMap;
		}
		out << Yaml::EndSeq;
		out << Yaml::EndMap << Yaml::EndMap;
		return out.c_str();
	}

	bool Sound::FromYAML(const std::string& text)
	{
		try
		{
			const auto node = Yaml::Load(text)["Sound"];
			if (!node || !node.IsMap())
				throw std::runtime_error("missing Sound map");
			const int version = node["Version"].as<int>(1);
			if (version != 1 && version != kVersion)
				throw std::runtime_error("unsupported version");
			Sound parsed;
			parsed.Bus = node["Bus"].as<std::string>(parsed.Bus);
			parsed.Volume = node["Volume"].as<float>(parsed.Volume);
			parsed.Pitch = node["Pitch"].as<float>(parsed.Pitch);
			parsed.InstanceLimit = node["InstanceLimit"].as<uint32_t>(parsed.InstanceLimit);
			if (const auto steal = node["Steal"]; steal && !ParseSoundStealPolicy(steal.as<std::string>(""), parsed.Steal))
				throw std::runtime_error("unknown steal policy");
			parsed.Cooldown = node["Cooldown"].as<float>(parsed.Cooldown);
			parsed.Stream = node["Stream"].as<bool>(parsed.Stream);
			if (const auto tempo = node["Tempo"])
			{
				parsed.Tempo.BPM = tempo["BPM"].as<float>(0.0f);
				parsed.Tempo.BeatsPerBar = tempo["BeatsPerBar"].as<uint32_t>(4);
				parsed.Tempo.BeatUnit = tempo["BeatUnit"].as<uint32_t>(4);
				parsed.Tempo.Offset = tempo["Offset"].as<float>(0.0f);
			}
			if (const auto markers = node["Markers"])
			{
				if (!markers.IsSequence())
					throw std::runtime_error("Markers must be a list");
				for (const auto& marker : markers)
					parsed.Markers.push_back({ marker["Name"].as<std::string>(""), marker["Seconds"].as<float>(0.0f) });
			}
			if (const auto spatial = node["Spatial"])
			{
				if (!spatial.IsMap())
					throw std::runtime_error("Spatial must be a map");
				parsed.Spatial.Enabled = spatial["Enabled"].as<bool>(parsed.Spatial.Enabled);
				if (const auto attenuation = spatial["Attenuation"];
					attenuation && !ParseSoundAttenuation(attenuation.as<std::string>(""), parsed.Spatial.Attenuation))
					throw std::runtime_error("unknown attenuation model");
				parsed.Spatial.MinDistance = spatial["MinDistance"].as<float>(parsed.Spatial.MinDistance);
				parsed.Spatial.MaxDistance = spatial["MaxDistance"].as<float>(parsed.Spatial.MaxDistance);
				parsed.Spatial.Rolloff = spatial["Rolloff"].as<float>(parsed.Spatial.Rolloff);
			}

			if (version == 1)
			{
				// Phase 2's single-clip form: its clip and loop flag become the root clip node.
				parsed.Nodes.front().Clip = node["Clip"].as<uint64_t>(0);
				parsed.Nodes.front().Loop = node["Loop"].as<bool>(false);
			}
			else
			{
				if (const auto parameters = node["Parameters"])
				{
					if (!parameters.IsSequence() || parameters.size() > MaxParameters)
						throw std::runtime_error("Parameters must be a list of at most 32");
					for (const auto& entry : parameters)
					{
						SoundParameter parameter;
						parameter.Name = entry["Name"].as<std::string>("");
						if (const auto labels = entry["Labels"])
						{
							if (!labels.IsSequence() || labels.size() > kMaxLabels || labels.size() == 0)
								throw std::runtime_error("Labels must be a list of 1 to 64 names");
							for (const auto& label : labels)
								parameter.Labels.push_back(label.as<std::string>());
							parameter.Minimum = 0.0f;
							parameter.Maximum = static_cast<float>(parameter.Labels.size() - 1);
						}
						else
						{
							parameter.Minimum = entry["Minimum"].as<float>(parameter.Minimum);
							parameter.Maximum = entry["Maximum"].as<float>(parameter.Maximum);
						}
						parameter.Default = entry["Default"].as<float>(parameter.Minimum);
						parameter.Global = entry["Global"].as<bool>(false);
						parsed.Parameters.push_back(std::move(parameter));
					}
				}
				const auto nodes = node["Nodes"];
				if (!nodes || !nodes.IsSequence() || nodes.size() == 0 || nodes.size() > MaxNodes)
					throw std::runtime_error("Nodes must be a list of 1 to 1024 nodes");
				parsed.Nodes.clear();
				for (const auto& entry : nodes)
				{
					SoundNode soundNode;
					if (!ParseSoundNodeType(entry["Type"].as<std::string>(""), soundNode.Type))
						throw std::runtime_error("unknown node type");
					soundNode.Name = entry["Name"].as<std::string>("");
					soundNode.Clip = entry["Clip"].as<uint64_t>(0);
					soundNode.StartOffset = entry["StartOffset"].as<float>(0.0f);
					soundNode.Loop = entry["Loop"].as<bool>(false);
					if (const auto region = entry["LoopRegion"])
					{
						const auto value = region.as<glm::vec2>();
						soundNode.HasLoopRegion = true;
						soundNode.LoopStart = value.x;
						soundNode.LoopEnd = value.y;
					}
					if (const auto children = entry["Children"])
					{
						if (!children.IsSequence() || children.size() > MaxNodes)
							throw std::runtime_error("Children must be a list of node indices");
						for (const auto& child : children)
							soundNode.Children.push_back(child.as<uint32_t>());
					}
					soundNode.AvoidRepeat = entry["AvoidRepeat"].as<bool>(false);
					soundNode.SwitchParameter = entry["SwitchParameter"].as<std::string>("");
					soundNode.Weight = entry["Weight"].as<float>(1.0f);
					soundNode.Volume = entry["Volume"].as<float>(1.0f);
					soundNode.Pitch = entry["Pitch"].as<float>(1.0f);
					soundNode.VolumeVariation = entry["VolumeVariation"].as<float>(0.0f);
					soundNode.PitchVariation = entry["PitchVariation"].as<float>(0.0f);
					if (const auto curves = entry["Curves"])
					{
						if (!curves.IsSequence())
							throw std::runtime_error("Curves must be a list");
						for (const auto& value : curves)
						{
							SoundCurve curve;
							curve.Parameter = value["Parameter"].as<std::string>("");
							if (!ParseSoundCurveTarget(value["Target"].as<std::string>(""), curve.Target))
								throw std::runtime_error("unknown curve target");
							const auto points = value["Points"];
							if (!points || !points.IsSequence() || points.size() > kMaxCurvePoints)
								throw std::runtime_error("curve Points must be a list of at most 64 points");
							for (const auto& point : points)
								curve.Points.push_back(point.as<glm::vec2>());
							soundNode.Curves.push_back(std::move(curve));
						}
					}
					parsed.Nodes.push_back(std::move(soundNode));
				}
			}

			std::string error;
			if (!parsed.Validate(&error))
				throw std::runtime_error(error);
			Nodes = std::move(parsed.Nodes);
			Parameters = std::move(parsed.Parameters);
			Bus = std::move(parsed.Bus);
			Volume = parsed.Volume;
			Pitch = parsed.Pitch;
			Spatial = parsed.Spatial;
			InstanceLimit = parsed.InstanceLimit;
			Steal = parsed.Steal;
			Cooldown = parsed.Cooldown;
			Stream = parsed.Stream;
			Tempo = parsed.Tempo;
			Markers = std::move(parsed.Markers);
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid sound: {}", error.what());
			return false;
		}
	}
}
