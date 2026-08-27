#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/SceneRenderer.h"

#include <vector>

namespace Lux {

	// Lightweight, always-available performance overview: a Tracy-style frame-time timeline plus
	// the headline CPU/GPU/draw/memory metrics. Deliberately separate from the RendererDebuggerPanel,
	// which keeps the deep per-pass profiler.
	class StatisticsPanel : public EditorPanel
	{
	public:
		StatisticsPanel() = default;
		virtual ~StatisticsPanel() = default;

		void SetContext(const Ref<SceneRenderer>& context) { m_Context = context; }
		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		void DrawFrameTimeline(float frameTimeMs);

		Ref<SceneRenderer> m_Context;
		std::vector<float> m_FrameTimeHistory;
		// X positions for the timeline plot (-(N-1)..0), rebuilt each frame to match the ring size.
		std::vector<float> m_FrameTimeAxis;
	};

}
