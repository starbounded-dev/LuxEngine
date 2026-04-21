#pragma once

#include "Lux/Core/Log.h"
#include "Lux/Editor/EditorPanel.h"

namespace Lux {

	class ProjectSettingsWindow : public EditorPanel
	{
	public:
		ProjectSettingsWindow();
		virtual ~ProjectSettingsWindow() = default;

		virtual void OnImGuiRender(bool& isOpen) override;
		virtual void OnProjectChanged(const Ref<Project>& project) override;
		virtual void OnClose() override;

	private:
		void SyncBuffersFromProject();
		void SaveProject();

		void RenderGeneralSettings();
		void RenderRendererSettings();
		void RenderAudioSettings();
		void RenderScriptingSettings();
		void RenderPhysicsSettings();
		void RenderLogSettings();

	private:
		Ref<Project> m_Project;
		AssetHandle m_DefaultScene = 0;
		bool m_Dirty = false;

		char m_NameBuffer[256]{};
		char m_ScriptModulePathBuffer[512]{};
	};

}
