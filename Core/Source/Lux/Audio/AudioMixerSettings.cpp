// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioMixerSettings.h"
#include "Lux/Serialization/Yaml.h"

#include <cmath>
#include <set>

namespace Lux {

	namespace {
		constexpr const char* kMaster = "bus:/";
		constexpr float kMaxVolume = 4.0f;

		bool InRange(float value, float low, float high)
		{
			return std::isfinite(value) && value >= low && value <= high;
		}
	}

	std::vector<AudioMixerBus> AudioMixerSettings::DefaultBuses()
	{
		return { { "bus:/Music" }, { "bus:/SFX" }, { "bus:/Dialogue" }, { "bus:/Ambience" } };
	}

	std::string AudioMixerSettings::ParentPath(const std::string& path)
	{
		const size_t slash = path.find_last_of('/');
		// The master's own slash (index 4) keeps "bus:/"; a deeper one cuts the last name off.
		return slash == std::string::npos ? std::string(kMaster) : path.substr(0, std::max<size_t>(slash, 5));
	}

	bool AudioMixerSettings::Validate(std::string* outError) const
	{
		const auto fail = [&](const std::string& message)
		{
			if (outError)
				*outError = message;
			return false;
		};
		std::set<std::string> seen = { kMaster };
		for (const AudioMixerBus& bus : Buses)
		{
			if (bus.Path.size() <= 5 || bus.Path.rfind(kMaster, 0) != 0 || bus.Path.back() == '/' || bus.Path.find("//") != std::string::npos)
				return fail("bus path '" + bus.Path + "' must be bus:/ followed by names separated by '/'");
			if (!seen.insert(bus.Path).second)
				return fail("bus '" + bus.Path + "' is listed twice");
			if (!seen.contains(ParentPath(bus.Path)))
				return fail("bus '" + bus.Path + "' is listed before its parent '" + ParentPath(bus.Path) + "'");
			if (!InRange(bus.Volume, 0.0f, kMaxVolume))
				return fail("bus '" + bus.Path + "' volume must be 0-4");
		}
		if (!InRange(Reverb.RoomSize, 0.0f, 1.0f) || !InRange(Reverb.Damping, 0.0f, 1.0f) || !InRange(Reverb.Width, 0.0f, 1.0f)
			|| !InRange(Reverb.Volume, 0.0f, kMaxVolume))
			return fail("reverb return room size, damping and width must be 0-1, and its volume 0-4");
		std::set<uint64_t> ids;
		std::set<std::string> names;
		for (const AudioMixerSnapshot& snapshot : Snapshots)
		{
			if (static_cast<uint64_t>(snapshot.ID) == 0 || !ids.insert(static_cast<uint64_t>(snapshot.ID)).second)
				return fail("snapshot '" + snapshot.Name + "' needs a unique ID");
			if (snapshot.Name.empty() || !names.insert(snapshot.Name).second)
				return fail("snapshot names must be nonempty and unique ('" + snapshot.Name + "')");
			std::set<std::string> overridden;
			for (const AudioMixerBus& bus : snapshot.Buses)
			{
				if (bus.Path == kMaster || !seen.contains(bus.Path) || !overridden.insert(bus.Path).second)
					return fail("snapshot '" + snapshot.Name + "' overrides '" + bus.Path + "', which is not a mixer bus or is listed twice");
				if (!InRange(bus.Volume, 0.0f, kMaxVolume))
					return fail("snapshot '" + snapshot.Name + "' volume for '" + bus.Path + "' must be 0-4");
			}
		}
		return true;
	}

	void AudioMixerSettings::SerializeYAML(Yaml::Writer& out) const
	{
		out << Yaml::BeginMap;
		out << Yaml::Key << "Buses" << Yaml::Value << Yaml::BeginSeq;
		for (const AudioMixerBus& bus : Buses)
		{
			out << Yaml::Flow << Yaml::BeginMap;
			out << Yaml::Key << "Path" << Yaml::Value << bus.Path;
			out << Yaml::Key << "Volume" << Yaml::Value << bus.Volume;
			out << Yaml::Key << "Muted" << Yaml::Value << bus.Muted;
			out << Yaml::EndMap;
		}
		out << Yaml::EndSeq;
		out << Yaml::Key << "Reverb" << Yaml::Value << Yaml::BeginMap;
		out << Yaml::Key << "RoomSize" << Yaml::Value << Reverb.RoomSize;
		out << Yaml::Key << "Damping" << Yaml::Value << Reverb.Damping;
		out << Yaml::Key << "Width" << Yaml::Value << Reverb.Width;
		out << Yaml::Key << "Volume" << Yaml::Value << Reverb.Volume;
		out << Yaml::EndMap;
		out << Yaml::Key << "Snapshots" << Yaml::Value << Yaml::BeginSeq;
		for (const AudioMixerSnapshot& snapshot : Snapshots)
		{
			out << Yaml::BeginMap;
			out << Yaml::Key << "ID" << Yaml::Value << static_cast<uint64_t>(snapshot.ID);
			out << Yaml::Key << "Name" << Yaml::Value << snapshot.Name;
			out << Yaml::Key << "Priority" << Yaml::Value << snapshot.Priority;
			out << Yaml::Key << "Buses" << Yaml::Value << Yaml::BeginSeq;
			for (const AudioMixerBus& bus : snapshot.Buses)
			{
				out << Yaml::Flow << Yaml::BeginMap;
				out << Yaml::Key << "Path" << Yaml::Value << bus.Path;
				out << Yaml::Key << "Volume" << Yaml::Value << bus.Volume;
				out << Yaml::EndMap;
			}
			out << Yaml::EndSeq;
			out << Yaml::EndMap;
		}
		out << Yaml::EndSeq;
		out << Yaml::EndMap;
	}

	bool AudioMixerSettings::DeserializeYAML(const Yaml::Node& node)
	{
		try
		{
			AudioMixerSettings parsed;
			if (node)
			{
				if (!node.IsMap())
					throw std::runtime_error("expected a mixer map");
				if (const auto buses = node["Buses"])
				{
					if (!buses.IsSequence())
						throw std::runtime_error("Buses must be a list");
					parsed.Buses.clear();
					for (const auto& entry : buses)
					{
						AudioMixerBus bus;
						bus.Path = entry["Path"].as<std::string>();
						bus.Volume = entry["Volume"].as<float>(bus.Volume);
						bus.Muted = entry["Muted"].as<bool>(bus.Muted);
						parsed.Buses.push_back(std::move(bus));
					}
				}
				if (const auto reverb = node["Reverb"])
				{
					parsed.Reverb.RoomSize = reverb["RoomSize"].as<float>(parsed.Reverb.RoomSize);
					parsed.Reverb.Damping = reverb["Damping"].as<float>(parsed.Reverb.Damping);
					parsed.Reverb.Width = reverb["Width"].as<float>(parsed.Reverb.Width);
					parsed.Reverb.Volume = reverb["Volume"].as<float>(parsed.Reverb.Volume);
				}
				if (const auto snapshots = node["Snapshots"])
				{
					if (!snapshots.IsSequence())
						throw std::runtime_error("Snapshots must be a list");
					for (const auto& entry : snapshots)
					{
						AudioMixerSnapshot snapshot;
						snapshot.ID = entry["ID"].as<uint64_t>();
						snapshot.Name = entry["Name"].as<std::string>();
						snapshot.Priority = entry["Priority"].as<int>(0);
						if (const auto buses = entry["Buses"])
						{
							for (const auto& bus : buses)
								snapshot.Buses.push_back({ bus["Path"].as<std::string>(), bus["Volume"].as<float>(1.0f) });
						}
						parsed.Snapshots.push_back(std::move(snapshot));
					}
				}
			}
			std::string error;
			if (!parsed.Validate(&error))
				throw std::runtime_error(error);
			*this = std::move(parsed);
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid mixer settings: {}", error.what());
			return false;
		}
	}

}
