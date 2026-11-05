// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ProfilerPanel.h"

#ifndef LUX_DIST
#include "RendererDebuggerPanel.h"
#include "Lux/ImGui/Colors.h"
#include <imgui/imgui_internal.h>
#include <implot/implot.h>
#include <implot/implot_internal.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace Lux
{
	namespace
	{
		void Require(bool condition, const char* message)
		{
			if (!condition)
				throw std::runtime_error(message);
		}

		ImPlotItem* LegendItem(ImPlotPlot* plot, const char* name)
		{
			for (int i = 0; i < plot->Items.GetLegendCount(); ++i)
				if (std::strcmp(plot->Items.GetLegendLabel(i), name) == 0)
					return plot->Items.GetLegendItem(i);
			return nullptr;
		}
	} // namespace

	bool ProfilerPanel::RunDiagnosticSelfTests(std::vector<std::string>& failures)
	{
#ifdef _MSC_VER
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImPlotContext* previousPlot = ImPlot::GetCurrentContext();
		ImGuiContext* context = ImGui::CreateContext();
		ImGui::SetCurrentContext(context);
		ImPlotContext* plots = ImPlot::CreateContext();
		ImPlot::SetCurrentContext(plots);
		try
		{
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigDebugHighlightIdConflicts = true;
			io.DisplaySize = {1920.0f, 1080.0f};
			io.Fonts->AddFontDefault();
			unsigned char* pixels = nullptr;
			int width = 0, height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
			ImGuiEx::TimingPlotState state;
			ImPlotPlot* drawn = nullptr;
			auto frame = [&](float panelWidth = 900.0f)
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowPos({0.0f, 0.0f});
				ImGui::SetNextWindowSize({panelWidth, 800.0f});
				ImGui::Begin("Timing plot test");
				ImGuiEx::TimingPlotControls("controls", state);
				ImGuiEx::DrawTimingPlot("history", state, "known CPU fixture");
				ImGui::PushID("history");
				drawn = ImPlot::GetPlot("##history");
				ImGui::PopID();
				ImGui::End();
				ImGui::Render();
			};
			frame();
			Require(state.History.Size() == 0 && drawn, "Empty history did not render a plot");
			state.Record(12.5f, 0.0f);
			Require(std::isnan(state.History.At(0).GPU) && state.History.At(0).CPU == 12.5f,
					"Missing GPU timing became a zero measurement or the CPU value changed");
			frame();
			Require(LegendItem(drawn, "CPU") && LegendItem(drawn, "GPU") && LegendItem(drawn, "Budget"),
					"Single-sample plot lost a series or its budget legend");
			Require(LegendItem(drawn, "CPU")->Color == Colors::Theme::accent &&
						LegendItem(drawn, "GPU")->Color == Colors::Theme::titlebarOrange,
					"CPU/GPU series colors differ from the shared convention");
			const auto& plotStyle = ImPlot::GetStyle();
			const float textHeight = ImGui::GetTextLineHeight();
			const ImVec2 legend(
				drawn->Items.Legend.Rect.Min.x + plotStyle.LegendInnerPadding.x + textHeight +
					plotStyle.LegendSpacing.x + ImGui::CalcTextSize("CPU").x + textHeight * 0.5f,
				drawn->Items.Legend.Rect.Min.y + plotStyle.LegendInnerPadding.y + textHeight * 0.5f);
			io.AddMousePosEvent(legend.x, legend.y);
			frame();
			io.AddMouseButtonEvent(0, true);
			frame();
			io.AddMouseButtonEvent(0, false);
			frame();
			Require(!LegendItem(drawn, "GPU")->Show, "Clicking a legend did not hide its series");
			io.AddMousePosEvent(-100.0f, -100.0f);
			for (size_t i = 1; i <= ImGuiEx::TimingHistory::Capacity + 20; ++i)
				state.Record(static_cast<float>(i), i % 2 ? 4.25f : 0.0f);
			Require(state.History.Size() == ImGuiEx::TimingHistory::Capacity &&
						state.History.At(0).Sequence == 21 && state.History.At(239).CPU == 260.0f,
					"Bounded history lost chronological ordering after wrap");
			state.Record(1000.0f, std::numeric_limits<float>::infinity());
			Require(state.History.Peak(1.0f) == 1000.0f && std::isnan(state.History.At(239).GPU),
					"Outlier or nonfinite measurement handling is incorrect");
			frame();
			Require(!LegendItem(drawn, "GPU")->Show && drawn->Axes[ImAxis_Y1].Range.Max >= 1000.0,
					"A new sample reset the legend or clipped an outlier");
			const ImVec2 center = drawn->PlotRect.GetCenter();
			io.AddMousePosEvent(center.x, center.y);
			frame();
			const double beforeZoom = drawn->Axes[ImAxis_X1].Range.Size();
			io.AddMouseWheelEvent(0.0f, 1.0f);
			frame();
			Require(!state.Follow && drawn->Axes[ImAxis_X1].Range.Size() < beforeZoom,
					"The first wheel gesture did not leave live follow and zoom the plot");
			const ImPlotRange inspected = drawn->Axes[ImAxis_X1].Range;
			state.Record(20.0f, 3.0f);
			frame();
			Require(drawn->Axes[ImAxis_X1].Range.Min == inspected.Min &&
						drawn->Axes[ImAxis_X1].Range.Max == inspected.Max,
					"Incoming measurements reset the inspected range");
			const uint64_t lastSample = state.History.At(239).Sequence;
			state.SetPaused(true);
			state.Record(9999.0f, 9999.0f);
			frame();
			Require(state.History.At(239).Sequence == lastSample && !state.Follow,
					"Pausing history continued recording or enabled follow");
			state.Fit();
			frame();
			Require(drawn->Axes[ImAxis_X1].Range.Min <= state.History.At(0).Sequence &&
						drawn->Axes[ImAxis_X1].Range.Max >= lastSample,
					"Fit did not restore the complete retained history");
			state.SetPaused(false);
			state.Record(7.0f, 2.0f);
			frame();
			Require(state.Follow && state.History.At(239).Sequence == lastSample + 1,
					"Resume did not restore recording and live follow");
			for (float scale : {1.0f, 1.5f})
			{
				ImGui::GetStyle() = ImGuiStyle{};
				ImGui::GetStyle().ScaleAllSizes(scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (float panelWidth : {900.0f, 260.0f, 900.0f})
				{
					frame(panelWidth * scale);
					Require(drawn->PlotRect.GetWidth() > 0.0f, "Narrow or scaled plot lost its canvas");
				}
			}
			state.Clear();
			frame();
			Require(state.History.Size() == 0, "Context reset retained stale timing samples");

			RendererDebuggerPanel debugger;
			SceneRenderer::Statistics stats;
			SceneRenderer::PassProfile pass{};
			pass.Name = "Known pass";
			pass.GPUTime = 4.25f;
			stats.PassProfiles.push_back(pass);
			auto passFrame = [&](float panelWidth)
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowSize({panelWidth, 800.0f});
				ImGui::Begin("GPU pass test");
				debugger.DrawPassGPUChart(stats);
				ImGui::End();
				ImGui::Render();
			};
			passFrame(900.0f);
			Require(debugger.m_PassChartValues.size() == 1 && debugger.m_PassChartValues[0] == 4.25f,
					"GPU pass chart changed the published value");
			debugger.m_PassChartFrozen = true;
			stats.PassProfiles[0].GPUTime = 30.0f;
			passFrame(260.0f);
			Require(debugger.m_PassChartValues[0] == 4.25f && debugger.m_PassChartNames[0] == "Known pass",
					"Frozen pass measurements or labels followed live data");
			debugger.m_PassChartFrozen = false;
			stats.PassProfiles[0].GPUTime = 0.0f;
			passFrame(900.0f);
			Require(debugger.m_PassChartValues.empty(), "An unavailable pass became a zero-cost bar");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		ImPlot::DestroyContext(plots);
		ImPlot::SetCurrentContext(previousPlot);
		ImGui::DestroyContext(context);
		ImGui::SetCurrentContext(previous);
		return failures.empty();
	}
} // namespace Lux
#endif
