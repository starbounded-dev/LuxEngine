#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/SceneRenderer.h"

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

		Ref<SceneRenderer> m_Context;
		std::string m_ShaderSearch;
		uint32_t m_LastReloadedShaderCount = 0;
		bool m_ProjectRendererSettingsDirty = false;
	};

}
