#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/SceneRenderer.h"

#include <string>
#include <vector>

namespace Lux {

	class RendererDebuggerPanel : public EditorPanel
	{
	public:
		RendererDebuggerPanel() = default;
		virtual ~RendererDebuggerPanel() = default;

		void SetContext(const Ref<SceneRenderer>& context);
		void SetDebugViewsRuntimeSuspended(bool suspended) { m_DebugViewsRuntimeSuspended = suspended; }
		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		void UpdateProfilingHistory(const SceneRenderer::Statistics& stats);
		void DrawFrameHistoryPlot();
		void DrawPassGPUChart(const SceneRenderer::Statistics& stats);
		void DrawOverview(const SceneRenderer::Statistics& stats);
		void DrawProfiling(const SceneRenderer::Statistics& stats);
		void DrawMemory(const SceneRenderer::Statistics& stats);
		void DrawWorkload(const SceneRenderer::Statistics& stats);
		void DrawGPUScene();
		void DrawShaders();
		void DrawRenderPassIsolation();
		void DrawRenderGraphInspector();

		Ref<SceneRenderer> m_Context;
		std::string m_ShaderSearch;
		std::string m_RenderGraphSearch;
		uint32_t m_RenderGraphSelectedPass = UINT32_MAX;
		uint32_t m_RenderGraphSelectedResource = UINT32_MAX;
		bool m_RenderGraphErrorsOnly = false;
		bool m_RenderGraphShowWarnings = true;
		bool m_RenderGraphShowCulled = true;
		bool m_RenderGraphShowAliased = false;
		bool m_RenderGraphShowTransient = false;
		bool m_RenderGraphShowExternal = false;
		bool m_RenderGraphShowGraphics = false;
		bool m_RenderGraphShowCompute = false;
		bool m_RenderGraphSelfTestRan = false;
		bool m_RenderGraphSelfTestPassed = false;
		std::vector<std::string> m_RenderGraphSelfTestFailures;
		bool m_SerializerSelfTestRan = false;
		bool m_SerializerSelfTestPassed = false;
		std::vector<std::string> m_SerializerSelfTestFailures;
		std::vector<float> m_FrameCPUHistory;
		std::vector<float> m_FrameGPUHistory;
		// X positions (-(N-1)..0) for the frame-history plot, rebuilt to match the ring size.
		std::vector<float> m_FrameHistoryAxis;
		// Reused per-frame scratch for the per-pass GPU bar chart (values + tick positions/labels),
		// so the chart doesn't reallocate every frame.
		std::vector<float> m_PassChartValues;
		std::vector<double> m_PassChartTicks;
		std::vector<const char*> m_PassChartLabels;
		uint32_t m_LastReloadedShaderCount = 0;
		uint32_t m_LastWarmedPipelineCount = 0;
		bool m_DebugViewsRuntimeSuspended = false;
	};

}
