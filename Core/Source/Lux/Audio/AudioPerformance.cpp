// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioPerformance.h"
#include "AudioEngine.h"
#include <fmod.hpp>
#include <fmod_studio.hpp>
#include <fmod_errors.h>
#include <chrono>
#include <unordered_set>
#include <cmath>
namespace Lux
{
	namespace
	{
		struct Bus
		{
			FMOD::Studio::Bus* Studio = nullptr;
			FMOD::ChannelGroup* Group = nullptr;
			FMOD::DSP* Meter = nullptr;
			bool Warned = false;
		};
		AudioPerformanceSettings s_Settings;
		AudioPerformanceStats s_Stats;
		std::vector<Bus> s_Buses;
		std::vector<AudioBusMeter> s_Meters;
		std::unordered_set<std::string> s_Errors;
		bool s_Warned[4]{};
		bool s_Configured = false;
		uint64_t s_Revision = UINT64_MAX;
		std::chrono::steady_clock::time_point s_NextSample;
		constexpr auto kSampleInterval = std::chrono::milliseconds(250);
		bool Check(FMOD_RESULT result, const char* operation)
		{
			if (result == FMOD_OK)
				return true;
			if (s_Errors.insert(operation).second)
				LUX_CORE_ERROR_TAG("Audio", "Performance monitor {}: {}", operation, FMOD_ErrorString(result));
			return false;
		}
		void Warn(bool exceeded, size_t index, const char* name, double measured, double limit)
		{
			if (exceeded && !s_Warned[index])
			{
				s_Warned[index] = true;
				LUX_CORE_WARN_TAG("Audio", "{} budget exceeded: {} (budget {})", name, measured, limit);
			}
		}
		bool Count(FMOD::ChannelGroup* group, int& total, int& real, int depth = 0)
		{
			if (depth > 128)
			{
				Check(FMOD_ERR_INTERNAL, "bus hierarchy exceeds 128 levels");
				return false;
			}
			int channels = 0, groups = 0;
			if (!Check(group->getNumChannels(&channels), "count bus channels") || !Check(group->getNumGroups(&groups), "count bus groups"))
				return false;
			for (int i = 0; i < channels; ++i)
			{
				FMOD::Channel* channel = nullptr;
				bool playing = false, virtualized = false;
				if (!Check(group->getChannel(i, &channel), "get bus channel") || !channel)
					return false;
				// Mixer voices can finish between enumeration and inspection.
				auto result = channel->isPlaying(&playing);
				if (result == FMOD_ERR_INVALID_HANDLE || result == FMOD_ERR_CHANNEL_STOLEN)
					continue;
				if (!Check(result, "query bus voice") || !playing)
					continue;
				result = channel->isVirtual(&virtualized);
				if (result == FMOD_ERR_INVALID_HANDLE || result == FMOD_ERR_CHANNEL_STOLEN)
					continue;
				if (!Check(result, "query virtual voice"))
					return false;
				++total;
				real += !virtualized;
			}
			for (int i = 0; i < groups; ++i)
			{
				FMOD::ChannelGroup* child = nullptr;
				if (!Check(group->getGroup(i, &child), "get child bus group") || !child || !Count(child, total, real, depth + 1))
					return false;
			}
			return true;
		}
	}
	void AudioPerformance::Reset()
	{
		for (auto& bus : s_Buses)
		{
			if (bus.Meter)
			{
				if (bus.Group)
					Check(bus.Group->removeDSP(bus.Meter), "detach bus input meter");
				Check(bus.Meter->release(), "release bus input meter");
			}
			if (bus.Studio)
				Check(AudioEngine::UnlockBusChannelGroup(bus.Studio), "unlock metered bus");
		}
		s_Buses.clear();
		s_Meters.clear();
		s_Stats = {};
		s_Errors.clear();
		std::fill(std::begin(s_Warned), std::end(s_Warned), false);
		s_Revision = UINT64_MAX;
		s_NextSample = {};
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
		s_Configured = true;
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
		LUX_PROFILE_FUNCTION_AUTO;
		auto* studio = AudioEngine::GetStudioSystem();
		auto* core = AudioEngine::GetEngine();
		if (!s_Configured || !studio || !core)
			return;
		const auto now = std::chrono::steady_clock::now();
		if (now < s_NextSample)
			return;
		s_NextSample = now + kSampleInterval;
		if (s_Revision != AudioEngine::GetBankRevision())
		{
			Reset();
			s_Revision = AudioEngine::GetBankRevision();
			s_NextSample = now + kSampleInterval;
			for (const auto& [path, limit] : s_Settings.BusVoices)
			{
				s_Meters.push_back({ path, limit });
				auto& bus = s_Buses.emplace_back();
				FMOD::Studio::Bus* candidate = nullptr;
				if (Check(studio->getBus(path.c_str(), &candidate), "resolve budget bus") && candidate && Check(AudioEngine::LockBusChannelGroup(candidate), "lock budget bus"))
					bus.Studio = candidate;
			}
			Check(studio->flushCommands(), "prepare budget bus groups");
			for (auto& bus : s_Buses)
			{
				if (!bus.Studio || !Check(bus.Studio->getChannelGroup(&bus.Group), "get budget bus group") || !bus.Group)
					continue;
				// Meter through a pass-through fader this monitor owns, never a DSP Studio created: with
				// Live Update on, changing metering on Studio's bus DSPs makes every later
				// Studio::System::update fail with FMOD_ERR_BADCOMMAND. Index 1 sits directly behind the
				// head DSP, so the meter's input is the same signal the head DSP receives.
				if (!Check(core->createDSPByType(FMOD_DSP_TYPE_FADER, &bus.Meter), "create bus input meter") || !bus.Meter)
				{
					bus.Meter = nullptr;
					continue;
				}
				if (!Check(bus.Group->addDSP(1, bus.Meter), "attach bus input meter"))
				{
					Check(bus.Meter->release(), "release bus input meter");
					bus.Meter = nullptr;
					continue;
				}
				Check(bus.Meter->setMeteringEnabled(true, false), "enable bus input metering");
			}
		}
		int total = 0;
		FMOD_CPU_USAGE cpu{};
		FMOD_STUDIO_CPU_USAGE studioCPU{};
		s_Stats.MixerAvailable = Check(core->getChannelsPlaying(&total, &s_Stats.RealVoices), "get voice counts") &&
			Check(studio->getCPUUsage(&studioCPU, &cpu), "get audio CPU");
		s_Stats.VirtualVoices = std::max(0, total - s_Stats.RealVoices);
		s_Stats.CPUPercent = cpu.dsp + cpu.stream + cpu.update + studioCPU.update;
		// Studio getMemoryUsage silently returns zeros with FMOD's release libraries.
		// Allocator statistics work in both SDK builds; avoid a DSP flush while sampling.
		int allocated = 0;
		s_Stats.MemoryAvailable = Check(FMOD::Memory_GetStats(&allocated, nullptr, false), "get FMOD allocated memory");
		s_Stats.MemoryMiB = static_cast<double>(allocated) / (1024.0 * 1024.0);
		s_Stats.VoicesExceeded = s_Stats.MixerAvailable && total > static_cast<int>(s_Settings.RealVoices);
		s_Stats.CPUExceeded = s_Stats.MixerAvailable && s_Stats.CPUPercent > s_Settings.CPUPercent;
		s_Stats.MemoryExceeded = s_Stats.MemoryAvailable && s_Stats.MemoryMiB > s_Settings.BankMemoryMiB;
		s_Stats.RaytracingExceeded = s_Stats.RaytracingAvailable && s_Stats.RaytracingMilliseconds > s_Settings.RaytracingMilliseconds;
		Warn(s_Stats.VoicesExceeded, 0, "Voice demand", total, s_Settings.RealVoices);
		Warn(s_Stats.CPUExceeded, 1, "Audio CPU (%)", s_Stats.CPUPercent, s_Settings.CPUPercent);
		Warn(s_Stats.MemoryExceeded, 2, "FMOD allocated memory (MiB)", s_Stats.MemoryMiB, s_Settings.BankMemoryMiB);
		Warn(s_Stats.RaytracingExceeded, 3, "VA raytracing (ms)", s_Stats.RaytracingMilliseconds, s_Settings.RaytracingMilliseconds);
		for (size_t i = 0; i < s_Buses.size(); ++i)
		{
			auto& bus = s_Buses[i];
			auto& meter = s_Meters[i];
			meter.Voices = meter.RealVoices = 0;
			meter.Peak = meter.RMS = 0;
			meter.Exceeded = false;
			meter.Available = bus.Group && bus.Meter && Count(bus.Group, meter.Voices, meter.RealVoices);
			if (!meter.Available)
				continue;
			FMOD_DSP_METERING_INFO info{};
			meter.Available = Check(bus.Meter->getMeteringInfo(&info, nullptr), "read bus meter");
			for (int channel = 0; meter.Available && channel < std::min<int>(info.numchannels, 32); ++channel)
			{
				meter.Peak = std::max(meter.Peak, info.peaklevel[channel]);
				meter.RMS = std::max(meter.RMS, info.rmslevel[channel]);
			}
			meter.Exceeded = meter.Available && meter.Voices > static_cast<int>(meter.Limit);
			if (meter.Exceeded && !bus.Warned)
			{
				bus.Warned = true;
				LUX_CORE_WARN_TAG("Audio", "Bus '{}' voice budget exceeded: {} (budget {})", meter.Path, meter.Voices, meter.Limit);
			}
		}
	}
	const AudioPerformanceSettings& AudioPerformance::GetSettings() { return s_Settings; }
	const AudioPerformanceStats& AudioPerformance::GetStats() { return s_Stats; }
	const std::vector<AudioBusMeter>& AudioPerformance::GetBuses() { return s_Meters; }
}
