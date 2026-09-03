#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/SceneRenderer.h"

namespace Lux {

	// Inspects the SceneRenderer's intermediate render targets (final composite, HDR scene colour, and
	// the G-buffer attachments). It only reads stable, renderer-owned images via the existing accessors
	// and displays them with ImGuiEx::Image — no render-graph or shader changes, so nothing here can
	// perturb the frame it is viewing.
	class FrameDebuggerPanel : public EditorPanel
	{
	public:
		FrameDebuggerPanel() = default;
		virtual ~FrameDebuggerPanel() = default;

		void SetContext(const Ref<SceneRenderer>& context) { m_Context = context; }

		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		Ref<Image2D> FetchTarget(int target);

	private:
		Ref<SceneRenderer> m_Context;
		int m_SelectedTarget = 0;
		bool m_FitToWindow = true;
		float m_Zoom = 1.0f;
	};

}
