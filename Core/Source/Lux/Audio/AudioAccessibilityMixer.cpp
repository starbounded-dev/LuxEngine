// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioAccessibilityMixer.h"
#include "AudioEngine.h"
#include <fmod.hpp>
#include <fmod_studio.hpp>
#include <fmod_errors.h>
#include <cmath>

namespace Lux
{
	namespace
	{
		bool Check(FMOD_RESULT result, const char* operation)
		{
			if (result == FMOD_OK)
				return true;
			LUX_CORE_ERROR_TAG("Audio", "Accessibility mixer {}: {}", operation, FMOD_ErrorString(result));
			return false;
		}
	}

	struct AudioAccessibilityMixer::Impl
	{
		struct Bus
		{
			FMOD::Studio::Bus* Studio = nullptr;
			FMOD::ChannelGroup* Group = nullptr;
			FMOD::DSP* Gain = nullptr;
			bool Attached = false;
		};
		std::array<Bus, AudioCategoryCount> Buses;
		FMOD::ChannelGroup* Master = nullptr;
		FMOD::DSP* Mono = nullptr;
		FMOD::DSP* Compressor = nullptr;
		bool MonoAttached = false, CompressorAttached = false;
		float Duck = 0.25f;
	};

	AudioAccessibilityMixer::AudioAccessibilityMixer() : m_Impl(CreateScope<Impl>())
	{
	}
	AudioAccessibilityMixer::~AudioAccessibilityMixer()
	{
		Reset();
	}

	void AudioAccessibilityMixer::Reset()
	{
		for (auto& bus : m_Impl->Buses)
		{
			if (bus.Gain)
			{
				if (bus.Attached)
					Check(bus.Group->removeDSP(bus.Gain), "detach bus gain");
				Check(bus.Gain->release(), "release bus gain");
			}
			if (bus.Studio)
				Check(AudioEngine::UnlockBusChannelGroup(bus.Studio), "unlock bus");
			bus = {};
		}
		for (auto* dsp : { m_Impl->Mono, m_Impl->Compressor })
		{
			if (!dsp)
				continue;
			if (dsp == m_Impl->Mono ? m_Impl->MonoAttached : m_Impl->CompressorAttached)
				Check(m_Impl->Master->removeDSP(dsp), "detach master processing");
			Check(dsp->release(), "release master processing");
		}
		m_Impl->Mono = nullptr;
		m_Impl->Compressor = nullptr;
		m_Impl->Master = nullptr;
		m_Impl->MonoAttached = m_Impl->CompressorAttached = false;
	}

	bool AudioAccessibilityMixer::Configure(const AudioAccessibilityConfig& config)
	{
		Reset();
		auto* studio = AudioEngine::GetStudioSystem();
		auto* core = AudioEngine::GetEngine();
		if (!studio || !core || !config.Validate())
			return false;
		m_Impl->Duck = config.DescriptionDuck;
		if (!Check(core->getMasterChannelGroup(&m_Impl->Master), "get master group") ||
			!Check(core->createDSPByType(FMOD_DSP_TYPE_CHANNELMIX, &m_Impl->Mono), "create mono fold") ||
			!Check(core->createDSPByType(FMOD_DSP_TYPE_COMPRESSOR, &m_Impl->Compressor), "create compressor"))
		{
			Reset();
			return false;
		}
		bool success = Check(m_Impl->Mono->setParameterInt(FMOD_DSP_CHANNELMIX_OUTPUTGROUPING, FMOD_DSP_CHANNELMIX_OUTPUT_ALLMONO), "configure mono fold");
		success &= Check(m_Impl->Mono->setBypass(true), "bypass mono fold");
		success &= Check(m_Impl->Compressor->setBypass(true), "bypass compressor");
		m_Impl->MonoAttached = Check(m_Impl->Master->addDSP(FMOD_CHANNELCONTROL_DSP_TAIL, m_Impl->Mono), "attach mono fold");
		success &= m_Impl->MonoAttached;
		m_Impl->CompressorAttached = Check(m_Impl->Master->addDSP(FMOD_CHANNELCONTROL_DSP_TAIL, m_Impl->Compressor), "attach compressor");
		success &= m_Impl->CompressorAttached;
		for (size_t i = 0; i < AudioCategoryCount; ++i)
		{
			if (config.BusPaths[i].empty())
				continue;
			FMOD::Studio::Bus* studioBus = nullptr;
			const auto result = studio->getBus(config.BusPaths[i].c_str(), &studioBus);
			if (result != FMOD_OK)
			{
				LUX_CORE_ERROR_TAG("Audio", "Accessibility bus '{}' is unavailable: {}", config.BusPaths[i], FMOD_ErrorString(result));
				success = false;
				continue;
			}
			if (!Check(AudioEngine::LockBusChannelGroup(studioBus), "lock category bus"))
			{
				success = false;
				continue;
			}
			m_Impl->Buses[i].Studio = studioBus;
		}
		// Locking creates virtualized groups. Flush only at setup/bank changes, never per frame.
		if (!Check(studio->flushCommands(), "create locked bus groups"))
		{
			Reset();
			return false;
		}
		for (auto& bus : m_Impl->Buses)
		{
			if (!bus.Studio)
				continue;
			if (!Check(bus.Studio->getChannelGroup(&bus.Group), "get category group") ||
				!Check(core->createDSPByType(FMOD_DSP_TYPE_FADER, &bus.Gain), "create category gain") ||
				!(bus.Attached = Check(bus.Group->addDSP(FMOD_CHANNELCONTROL_DSP_TAIL, bus.Gain), "attach category gain")))
				success = false;
		}
		return success;
	}

	bool AudioAccessibilityMixer::Apply(const AudioAccessibilityPreferences& preferences, bool describing)
	{
		if (!preferences.Validate() || !m_Impl->Mono || !m_Impl->Compressor)
			return false;
		bool success = Check(m_Impl->Mono->setBypass(!preferences.Mono), "set mono mode");
		const bool night = preferences.DynamicRange == AudioDynamicRange::Night;
		success &= Check(m_Impl->Compressor->setParameterFloat(FMOD_DSP_COMPRESSOR_THRESHOLD, night ? -24.0f : -12.0f), "set compression threshold");
		success &= Check(m_Impl->Compressor->setParameterFloat(FMOD_DSP_COMPRESSOR_RATIO, night ? 6.0f : 3.0f), "set compression ratio");
		success &= Check(m_Impl->Compressor->setParameterFloat(FMOD_DSP_COMPRESSOR_ATTACK, 10.0f), "set compression attack");
		success &= Check(m_Impl->Compressor->setParameterFloat(FMOD_DSP_COMPRESSOR_RELEASE, 150.0f), "set compression release");
		success &= Check(m_Impl->Compressor->setParameterFloat(FMOD_DSP_COMPRESSOR_GAINMAKEUP, 0.0f), "set compression makeup");
		success &= Check(m_Impl->Compressor->setBypass(preferences.DynamicRange == AudioDynamicRange::Full), "set compression mode");
		for (size_t i = 0; i < AudioCategoryCount; ++i)
		{
			auto* gain = m_Impl->Buses[i].Gain;
			if (!m_Impl->Buses[i].Attached)
				continue;
			float volume = preferences.Volumes[i];
			if (i == static_cast<size_t>(AudioCategory::Dialogue))
				volume *= preferences.DialogueBoost;
			else if (describing && i != static_cast<size_t>(AudioCategory::Master))
				volume *= m_Impl->Duck;
			// Wet/dry zero is an exact mute; the gain control alone bottoms out at -80 dB.
			success &= Check(gain->setWetDryMix(1.0f, volume == 0 ? 0.0f : 1.0f, 0.0f), "set category mute");
			success &= Check(gain->setParameterFloat(FMOD_DSP_FADER_GAIN, volume > 0 ? std::max(-80.0f, 20.0f * std::log10(volume)) : -80.0f), "set category gain");
		}
		return success;
	}

	bool AudioAccessibilityMixer::HasBus(AudioCategory category) const
	{
		return category < AudioCategory::Count && m_Impl->Buses[static_cast<size_t>(category)].Attached;
	}
}
