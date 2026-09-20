// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioEngine.h"
#include "AudioPerformance.h"
#include <fmod.hpp>
#include <fmod_errors.h>

namespace Lux
{
	namespace
	{
		uint64_t s_FocusGeneration = UINT64_MAX;
		int s_LastFocusMute = -1;
	}

	void AudioEngine::SetApplicationFocused(bool focused)
	{
		auto* core = GetEngine();
		if (!core)
			return;
		const int muted = !focused && AudioPerformance::GetSettings().MuteWhenUnfocused;
		const auto generation = GetEventGeneration();
		if (s_FocusGeneration == generation && s_LastFocusMute == muted)
			return;
		s_FocusGeneration = generation;
		s_LastFocusMute = muted;
		// The Core output group is outside Studio's authored bus tree. Changing its mute
		// preserves bus gains/mutes and both script and scene pause state.
		FMOD::ChannelGroup* output = nullptr;
		auto result = core->getMasterChannelGroup(&output);
		if (result == FMOD_OK)
			result = output->setMute(muted != 0);
		if (result != FMOD_OK)
			LUX_CORE_ERROR_TAG("Audio", "Cannot apply application focus mute: {}", FMOD_ErrorString(result));
	}
}
