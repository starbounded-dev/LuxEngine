// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Log.h"
#include "Lux/Audio/AudioLegacyCleanup.h"
#include "Lux/Editor/EditorPanel.h"
#include <optional>
#include <string>
#include <vector>

namespace Lux {

	namespace RuntimeExport { class RuntimeBuildJob; }

	class ProjectSettingsWindow : public EditorPanel
	{
	public:
		// The build job is EditorLayer's, shared with the export window so only one build runs.
		explicit ProjectSettingsWindow(RuntimeExport::RuntimeBuildJob& runtimeBuildJob);
		virtual ~ProjectSettingsWindow() = default;

		virtual void OnImGuiRender(bool& isOpen) override;
		virtual void OnProjectChanged(const Ref<Project>& project) override;
		virtual void OnClose() override;
		void SetSceneContext(const Ref<Scene>& context) override { m_SceneContext = context; }
		// Startup-only: requires main thread, before any editor ImGui/render frame.
		static bool RunAcousticSettingsSelfTests(std::vector<std::string>& failures);

	private:
		void SyncBuffersFromProject();
		void SaveProject();

		void RenderGeneralSettings();
		void RenderRuntimeExportSettings();
		void RenderRendererSettings();
		void RenderAudioSettings();
		void RenderLegacyAudioData();
		void RemoveLegacyAudioData();
		void RenderScriptingSettings();
		void RenderPhysicsSettings();
		void RenderLogSettings();

	private:
		RuntimeExport::RuntimeBuildJob& m_RuntimeBuildJob;
		Ref<Project> m_Project;
		Ref<Scene> m_SceneContext;
		std::optional<AudioLegacyCleanup::ScanResult> m_LegacyScan;
		std::vector<std::string> m_LegacyErrors; // from the last Remove
		AssetHandle m_DefaultScene = 0;
		AssetHandle m_RuntimeIcon = 0;
		bool m_Dirty = false;

		char m_NameBuffer[256]{};
		char m_RuntimeGameNameBuffer[256]{};
		char m_DefaultNamespaceBuffer[256]{};
		char m_ScriptModulePathBuffer[512]{};
	};

}
