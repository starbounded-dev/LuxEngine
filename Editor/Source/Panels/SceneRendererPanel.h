#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/SceneRenderer.h"

namespace Lux {

	class SceneRendererPanel : public EditorPanel
	{
	public:
		SceneRendererPanel() = default;
		virtual ~SceneRendererPanel() = default;

		void SetContext(const Ref<SceneRenderer>& context) { m_Context = context; }
		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		Ref<SceneRenderer> m_Context;
	};

}
