#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/SceneRenderer.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Lux {

	class SceneRendererPanel : public EditorPanel
	{
	public:
		SceneRendererPanel() = default;
		virtual ~SceneRendererPanel() = default;

		void SetContext(const Ref<SceneRenderer>& context);
		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		void ApplyProjectSettingsToContext();
		void SyncProjectSettingsFromContext();
		bool SaveProjectRendererSettings();
		void UpdateProfilingHistory(const SceneRenderer::Statistics& stats);
		void DrawProfilingHistory(const SceneRenderer::Statistics& stats);

		struct PassHistory
		{
			std::vector<float> CPU;
			std::vector<float> GPU;
		};

		Ref<SceneRenderer> m_Context;
		std::string m_ShaderSearch;
		std::vector<float> m_FrameCPUHistory;
		std::vector<float> m_FrameGPUHistory;
		std::unordered_map<std::string, PassHistory> m_PassHistory;
		uint32_t m_LastReloadedShaderCount = 0;
		uint32_t m_LastWarmedPipelineCount = 0;
		bool m_ProjectRendererSettingsDirty = false;
	};

}
