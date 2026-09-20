// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once
#include "AudioPerformanceSettings.h"
#include <vector>
namespace Lux
{
	struct AudioBusMeter
	{
		std::string Path;
		uint32_t Limit = 0;
		int Voices = 0, RealVoices = 0;
		float Peak = 0, RMS = 0;
		bool Available = false, Exceeded = false;
	};
	struct AudioPerformanceStats
	{
		int RealVoices = 0, VirtualVoices = 0;
		float CPUPercent = 0;
		double MemoryMiB = 0, RaytracingMilliseconds = 0;
		size_t CulledSources = 0;
		bool MixerAvailable = false, MemoryAvailable = false, RaytracingAvailable = false;
		bool VoicesExceeded = false, CPUExceeded = false, MemoryExceeded = false, RaytracingExceeded = false;
	};
	// Main-thread SDK sampling. Cached bus groups are released before bank unload.
	class AudioPerformance
	{
	public:
		static bool Configure(const AudioPerformanceSettings& settings);
		static void Reset();
		static void Update();
		static void SubmitScene(double raytracingMs, size_t culled, bool available);
		static const AudioPerformanceSettings& GetSettings();
		static const AudioPerformanceStats& GetStats();
		static const std::vector<AudioBusMeter>& GetBuses();
	};
}
