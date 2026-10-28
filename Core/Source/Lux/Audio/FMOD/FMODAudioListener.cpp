// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioListener.h"

#include "Lux/Audio/AudioEngine.h"
#include "FMODInternal.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <fmod_studio.hpp>
#include <fmod_errors.h>

namespace Lux {

	namespace {

		// Distinct slots/operations must not reset each other's error suppression each frame.
		std::array<std::array<FMOD_RESULT, AudioListener::MaxListeners>, 4> s_LastResults{};

		bool CheckListenerResult(FMOD_RESULT result, int operation, int index, const char* name)
		{
			auto& last = s_LastResults[operation][index];
			if (result != FMOD_OK && result != last)
				LUX_CORE_ERROR_TAG("Audio", "Listener {0}: {1}: {2}", index, name, FMOD_ErrorString(result));
			last = result;
			return result == FMOD_OK;
		}
	}

	void AudioListener::Apply(const States& listeners)
	{
		LUX_PROFILE_FUNCTION("AudioListener::Apply");
		if (!AudioEngine::HasInitializedEngine())
			return;

		const int primary = GetPrimaryIndex(listeners);
		const AudioListenerState fallback;
		const auto& primaryState = primary >= 0 ? listeners[primary] : fallback;
		auto* studio = FMODInternal::GetStudioSystem();
		int count = 1;
		float totalWeight = 0.0f;
		for (int index = 0; index < MaxListeners; ++index)
		{
			if (std::isfinite(listeners[index].Weight) && listeners[index].Weight > 0.0f)
			{
				count = index + 1;
				totalWeight += std::clamp(listeners[index].Weight, 0.0f, 1.0f);
			}
		}
		int currentCount = 0;
		if (!CheckListenerResult(studio->getNumListeners(&currentCount), 0, 0, "getNumListeners"))
			return;
		if (currentCount != count && !CheckListenerResult(studio->setNumListeners(count), 1, 0, "setNumListeners"))
			return;

		for (int index = 0; index < count; ++index)
		{
			const float authoredWeight = std::isfinite(listeners[index].Weight) ? std::clamp(listeners[index].Weight, 0.0f, 1.0f) : 0.0f;
			// Keep unused slots at a valid camera transform; their Studio weight remains zero.
			const auto& state = authoredWeight > 0.0f ? listeners[index] : primaryState;
			const FMOD_3D_ATTRIBUTES attributes{
				{ state.Position.x, state.Position.y, state.Position.z },
				{ state.Velocity.x, state.Velocity.y, state.Velocity.z },
				{ state.Forward.x, state.Forward.y, state.Forward.z },
				{ state.Up.x, state.Up.y, state.Up.z }
			};
			const FMOD_VECTOR attenuation{ state.AttenuationPosition.x, state.AttenuationPosition.y, state.AttenuationPosition.z };
			CheckListenerResult(studio->setListenerAttributes(index, &attributes, state.UseAttenuationPosition ? &attenuation : nullptr),
				2, index, "setListenerAttributes");
			// Studio requires a nonzero total. Weights are relative; an empty scene uses a neutral
			// origin listener so a deleted camera cannot leave a stale location behind.
			const float weight = primary < 0 ? 1.0f : authoredWeight / totalWeight;
			CheckListenerResult(studio->setListenerWeight(index, weight), 3, index, "setListenerWeight");
		}
	}
}
