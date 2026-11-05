// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/ImGui/DiagnosticPlot.h"

namespace Lux {

	// Consolidated in-editor performance view. Pure visualization over data the engine already
	// collects: Application's per-frame CPU timers + named profiler zones, and SceneRenderer's
	// per-pass CPU/GPU profiles. No engine instrumentation lives here — see Application.h
	// (PerformanceTimers / GetProfilerPreviousFrameData) and SceneRenderer::Statistics.
	class ProfilerPanel : public EditorPanel
	{
	public:
		ProfilerPanel() = default;
		virtual ~ProfilerPanel() = default;

		void SetContext(const Ref<SceneRenderer>& context);
		const char* GetWindowName() const override;

		virtual void OnImGuiRender(bool& isOpen) override;
#ifndef LUX_DIST
		static bool RunDiagnosticSelfTests(std::vector<std::string>& failures);
#endif

	private:
		void UI_CPUBreakdown(float cpuFrameMs);
		void UI_GPUBreakdown();
		void UI_SceneAndMemory();

	private:
		Ref<SceneRenderer> m_Context;

		ImGuiEx::TimingPlotState m_TimingPlot;
	};

}
