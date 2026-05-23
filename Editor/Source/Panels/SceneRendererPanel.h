#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/SceneRenderer.h"

#include <functional>

namespace Lux {

	class SceneRendererPanel : public EditorPanel
	{
	public:
		SceneRendererPanel() = default;
		virtual ~SceneRendererPanel() = default;

		void SetContext(const Ref<SceneRenderer>& context);
		void SetDebugViewCallbacks(std::function<void()> onResetDebugViews, std::function<void()> onDebugViewsChanged);
		void SetDebugViewsRuntimeSuspended(bool suspended);
		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		void ApplyProjectSettingsToContext();
		void SyncProjectSettingsFromContext();
		bool SaveProjectRendererSettings();

		Ref<SceneRenderer> m_Context;
		std::function<void()> m_OnResetDebugViews;
		std::function<void()> m_OnDebugViewsChanged;
		bool m_ProjectRendererSettingsDirty = false;
		bool m_DebugViewsRuntimeSuspended = false;
	};

}
