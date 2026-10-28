// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioEngine.h"
#include "FMODInternal.h"

#include <fmod_studio.hpp>
#include <fmod_errors.h>

namespace Lux {

	namespace {

		constexpr size_t kLabelBufferSize = 1024;

		FMOD::Studio::EventDescription* Describe(const std::string& eventGuid)
		{
			FMOD_GUID id{};
			FMOD::Studio::EventDescription* description = nullptr;
			auto* studio = FMODInternal::GetStudioSystem();
			if (studio && FMOD::Studio::parseID(eventGuid.c_str(), &id) == FMOD_OK && studio->getEventByID(&id, &description) == FMOD_OK)
				return description;
			return nullptr;
		}

	}

	bool AudioEngine::GetEventParameter(const std::string& eventGuid, const std::string& name, AudioEventParameterInfo& outInfo)
	{
		FMOD::Studio::EventDescription* description = Describe(eventGuid);
		FMOD_STUDIO_PARAMETER_DESCRIPTION parameter{};
		if (!description || description->getParameterDescriptionByName(name.c_str(), &parameter) != FMOD_OK ||
			(parameter.flags & (FMOD_STUDIO_PARAMETER_READONLY | FMOD_STUDIO_PARAMETER_AUTOMATIC | FMOD_STUDIO_PARAMETER_GLOBAL)))
			return false;
		outInfo.Name = parameter.name;
		outInfo.Minimum = parameter.minimum;
		outInfo.Maximum = parameter.maximum;
		outInfo.Default = parameter.defaultvalue;
		outInfo.Discrete = (parameter.flags & FMOD_STUDIO_PARAMETER_DISCRETE) != 0;
		outInfo.Labeled = (parameter.flags & FMOD_STUDIO_PARAMETER_LABELED) != 0;
		return true;
	}

	bool AudioEngine::GetEventParameterLabel(const std::string& eventGuid, const std::string& name, int index,
		std::string& outLabel, std::string& outError)
	{
		FMOD::Studio::EventDescription* description = Describe(eventGuid);
		if (!description)
		{
			outError = "the event is not in a loaded bank";
			return false;
		}
		char label[kLabelBufferSize]{};
		int required = 0;
		auto result = description->getParameterLabelByName(name.c_str(), index, label, sizeof(label), &required);
		std::string extended;
		if (result == FMOD_ERR_TRUNCATED && required > 0)
		{
			extended.resize(static_cast<size_t>(required));
			result = description->getParameterLabelByName(name.c_str(), index, extended.data(), required, nullptr);
		}
		if (result != FMOD_OK)
		{
			outError = FMOD_ErrorString(result);
			return false;
		}
		// The retrieved size counts the terminator, so take the C string rather than the buffer.
		outLabel = extended.empty() ? label : extended.c_str();
		return true;
	}

}
