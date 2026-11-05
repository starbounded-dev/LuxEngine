// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Lux::ImGuiEx
{
	struct TimingSample
	{
		uint64_t Sequence = 0;
		float CPU = 0.0f;
		float GPU = 0.0f;
	};

	// Main-thread presentation data, sampled only while the owning panel is visible.
	// A GPU value <= 0 means this renderer has no published measurement. Missing values are NaN,
	// leaving a gap in the plot rather than inventing a zero-cost GPU frame.
	class TimingHistory
	{
	public:
		static constexpr size_t Capacity = 240;
		void Push(float cpuMs, float gpuMs);
		void Clear();
		size_t Size() const
		{
			return m_Count;
		}
		const TimingSample& At(size_t oldestFirstIndex) const;
		float Peak(float minimum) const;

	private:
		std::array<TimingSample, Capacity> m_Samples{};
		size_t m_Head = 0, m_Count = 0;
		uint64_t m_NextSequence = 0;
	};

	struct TimingPlotState
	{
		TimingHistory History;
		bool Paused = false;
		bool Follow = true;
		bool FitRequested = true;
		float BudgetMs = 1000.0f / 60.0f;

		void Record(float cpuMs, float gpuMs);
		void SetPaused(bool paused);
		void Fit();
		void Clear();
	};

	// Inside a main-thread ImGui window; caller supplies a stable ID and the actual CPU source.
	// The chart owns no renderer, global telemetry or persistent project data.
	void TimingPlotControls(const char* id, TimingPlotState& state);
	void DrawTimingPlot(const char* id, TimingPlotState& state, const char* cpuSource);
} // namespace Lux::ImGuiEx
