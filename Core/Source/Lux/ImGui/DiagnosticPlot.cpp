// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "DiagnosticPlot.h"

#include "Colors.h"
#include "ImGuiUtilities.h"
#include "PanelControls.h"
#include "Lux/Editor/FontAwesome.h"

#include <imgui/imgui.h>
#include <implot/implot.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Lux::ImGuiEx
{
	namespace
	{
		enum class PlotAction
		{
			Pause,
			Follow,
			Fit,
			Budget30,
			Budget60,
			Budget120,
			Budget144
		};
		constexpr float k_Missing = std::numeric_limits<float>::quiet_NaN();
		constexpr float k_Budgets[] = {1000.0f / 30.0f, 1000.0f / 60.0f, 1000.0f / 120.0f, 1000.0f / 144.0f};

		struct Series
		{
			const TimingHistory& History;
			bool GPU = false;
		};

		ImPlotPoint SamplePoint(int index, void* data)
		{
			const auto& series = *static_cast<Series*>(data);
			const TimingSample& point = series.History.At(static_cast<size_t>(index));
			return {static_cast<double>(point.Sequence), series.GPU ? point.GPU : point.CPU};
		}

		struct PlotStyle
		{
			PlotStyle()
			{
				ImPlot::PushStyleColor(ImPlotCol_FrameBg, Colors::Theme::backgroundDark);
				ImPlot::PushStyleColor(ImPlotCol_PlotBg, Colors::Theme::backgroundDark);
			}
			~PlotStyle()
			{
				ImPlot::PopStyleColor(2);
			}
		};
	} // namespace

	void TimingHistory::Push(float cpuMs, float gpuMs)
	{
		m_Samples[m_Head] = {m_NextSequence++, std::isfinite(cpuMs) && cpuMs >= 0.0f ? cpuMs : k_Missing,
							 std::isfinite(gpuMs) && gpuMs > 0.0f ? gpuMs : k_Missing};
		m_Head = (m_Head + 1) % Capacity;
		m_Count = std::min(m_Count + 1, Capacity);
	}

	void TimingHistory::Clear()
	{
		m_Head = m_Count = 0;
		m_NextSequence = 0;
	}

	const TimingSample& TimingHistory::At(size_t oldestFirstIndex) const
	{
		LUX_CORE_ASSERT(oldestFirstIndex < m_Count);
		return m_Samples[(m_Head + Capacity - m_Count + oldestFirstIndex) % Capacity];
	}

	float TimingHistory::Peak(float minimum) const
	{
		for (size_t i = 0; i < m_Count; ++i)
		{
			const TimingSample& point = At(i);
			if (std::isfinite(point.CPU))
				minimum = std::max(minimum, point.CPU);
			if (std::isfinite(point.GPU))
				minimum = std::max(minimum, point.GPU);
		}
		return minimum;
	}

	void TimingPlotState::Record(float cpuMs, float gpuMs)
	{
		if (!Paused)
			History.Push(cpuMs, gpuMs);
	}

	void TimingPlotState::SetPaused(bool paused)
	{
		Paused = paused;
		Follow = !paused;
	}

	void TimingPlotState::Fit()
	{
		Follow = false;
		FitRequested = true;
	}

	void TimingPlotState::Clear()
	{
		History.Clear();
		FitRequested = true;
	}

	void TimingPlotControls(const char* id, TimingPlotState& state)
	{
		const PanelAction actions[] = {
			{static_cast<int>(PlotAction::Pause), state.Paused ? "Resume history" : "Pause history",
			 state.Paused ? LUX_ICON_PLAY : LUX_ICON_PAUSE, nullptr, true, state.Paused, true},
			{static_cast<int>(PlotAction::Follow), "Follow live", LUX_ICON_ARROW_RIGHT, nullptr,
			 !state.Paused, state.Follow},
			{static_cast<int>(PlotAction::Fit), "Fit", LUX_ICON_EXPAND},
			{static_cast<int>(PlotAction::Budget30), "Budget: 30 fps", nullptr, nullptr, true,
			 state.BudgetMs == k_Budgets[0], false, true},
			{static_cast<int>(PlotAction::Budget60), "Budget: 60 fps", nullptr, nullptr, true,
			 state.BudgetMs == k_Budgets[1], false, true},
			{static_cast<int>(PlotAction::Budget120), "Budget: 120 fps", nullptr, nullptr, true,
			 state.BudgetMs == k_Budgets[2], false, true},
			{static_cast<int>(PlotAction::Budget144), "Budget: 144 fps", nullptr, nullptr, true,
			 state.BudgetMs == k_Budgets[3], false, true}};
		const int action = PanelActionBar(id, actions);
		if (action == static_cast<int>(PlotAction::Pause))
			state.SetPaused(!state.Paused);
		else if (action == static_cast<int>(PlotAction::Follow))
			state.Follow = !state.Follow;
		else if (action == static_cast<int>(PlotAction::Fit))
			state.Fit();
		else if (action >= static_cast<int>(PlotAction::Budget30) &&
				 action <= static_cast<int>(PlotAction::Budget144))
			state.BudgetMs = k_Budgets[action - static_cast<int>(PlotAction::Budget30)];
	}

	void DrawTimingPlot(const char* id, TimingPlotState& state, const char* cpuSource)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		ScopedID scope(id);
		const auto& history = state.History;
		const size_t count = history.Size();
		ImGui::TextWrapped("%s | %zu samples | Budget %.2f ms (%.0f fps)",
						   state.Paused	  ? "Paused history"
						   : state.Follow ? "Following live"
										  : "Inspecting; history still records",
						   count, state.BudgetMs, 1000.0f / state.BudgetMs);
		ImGui::TextWrapped("CPU: %s. GPU: scene command buffer. Other panel metrics remain live.", cpuSource);
		const float width = ImGui::GetContentRegionAvail().x;
		const float height = ImGui::GetFontSize() * 12.0f;
		if (width <= 1.0f)
			return;

		// Detect the first inspection gesture before Always limits lock ImPlot's input for this frame.
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		if (ImGui::IsWindowHovered() &&
			ImGui::IsMouseHoveringRect(origin, {origin.x + width, origin.y + height}) &&
			(ImGui::GetIO().MouseWheel != 0.0f || ImGui::IsMouseDragging(0) || ImGui::IsMouseDragging(1) ||
			 ImGui::IsMouseDragging(2) || ImGui::IsMouseDoubleClicked(0)))
			state.Follow = false;

		PlotStyle style;
		if (ImPlot::BeginPlot("##history", {width, height}, ImPlotFlags_NoTitle | ImPlotFlags_NoMouseText))
		{
			ImPlot::SetupAxis(ImAxis_X1, "Sample", ImPlotAxisFlags_NoSideSwitch);
			ImPlot::SetupAxis(ImAxis_Y1, "ms", ImPlotAxisFlags_NoSideSwitch);
			ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);
			if (state.Follow || state.FitRequested)
			{
				const double first = count ? static_cast<double>(history.At(0).Sequence) : 0.0;
				const double last = count ? static_cast<double>(history.At(count - 1).Sequence) : 1.0;
				ImPlot::SetupAxisLimits(ImAxis_X1, first - 0.5, last + 0.5, ImPlotCond_Always);
				ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, history.Peak(state.BudgetMs) * 1.15,
										ImPlotCond_Always);
				state.FitRequested = false;
			}
			Series cpu{history}, gpu{history, true};
			ImPlotSpec spec;
			spec.LineWeight = 1.5f;
			if (count == 1)
			{
				spec.Marker = ImPlotMarker_Circle;
				spec.MarkerSize = ImGui::GetFontSize() * 0.2f;
			}
			spec.LineColor = ImGui::ColorConvertU32ToFloat4(Colors::Theme::accent);
			ImPlot::PlotLineG("CPU", SamplePoint, &cpu, static_cast<int>(count), spec);
			spec.LineColor = ImGui::ColorConvertU32ToFloat4(Colors::Theme::titlebarOrange);
			ImPlot::PlotLineG("GPU", SamplePoint, &gpu, static_cast<int>(count), spec);
			const double budget = state.BudgetMs;
			spec.LineColor = ImGui::ColorConvertU32ToFloat4(Colors::Theme::textCaption);
			spec.Flags = ImPlotInfLinesFlags_Horizontal;
			ImPlot::PlotInfLines("Budget", &budget, 1, spec);
			if (count && ImPlot::IsPlotHovered() && ImGui::BeginTooltip())
			{
				const double offset =
					ImPlot::GetPlotMousePos().x - static_cast<double>(history.At(0).Sequence);
				const size_t index =
					static_cast<size_t>(std::clamp(std::round(offset), 0.0, static_cast<double>(count - 1)));
				const TimingSample& point = history.At(index);
				ImGui::Text("Sample %llu", static_cast<unsigned long long>(point.Sequence));
				if (std::isfinite(point.CPU))
					ImGui::Text("CPU: %.3f ms", point.CPU);
				else
					ImGui::TextUnformatted("CPU unavailable");
				if (std::isfinite(point.GPU))
					ImGui::Text("GPU: %.3f ms", point.GPU);
				else
					ImGui::TextUnformatted("GPU unavailable / pending");
				ImGui::EndTooltip();
			}
			ImPlot::EndPlot();
		}
		if (!count)
			ImGui::TextWrapped("Waiting for timing samples. Resume history if paused.");
		else if (!std::isfinite(history.At(count - 1).GPU))
			ImGui::TextWrapped("GPU timing unavailable / pending. Gaps are missing measurements.");
	}
} // namespace Lux::ImGuiEx
