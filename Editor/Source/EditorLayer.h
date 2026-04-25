#pragma once

#include "Lux.h"

#include "Panels/RenderStatsPanel.h"
#include "Panels/MaterialEditorPanel.h"
#include "Panels/LightSettingsPanel.h"
#include "Lux/Editor/EditorConsolePanel.h"

#include "Lux/Asset/Asset.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Editor/EditorCamera.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Project/UserPreferences.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/Framebuffer.h"
#include "Lux/Renderer/Shader.h"
#include "Lux/Renderer/VertexBuffer.h"

#include "entt/entt.hpp"
#include "Lux/Editor/PanelManager.h"
#include "Lux/Renderer/SceneRenderer.h"

namespace Lux
{
	class SceneHierarchyPanel;
	class SceneRendererPanel;

	class EditorLayer : public Layer
	{
	public:
		EditorLayer();
		virtual ~EditorLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;

		virtual void OnUpdate(Timestep ts) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Event& e) override;
	private:
		bool OnKeyPressed(KeyPressedEvent& e);
		bool OnMouseButtonPressed(MouseButtonPressedEvent& e);
		bool OnTitleBarHitTest(WindowTitleBarHitTestEvent& e);
		//bool OnWindowDrop(WindowDropEvent& e);

		void OnOverlayRender();
		void UI_DrawTitlebar();
		void UI_DrawMenubar();
		void UI_GizmosToolbar();
		void UI_CentralToolbar();
		void UI_ViewportSettings();
		Entity CastMousePick();
		bool RayIntersectsEntity(Entity entity, const glm::vec3& rayOrigin, const glm::vec3& rayDirection, float& outDistance) const;

		void NewProject();
		bool OpenProject();
		void OpenProject(const std::filesystem::path& path);
		void SaveProject();

		void NewScene();
		void OpenScene();
		void OpenScene(AssetHandle handle);
		void SaveScene();
		void SaveSceneAs();

		void SerializeScene(Ref<Scene> scene, const std::filesystem::path& filepath);

		void OnScenePlay();
		void OnSceneSimulate();
		void OnSceneStop();
		void OnScenePause();

		void OnDuplicateEntity();

		// UI Panels
		void UI_Toolbar();
		void LoadEditorPreferences();
		void SaveEditorPreferences() const;
		void ApplyEditorPreferences();
		void LoadUserPreferences();
		void SaveUserPreferences() const;
		void AddRecentProject(const std::filesystem::path& projectPath);
		std::vector<RecentProject> GetRecentProjects() const;
		std::filesystem::path GetStartupProjectPath() const;
	private:
		EditorCamera m_EditorCamera;

		Scope<PanelManager> m_PanelManager;

		bool m_VSync = true;
		bool m_ViewportFocused = false, m_ViewportHovered = false;

		Ref<Renderer2D> m_Renderer2D;

		Ref<SceneRenderer> m_SceneRenderer;
		Ref<SceneHierarchyPanel> m_SceneHierarchyPanel;
		Ref<SceneRendererPanel> m_SceneRendererPanel;
		Ref<EditorConsolePanel> m_ConsolePanel;

		// Temp
		Ref<VertexBuffer> m_SquareVA;
		Ref<Shader> m_FlatColorShader;
		Ref<Framebuffer> m_Framebuffer;

		Ref<Scene> m_ActiveScene;
		Ref<Scene> m_EditorScene;
		std::filesystem::path m_EditorScenePath;
		Ref<UserPreferences> m_UserPreferences;
		std::filesystem::path m_UserPreferencesPath;
		Entity m_SquareEntity;
		Entity m_CameraEntity;
		Entity m_SecondCamera;

		Entity m_HoveredEntity;

		bool m_PrimaryCamera = true;

		Ref<Texture2D> m_CheckerboardTexture;

		glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
		glm::vec2 m_ViewportBounds[2];

		glm::vec4 m_SquareColor = { 0.2f, 0.3f, 0.8f, 1.0f };

		int m_GizmoType = -1;

		bool m_ShowPhysicsColliders = false;
		bool m_ShowBoundingBoxes = false;
		bool m_ShowEntityIcons = true;
		bool m_UseGizmoSnap = false;
		float m_TranslationSnapValue = 0.5f;
		float m_RotationSnapValue = 45.0f;

		enum class SceneState
		{
			Edit = 0, Play = 1, Simulate = 2
		};
		SceneState m_SceneState = SceneState::Edit;

		enum class ViewportDisplayMode
		{
			Lit = 0,
			SelectedWireframe = 1
		};
		ViewportDisplayMode m_ViewportDisplayMode = ViewportDisplayMode::Lit;

		ImVec4 m_AnimatedTitlebarColor = ImGui::ColorConvertU32ToFloat4(Colors::Theme::titlebar);
		ImVec2 m_TitleBarDragRectMin = { 0.0f, 0.0f };
		ImVec2 m_TitleBarDragRectMax = { 0.0f, 0.0f };
		float m_TitlebarHeight = 57.0f;

		bool m_ShowImGuiMetrics = false;
		bool m_ShowImGuiStyleEditor = false;
		bool m_ShowAboutPopup = false;
		bool m_SecondViewportEnabled = false;

		// Editor resources
		Ref<Texture2D> m_IconPlay, m_IconPause, m_IconStep, m_IconSimulate, m_IconStop;
	};
}
