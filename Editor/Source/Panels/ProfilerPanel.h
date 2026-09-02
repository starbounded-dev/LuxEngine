#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/SceneRenderer.h"

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

		void SetContext(const Ref<SceneRenderer>& context) { m_Context = context; }

		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		void PushFrameSample(float cpuMs, float gpuMs);
		void UI_Graph(float cpuMs, float gpuMs);
		void UI_CPUBreakdown(float cpuFrameMs);
		void UI_GPUBreakdown();
		void UI_SceneAndMemory();

	private:
		Ref<SceneRenderer> m_Context;

		static constexpr int kHistory = 180;   // ~3s at 60fps
		float m_CPUHistory[kHistory] = {};
		float m_GPUHistory[kHistory] = {};
		int m_HistoryHead = 0;                  // next slot to write
		int m_HistoryCount = 0;                 // valid samples, saturating at kHistory

		bool m_Paused = false;
		float m_TargetMs = 1000.0f / 60.0f;     // frame-budget reference line
	};

}
