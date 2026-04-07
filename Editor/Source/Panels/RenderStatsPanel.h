#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/SceneRenderer.h"

namespace Lux {

	class RenderStatsPanel : public EditorPanel
	{
	public:
		RenderStatsPanel() = default;
		virtual ~RenderStatsPanel() = default;

		void SetRenderer2D(const Ref<Renderer2D>& renderer) { m_Renderer2D = renderer; }
		void SetSceneRenderer(const Ref<SceneRenderer>& renderer) { m_SceneRenderer = renderer; }

		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		Ref<Renderer2D> m_Renderer2D;
		Ref<SceneRenderer> m_SceneRenderer;
	};

}
