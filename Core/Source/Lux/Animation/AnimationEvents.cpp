// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AnimationEvents.h"
#include "Lux/Asset/AssetReference.h"

#include "Lux/Serialization/Yaml.h"

#include <cmath>

namespace Lux
{
	namespace
	{
		// "Model|Walk" and "Walk" name the same clip.
		std::string ShortName(const std::string& name)
		{
			const size_t bar = name.rfind('|');
			return bar == std::string::npos ? name : name.substr(bar + 1);
		}
	}

	bool ApplyAnimationEvents(const std::string& yaml, std::vector<Ref<Animation>>& clips, std::vector<std::string>* warnings)
	{
		try
		{
			const auto root = Yaml::Load(yaml)["AnimationEvents"];
			if (!root || root["Version"].as<int>(1) != 1)
				throw std::runtime_error("missing AnimationEvents or unsupported version");

			struct Parsed
			{
				std::string Clip;
				std::vector<AnimationEvent> Events;
			};
			std::vector<Parsed> parsed;
			const auto entries = root["Clips"];
			for (size_t i = 0; entries && i < entries.size(); i++)
			{
				Parsed entry;
				entry.Clip = entries[i]["Clip"].as<std::string>("");
				if (entry.Clip.empty())
					throw std::runtime_error("a clip entry has no Clip name");
				const auto events = entries[i]["Events"];
				for (size_t e = 0; events && e < events.size(); e++)
				{
					AnimationEvent event;
					event.Time = events[e]["Time"].as<float>(0.0f);
					event.Name = events[e]["Name"].as<std::string>("");
					event.Argument = events[e]["Argument"].as<std::string>("");
					event.Sound = AssetReference::ReadValue(events[e]["Sound"]);
					if (event.Name.empty() || !std::isfinite(event.Time) || event.Time < 0.0f)
						throw std::runtime_error("an event needs a Name and a Time of 0 or more");
					entry.Events.push_back(std::move(event));
				}
				if (entry.Events.size() > Animation::MaxEvents)
					throw std::runtime_error("too many events on one clip");
				parsed.push_back(std::move(entry));
			}

			for (Parsed& entry : parsed)
			{
				bool matched = false;
				for (Ref<Animation>& clip : clips)
				{
					if (clip->GetName() != entry.Clip && ShortName(clip->GetName()) != ShortName(entry.Clip))
						continue;
					matched = true;
					std::vector<AnimationEvent> kept;
					for (const AnimationEvent& event : entry.Events)
					{
						if (event.Time > clip->GetDuration())
						{
							if (warnings)
								warnings->push_back("event '" + event.Name + "' is after the end of clip '" + clip->GetName() + "'");
							continue;
						}
						kept.push_back(event);
					}
					clip->SetEvents(std::move(kept));
				}
				if (!matched && warnings)
					warnings->push_back("no clip named '" + entry.Clip + "'");
			}
			return true;
		}
		catch (const std::exception& error)
		{
			LUX_CORE_ERROR("Invalid animation events file: {}", error.what());
			return false;
		}
	}
}
