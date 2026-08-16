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
		// The scene owns the post-processing settings (exposure model, grading, tonemap)
		// now that the per-volume system is gone, so the panel needs it to edit them.
		virtual void SetSceneContext(const Ref<Scene>& context) override { m_Scene = context; }
		void SetDebugViewCallbacks(std::function<void()> onResetDebugViews, std::function<void()> onDebugViewsChanged);
		void SetDebugViewsRuntimeSuspended(bool suspended);
		virtual void OnImGuiRender(bool& isOpen) override;

	private:
		void ApplyProjectSettingsToContext();
		void SyncProjectSettingsFromContext();
		bool SaveProjectRendererSettings();

		Ref<SceneRenderer> m_Context;
		Ref<Scene> m_Scene;
		std::function<void()> m_OnResetDebugViews;
		std::function<void()> m_OnDebugViewsChanged;
		bool m_ProjectRendererSettingsDirty = false;
		bool m_DebugViewsRuntimeSuspended = false;
	};

}
