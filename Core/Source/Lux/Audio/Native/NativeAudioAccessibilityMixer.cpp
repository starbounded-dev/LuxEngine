// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioAccessibilityMixer.h"
#include "NativeInternal.h"

#include <array>

// The same controls as the FMOD backend's: a category gain on each mapped bus, independent of the
// mix and of gameplay bus volumes; dialogue boost; ducking for audio descriptions; mono fold and
// compression on the master output.
namespace Lux
{
	struct AudioAccessibilityMixer::Impl
	{
		std::array<std::string, AudioCategoryCount> Buses; // the mapped bus per category, empty when unmapped
		float Duck = 0.25f;
		bool Configured = false;
	};

	AudioAccessibilityMixer::AudioAccessibilityMixer() : m_Impl(CreateScope<Impl>())
	{
	}

	AudioAccessibilityMixer::~AudioAccessibilityMixer()
	{
		Reset();
	}

	bool AudioAccessibilityMixer::Configure(const AudioAccessibilityConfig& config)
	{
		Reset();
		if (!NativeInternal::GetEngine() || !config.Validate())
			return false;
		bool success = true;
		for (size_t i = 0; i < AudioCategoryCount; ++i)
		{
			if (config.BusPaths[i].empty())
				continue;
			if (!NativeInternal::HasMixerBus(config.BusPaths[i]))
			{
				LUX_CORE_ERROR_TAG("Audio", "Accessibility bus '{}' is not in the project's mixer", config.BusPaths[i]);
				success = false;
				continue;
			}
			m_Impl->Buses[i] = config.BusPaths[i];
		}
		m_Impl->Duck = config.DescriptionDuck;
		m_Impl->Configured = true;
		return success;
	}

	bool AudioAccessibilityMixer::Apply(const AudioAccessibilityPreferences& preferences, bool describing)
	{
		if (!preferences.Validate() || !m_Impl->Configured || !NativeInternal::GetEngine())
			return false;
		NativeInternal::SetAccessibilityProcessing(preferences.Mono, preferences.DynamicRange);
		for (size_t i = 0; i < AudioCategoryCount; ++i)
		{
			if (m_Impl->Buses[i].empty())
				continue;
			float volume = preferences.Volumes[i];
			if (i == static_cast<size_t>(AudioCategory::Dialogue))
				volume *= preferences.DialogueBoost;
			else if (describing && i != static_cast<size_t>(AudioCategory::Master))
				volume *= m_Impl->Duck;
			NativeInternal::SetAccessibilityGain(m_Impl->Buses[i], volume);
		}
		return true;
	}

	void AudioAccessibilityMixer::Reset()
	{
		if (m_Impl->Configured && NativeInternal::GetEngine())
			NativeInternal::ResetAccessibility();
		m_Impl->Buses = {};
		m_Impl->Configured = false;
	}

	bool AudioAccessibilityMixer::HasBus(AudioCategory category) const
	{
		return category < AudioCategory::Count && !m_Impl->Buses[static_cast<size_t>(category)].empty();
	}
}
