#include "lpch.h"
#include "StatisticsPanel.h"

#include "Lux/Core/Application.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiFonts.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui.h>
#include <implot/implot.h>

#include <algorithm>

namespace Lux {

	namespace {

		constexpr size_t kMaxFrameSamples = 240;

		// Frame-time budgets that colour the timeline bars: at/under 60 FPS reads calm, 30-60 FPS
		// warns, below 30 FPS alarms.
		constexpr float kBudget60FPS = 1000.0f / 60.0f; // 16.67 ms
		constexpr float kBudget30FPS = 1000.0f / 30.0f; // 33.33 ms

		// Perf-budget bar colours are graph data-semantics (good / warning / over-budget), not theme
		// UI colours — kept literal here the way axis or chart-series colours are, rather than
		// borrowing an unrelated named theme constant.
		constexpr ImU32 kFrameGoodColor = IM_COL32(200, 255, 77, 255);  // on-brand lime, within budget
		constexpr ImU32 kFrameWarnColor = IM_COL32(230, 170, 60, 255);  // amber, 30-60 FPS
		constexpr ImU32 kFrameBadColor = IM_COL32(232, 84, 84, 255);    // red, below 30 FPS

		void MetricRow(const char* label, const char* value)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextDisabled("%s", label);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(value);
		}

	}

	void StatisticsPanel::OnImGuiRender(bool& isOpen)
	{
		if (!ImGui::Begin("Statistics", &isOpen))
		{
			ImGui::End();
			return;
		}

		const ImGuiIO& io = ImGui::GetIO();
		const float fps = io.Framerate;
		const float frameTimeMs = fps > 0.0f ? 1000.0f / fps : 0.0f;

		DrawFrameTimeline(frameTimeMs);

		// CPU time combines the two engine threads differently by policy: under MultiThreaded they
		// overlap so the frame is gated by the slower of the two; under SingleThreaded the render
		// work runs inline on the main thread, so the spans are sequential and add up. Mirrors the
		// viewport performance HUD.
		const auto& timers = Application::Get().GetPerformanceTimers();
		const bool renderThreadIsConcurrent =
			Application::Get().GetSpecification().CoreThreadingPolicy == ThreadingPolicy::MultiThreaded;
		const float cpuTime = renderThreadIsConcurrent
			? std::max(timers.MainThreadWorkTime, timers.RenderThreadWorkTime)
			: timers.MainThreadWorkTime + timers.RenderThreadWorkTime;

		char buffer[128];

		ImGui::SeparatorText("Timings");
		if (ImGui::BeginTable("##stats_timings", 2, ImGuiTableFlags_SizingStretchProp))
		{
			std::snprintf(buffer, sizeof(buffer), "%.0f", fps);
			MetricRow("FPS", buffer);
			std::snprintf(buffer, sizeof(buffer), "%.2f ms", frameTimeMs);
			MetricRow("Frame Time", buffer);
			std::snprintf(buffer, sizeof(buffer), "%.2f ms", cpuTime);
			MetricRow("CPU", buffer);

			if (m_Context)
			{
				std::snprintf(buffer, sizeof(buffer), "%.2f ms", m_Context->GetStatistics().TotalGPUTime);
				MetricRow("GPU (scene)", buffer);
			}
			ImGui::EndTable();
		}

		ImGui::SeparatorText("Threads");
		if (ImGui::BeginTable("##stats_threads", 2, ImGuiTableFlags_SizingStretchProp))
		{
			std::snprintf(buffer, sizeof(buffer), "%.2f ms", timers.MainThreadWorkTime);
			MetricRow("Main Thread", buffer);
			std::snprintf(buffer, sizeof(buffer), "%.2f ms", timers.RenderThreadWorkTime);
			MetricRow("Render Thread", buffer);
			std::snprintf(buffer, sizeof(buffer), "%.2f ms", timers.ScriptUpdate);
			MetricRow("Script Update", buffer);
			std::snprintf(buffer, sizeof(buffer), "%.2f ms", timers.PhysicsStepTime);
			MetricRow("Physics Step", buffer);
			ImGui::EndTable();
		}

		if (m_Context)
		{
			const auto& stats = m_Context->GetStatistics();
			const auto& memory = stats.MemoryStats;

			ImGui::SeparatorText("Scene");
			if (ImGui::BeginTable("##stats_scene", 2, ImGuiTableFlags_SizingStretchProp))
			{
				std::snprintf(buffer, sizeof(buffer), "%u", stats.DrawCalls);
				MetricRow("Draw Calls", buffer);
				std::snprintf(buffer, sizeof(buffer), "%u", stats.VisibleInstances);
				MetricRow("Visible Instances", buffer);
				std::snprintf(buffer, sizeof(buffer), "%u", stats.GPUVisibleInstances);
				MetricRow("GPU Visible", buffer);
				ImGui::EndTable();
			}

			ImGui::SeparatorText("Memory");
			if (ImGui::BeginTable("##stats_memory", 2, ImGuiTableFlags_SizingStretchProp))
			{
				if (memory.BudgetBytes > 0)
					std::snprintf(buffer, sizeof(buffer), "%s / %s",
						Utils::BytesToString(memory.UsedBytes).c_str(),
						Utils::BytesToString(memory.BudgetBytes).c_str());
				else
					std::snprintf(buffer, sizeof(buffer), "%s", Utils::BytesToString(memory.UsedBytes).c_str());
				MetricRow("VRAM", buffer);
				MetricRow("Textures", Utils::BytesToString(memory.TextureBytes).c_str());
				MetricRow("Buffers", Utils::BytesToString(memory.BufferBytes).c_str());
				MetricRow("Render Targets", Utils::BytesToString(memory.RenderTargetBytes).c_str());
				ImGui::EndTable();
			}
		}
		else
		{
			ImGui::TextDisabled("No scene renderer context.");
		}

		ImGui::End();
	}

	void StatisticsPanel::DrawFrameTimeline(float frameTimeMs)
	{
		m_FrameTimeHistory.push_back(frameTimeMs);
		if (m_FrameTimeHistory.size() > kMaxFrameSamples)
			m_FrameTimeHistory.erase(m_FrameTimeHistory.begin(), m_FrameTimeHistory.begin() + (m_FrameTimeHistory.size() - kMaxFrameSamples));

		const size_t sampleCount = m_FrameTimeHistory.size();

		float maxValue = kBudget60FPS;
		for (float value : m_FrameTimeHistory)
			maxValue = std::max(maxValue, value);

		// X positions run from -(N-1)..0 so the newest frame sits at x=0 (right edge), Tracy-style,
		// and the window scrolls left as frames accrue.
		m_FrameTimeAxis.resize(sampleCount);
		for (size_t i = 0; i < sampleCount; i++)
			m_FrameTimeAxis[i] = (float)i - (float)(sampleCount - 1);

		// The line colour reflects the current frame's budget bucket so a spike reads at a glance.
		ImU32 lineColor = kFrameGoodColor;
		if (frameTimeMs >= kBudget30FPS)
			lineColor = kFrameBadColor;
		else if (frameTimeMs >= kBudget60FPS)
			lineColor = kFrameWarnColor;

		ImPlot::PushStyleColor(ImPlotCol_FrameBg, Colors::Theme::backgroundDark);
		ImPlot::PushStyleColor(ImPlotCol_PlotBg, Colors::Theme::backgroundDark);
		ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0.0f, 0.0f));

		const ImPlotFlags plotFlags = ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus |
			ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText;
		ImGuiEx::Fonts::PushFont("Mono"); // mono axis/tick labels, Tracy-style
		if (ImPlot::BeginPlot("##frametime", ImVec2(-1.0f, 110.0f), plotFlags))
		{
			// X: the frame window, fixed span, no ticks/labels (it is a rolling timeline, not data
			// the user reads off). Y: milliseconds from 0 with headroom above the running peak.
			ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_Lock);
			ImPlot::SetupAxis(ImAxis_Y1, "ms", ImPlotAxisFlags_LockMin);
			ImPlot::SetupAxisLimits(ImAxis_X1, -(double)kMaxFrameSamples, 0.0, ImGuiCond_Always);
			ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, (double)maxValue * 1.15, ImGuiCond_Always);

			if (sampleCount > 0)
			{
				// One shaded line: fill runs from the line down to y=0 (ImPlotLineFlags_Shaded),
				// coloured by the current frame's budget bucket.
				const ImVec4 lineVec = ImGui::ColorConvertU32ToFloat4(lineColor);
				ImPlotSpec lineSpec;
				lineSpec.LineColor = lineVec;
				lineSpec.LineWeight = 1.5f;
				lineSpec.FillColor = lineVec;
				lineSpec.FillAlpha = 0.22f;
				lineSpec.Flags = ImPlotLineFlags_Shaded;
				ImPlot::PlotLine("Frame Time", m_FrameTimeAxis.data(), m_FrameTimeHistory.data(), (int)sampleCount, lineSpec);
			}

			// 16.67 ms (60 FPS) budget reference line.
			double budget = kBudget60FPS;
			ImPlotSpec budgetSpec;
			budgetSpec.LineColor = ImGui::ColorConvertU32ToFloat4(Colors::Theme::muted);
			budgetSpec.LineWeight = 1.0f;
			budgetSpec.Flags = ImPlotInfLinesFlags_Horizontal;
			ImPlot::PlotInfLines("##budget", &budget, 1, budgetSpec);

			ImPlot::EndPlot();
		}
		ImGuiEx::Fonts::PopFont();

		ImPlot::PopStyleVar();
		ImPlot::PopStyleColor(2);

		// Peak-over-window readout under the graph.
		char overlay[64];
		std::snprintf(overlay, sizeof(overlay), "peak %.2f ms  (window %zu frames)", maxValue, sampleCount);
		ImGui::TextDisabled("%s", overlay);
	}

}
