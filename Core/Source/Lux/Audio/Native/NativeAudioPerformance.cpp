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
		std::vector<AudioBusMeter> s_Buses;
		std::vector<bool> s_WarnedBuses;
		bool s_WarnedVoices = false;
		bool s_WarnedRaytracing = false;
		bool s_WarnedCPU = false, s_WarnedMemory = false;
	}

	void AudioPerformance::Reset()
	{
		s_Stats = {};
		s_WarnedVoices = s_WarnedRaytracing = s_WarnedCPU = s_WarnedMemory = false;
		s_Buses.clear();
		s_WarnedBuses.clear();
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
		for (const auto& [path, limit] : settings.BusVoices)
			s_Buses.push_back({ path, limit });
		s_WarnedBuses.assign(s_Buses.size(), false);
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
		// The budget virtualizes what does not fit, so demand is the real and virtual voices together.
		s_Stats.VirtualVoices = NativeInternal::ApplyVoiceBudget(s_Settings.RealVoices);
		s_Stats.RealVoices = NativeInternal::CountPlayingVoices() - s_Stats.VirtualVoices;
		const int demand = s_Stats.RealVoices + s_Stats.VirtualVoices;
		s_Stats.VoicesExceeded = demand > static_cast<int>(s_Settings.RealVoices);
		s_Stats.RaytracingExceeded = s_Stats.RaytracingAvailable && s_Stats.RaytracingMilliseconds > s_Settings.RaytracingMilliseconds;
		// The device callback's share of real time; unavailable (not 0) with no device.
		float load = 0.0f;
		s_Stats.MixerAvailable = NativeInternal::ReadAudioLoad(load);
		s_Stats.CPUPercent = s_Stats.MixerAvailable ? load : 0.0f;
		s_Stats.CPUExceeded = s_Stats.MixerAvailable && s_Stats.CPUPercent > s_Settings.CPUPercent;
		s_Stats.MemoryAvailable = true;
		s_Stats.MemoryMiB = static_cast<double>(NativeInternal::GetClipMemoryBytes()) / (1024.0 * 1024.0);
		s_Stats.MemoryExceeded = s_Stats.MemoryMiB > s_Settings.BankMemoryMiB;
		if (s_Stats.CPUExceeded && !s_WarnedCPU)
		{
			s_WarnedCPU = true;
			LUX_CORE_WARN_TAG("Audio", "Audio CPU (%) budget exceeded: {} (budget {})", s_Stats.CPUPercent, s_Settings.CPUPercent);
		}
		if (s_Stats.MemoryExceeded && !s_WarnedMemory)
		{
			s_WarnedMemory = true;
			LUX_CORE_WARN_TAG("Audio", "Audio clip memory (MiB) budget exceeded: {} (budget {})", s_Stats.MemoryMiB, s_Settings.BankMemoryMiB);
		}
		if (s_Stats.VoicesExceeded && !s_WarnedVoices)
		{
			s_WarnedVoices = true;
			LUX_CORE_WARN_TAG("Audio", "Voice demand budget exceeded: {} (budget {}); the lowest-priority voices are virtual", demand, s_Settings.RealVoices);
		}
		for (size_t i = 0; i < s_Buses.size(); ++i)
		{
			// Inclusive counts: a parent counts the voices of the buses below it.
			AudioBusMeter& meter = s_Buses[i];
			meter.Voices = meter.RealVoices = NativeInternal::CountPlayingVoicesOn(meter.Path);
			meter.Available = NativeInternal::ReadBusMeter(meter.Path, meter.Peak, meter.RMS);
			meter.Exceeded = meter.Available && meter.Voices > static_cast<int>(meter.Limit);
			if (meter.Exceeded && !s_WarnedBuses[i])
			{
				s_WarnedBuses[i] = true;
				LUX_CORE_WARN_TAG("Audio", "Bus '{}' voice budget exceeded: {} (budget {})", meter.Path, meter.Voices, meter.Limit);
			}
		}
		if (s_Stats.RaytracingExceeded && !s_WarnedRaytracing)
		{
			s_WarnedRaytracing = true;
			LUX_CORE_WARN_TAG("Audio", "Soundwave acoustics (ms) budget exceeded: {} (budget {})", s_Stats.RaytracingMilliseconds, s_Settings.RaytracingMilliseconds);
		}
	}

	const AudioPerformanceSettings& AudioPerformance::GetSettings() { return s_Settings; }
	const AudioPerformanceStats& AudioPerformance::GetStats() { return s_Stats; }
	const std::vector<AudioBusMeter>& AudioPerformance::GetBuses() { return s_Buses; }
}
