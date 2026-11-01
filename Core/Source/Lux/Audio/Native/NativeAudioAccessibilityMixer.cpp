// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioAccessibilityMixer.h"

// The native backend has no buses to attach category gains, mono or compression to until it has a
// mixer. Captions, sound cues and narration still run (they observe playback, not the mix), so
// the mixer accepts its configuration, reports the gap once and leaves the output unchanged.
namespace Lux
{
	namespace
	{
		bool s_Reported = false;
	}

	struct AudioAccessibilityMixer::Impl
	{
	};

	AudioAccessibilityMixer::AudioAccessibilityMixer() : m_Impl(CreateScope<Impl>())
	{
	}

	AudioAccessibilityMixer::~AudioAccessibilityMixer() = default;

	bool AudioAccessibilityMixer::Configure(const AudioAccessibilityConfig&)
	{
		if (!s_Reported)
		{
			s_Reported = true;
			LUX_CORE_INFO_TAG("Audio", "Accessibility category volumes, mono and compression need the native mixer; captions and sound cues work now");
		}
		return true;
	}

	bool AudioAccessibilityMixer::Apply(const AudioAccessibilityPreferences&, bool)
	{
		return true;
	}

	void AudioAccessibilityMixer::Reset()
	{
	}

	bool AudioAccessibilityMixer::HasBus(AudioCategory) const
	{
		return false;
	}
}
