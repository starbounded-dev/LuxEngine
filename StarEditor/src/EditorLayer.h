#pragma once

#include "StarEngine.h"

#include "Panels/SceneHierarchyPanel.h"
#include "Panels/ContentBrowserPanel.h"

#include "StarEngine/Scene/Entity.h"
#include "StarEngine/Editor/EditorCamera.h"

#include "entt.hpp"
#include "StarEngine/Editor/EditorConsolePanel.h"
#include "StarEngine/Editor/PanelManager.h"
#include "StarEngine/Project/UserPreferences.h"
#include "StarEngine/Renderer/OrthographicCameraController.h"

namespace StarEngine
{
	class EditorLayer : public Layer
	{
	public:
		EditorLayer(const Ref<UserPreferences>& userPreferences);
		virtual ~EditorLayer() override;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(Timestep ts) override;

		virtual void OnImGuiRender() override;
		virtual void OnEvent(Event& e) override;

		bool OnKeyPressedEvent(KeyPressedEvent& e);
		bool OnMouseButtonPressed(MouseButtonPressedEvent& e);

		void OpenProject();
		void OpenProject(const std::filesystem::path& filepath);

		void CreateProject(std::filesystem::path projectPath);
		void EmptyProject();
		void UpdateCurrentProject();
		void SaveProject();
		void CloseProject(bool unloadProject = true);

		void NewScene(const std::string& name = "UntitledScene");
		bool OpenScene();
		bool OpenScene(const std::filesystem::path& filepath, const bool checkAutoSave = true);

		void SaveScene();
		void SaveSceneAuto();
		void SaveSceneAs();
	private:
		void OnOverlayRender();

		void SerializeScene(Ref<Scene> scene, const std::filesystem::path& filepath);

		void OnScenePlay();
		void OnSceneSimulate();
		void OnSceneStop();
		void OnScenePause();

		void OnDuplicateEntity();

		// UI Panels
		void UI_Toolbar();
	private:
		Ref<UserPreferences> m_UserPreferences;

		Scope<PanelManager> m_PanelManager;
		Ref<EditorConsolePanel> m_ConsolePanel;
		bool m_ShowStatisticsPanel = false;

		std::vector<Ref<Viewport>> m_EditorViewports;

		Ref<Scene> m_RuntimeScene, m_EditorScene, m_SimulationScene, m_CurrentScene;
		std::string m_SceneFilePath;

		enum class SceneState
		{
			Edit = 0, Play = 1, Pause = 2, Simulate = 3
		};
		SceneState m_SceneState = SceneState::Edit;
	};
}

