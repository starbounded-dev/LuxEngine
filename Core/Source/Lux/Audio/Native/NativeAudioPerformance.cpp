// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Audio/AudioPerformance.h"
#include "NativeInternal.h"

#include <cmath>

// Budgets and the voice count. Audio-thread CPU, memory and bus meters are not measured yet, so
// they report unavailable rather than zero.
namespace Lux
{
	namespace
	{
		AudioPerformanceSettings s_Settings;
		AudioPerformanceStats s_Stats;
		const std::vector<AudioBusMeter> s_NoBuses;
		bool s_WarnedVoices = false;
		bool s_WarnedRaytracing = false;
	}

	void AudioPerformance::Reset()
	{
		s_Stats = {};
		s_WarnedVoices = s_WarnedRaytracing = false;
	}

	bool AudioPerformance::Configure(const AudioPerformanceSettings& settings)
	{
		if (!settings.Validate())
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot configure invalid performance budgets");
			return false;
		}
		Reset();
		s_Settings = settings;
		return true;
	}

	void AudioPerformance::SubmitScene(double raytracingMs, size_t culled, bool available)
	{
		s_Stats.RaytracingAvailable = available && std::isfinite(raytracingMs) && raytracingMs >= 0;
		s_Stats.RaytracingMilliseconds = s_Stats.RaytracingAvailable ? raytracingMs : 0;
		s_Stats.CulledSources = culled;
	}

	void AudioPerformance::Update()
	{
		// Nothing is virtualized yet: every playing voice is real.
		s_Stats.RealVoices = NativeInternal::CountPlayingVoices();
		s_Stats.VirtualVoices = 0;
		s_Stats.VoicesExceeded = s_Stats.RealVoices > static_cast<int>(s_Settings.RealVoices);
		s_Stats.RaytracingExceeded = s_Stats.RaytracingAvailable && s_Stats.RaytracingMilliseconds > s_Settings.RaytracingMilliseconds;
		if (s_Stats.VoicesExceeded && !s_WarnedVoices)
		{
			s_WarnedVoices = true;
			LUX_CORE_WARN_TAG("Audio", "Voice demand budget exceeded: {} (budget {})", s_Stats.RealVoices, s_Settings.RealVoices);
		}
		if (s_Stats.RaytracingExceeded && !s_WarnedRaytracing)
		{
			s_WarnedRaytracing = true;
			LUX_CORE_WARN_TAG("Audio", "Asterwave raytracing (ms) budget exceeded: {} (budget {})", s_Stats.RaytracingMilliseconds, s_Settings.RaytracingMilliseconds);
		}
	}

	const AudioPerformanceSettings& AudioPerformance::GetSettings() { return s_Settings; }
	const AudioPerformanceStats& AudioPerformance::GetStats() { return s_Stats; }
	const std::vector<AudioBusMeter>& AudioPerformance::GetBuses() { return s_NoBuses; }
}
