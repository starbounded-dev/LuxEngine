#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/SceneRenderer.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Lux {

	class RendererDebuggerPanel : public EditorPanel
	{
	public:
		RendererDebuggerPanel() = default;
		virtual ~RendererDebuggerPanel() = default;

		void SetContext(const Ref<SceneRenderer>& context);
		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		struct PassHistory
		{
			std::vector<float> CPU;
			std::vector<float> GPU;
		};

		void UpdateProfilingHistory(const SceneRenderer::Statistics& stats);
		void DrawProfilingHistory(const SceneRenderer::Statistics& stats);
		void DrawOverview(const SceneRenderer::Statistics& stats);
		void DrawProfiling(const SceneRenderer::Statistics& stats);
		void DrawMemory(const SceneRenderer::Statistics& stats);
		void DrawWorkload(const SceneRenderer::Statistics& stats);
		void DrawShaders();
		void DrawRenderGraphInspector();

		Ref<SceneRenderer> m_Context;
		std::string m_ShaderSearch;
		std::string m_RenderGraphSearch;
		std::vector<float> m_FrameCPUHistory;
		std::vector<float> m_FrameGPUHistory;
		std::unordered_map<std::string, PassHistory> m_PassHistory;
		uint32_t m_LastReloadedShaderCount = 0;
		uint32_t m_LastWarmedPipelineCount = 0;
	};

}
