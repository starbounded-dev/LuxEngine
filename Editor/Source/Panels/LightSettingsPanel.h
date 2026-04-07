#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Editor/EditorPanel.h"

#include <optional>

namespace Lux {

	class Scene; // forward declare

	class LightSettingsPanel : public EditorPanel
	{
	public:
		LightSettingsPanel();
		virtual ~LightSettingsPanel() = default;

		virtual void OnImGuiRender(bool& isOpen) override;

		void SetSceneContext(const Ref<Scene>& context) { m_Context = context; }
		Ref<Scene> GetSceneContext() const { return m_Context; }

	private:
		void RenderDirectionalLight();
		void RenderPointLights();
		void RenderSpotLights();
		void RenderSkyLight();

	private:
		Ref<Scene> m_Context;
		int m_SelectedLightIndex = -1;
	};

}
