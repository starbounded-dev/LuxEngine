#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <mutex>

#include "Log.h"

namespace StarEngine {

	class Timer {
	public:
		Timer() { Reset(); }

		SE_FORCE_INLINE void Reset() {
			m_Start = std::chrono::high_resolution_clock::now();
		}

		SE_FORCE_INLINE float Elapsed() const {
			return std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - m_Start).count();
		}

		SE_FORCE_INLINE float ElapsedMillis() const {
			return std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - m_Start).count();
		}

	private:
		std::chrono::time_point<std::chrono::high_resolution_clock> m_Start;
	};

	class ScopedTimer {
	public:
		explicit ScopedTimer(std::string name)
			: m_Name(std::move(name)) {
		}

		~ScopedTimer() {
			float ms = m_Timer.ElapsedMillis();
			SE_CORE_TRACE_TAG("Timer", "{} - {:.3f}ms", m_Name, ms);
		}

	private:
		std::string m_Name;
		Timer m_Timer;
	};

	class PerformanceProfiler {
	public:
		struct PerFrameData {
			float Time = 0.0f;
			uint32_t Samples = 0;

			PerFrameData() = default;
			PerFrameData(float time) : Time(time), Samples(1) {}

			operator float() const { return Time; }

			PerFrameData& operator+=(float time) {
				Time += time;
				++Samples;
				return *this;
			}
		};

		void SetPerFrameTiming(const char* name, float time)
		{
			std::scoped_lock<std::mutex> lock(m_PerFrameDataMutex);

			if (m_PerFrameData.find(name) == m_PerFrameData.end())
				m_PerFrameData[name] = 0.0f;

			PerFrameData& data = m_PerFrameData[name];
			data.Time += time;
			data.Samples++;
		}

		void Clear() {
			std::scoped_lock lock(m_PerFrameDataMutex);
			m_PerFrameData.clear();
		}

		const std::unordered_map<const char*, PerFrameData>& GetPerFrameData() const { return m_PerFrameData; }

	private:
		std::unordered_map<const char*, PerFrameData> m_PerFrameData;
		inline static std::mutex m_PerFrameDataMutex;
	};

	class ScopePerfTimer {
	public:
		ScopePerfTimer(const char* name, PerformanceProfiler* profiler)
			: m_Name(name), m_Profiler(profiler) {
		}

		~ScopePerfTimer()
		{
			float time = m_Timer.ElapsedMillis();
			m_Profiler->SetPerFrameTiming(m_Name, time);
		}

	private:
		const char* m_Name;
		PerformanceProfiler* m_Profiler;
		Timer m_Timer;
	};

	// TODO: Replace with proper context-aware profiler system
	inline PerformanceProfiler& GetGlobalProfiler() {
		static PerformanceProfiler s_GlobalProfiler;
		return s_GlobalProfiler;
	}

#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)

#if 1
	// TEMPORARY FIX: Use global profiler instead of Application singleton
#define SE_SCOPE_PERF(name) \
		::StarEngine::ScopePerfTimer CONCAT(scope_perf_, __LINE__)(name, &::StarEngine::GetGlobalProfiler())

#define SE_SCOPE_TIMER(name) \
		::StarEngine::ScopedTimer CONCAT(scoped_timer_, __LINE__)(name)
#else
#define SE_SCOPE_PERF(name)
#define SE_SCOPE_TIMER(name)
#endif

}
