#pragma once

#include "Lux.h"

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
#include "Viewport/Viewport.h"

namespace Lux
{
	class SceneHierarchyPanel;
	class SceneRendererPanel;
	class RendererDebuggerPanel;
	class StatisticsPanel;

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
		void ResetDefaultDockLayout(ImGuiID dockspaceId);
		void SetEditorLayoutMode(bool simple);
		void UI_TitlebarTransport(float titlebarWidth);
		void UI_ViewportSettings();
		void UI_ViewportOrientationGizmo();
		void UI_ViewportSelectionBadge();
		void UI_ViewportPerformanceHUD();
		Entity CastMousePick();
		bool RayIntersectsEntity(Entity entity, const glm::vec3& rayOrigin, const glm::vec3& rayDirection, float& outDistance) const;

		void NewProject();
		bool OpenProject();
		void OpenProject(const std::filesystem::path& path);
		void SaveProject();
		void ExportRuntime();
		bool ExportRuntimeNow();
		void RenderRuntimeExportWindow();
		void SyncRuntimeExportWindowFromProject();

		void NewScene();
		void OpenScene();
		void OpenScene(AssetHandle handle);
		void SaveScene();
		void SaveSceneAs();

		void SerializeScene(Ref<Scene> scene, const std::filesystem::path& filepath);

		void UpdateDiscordPresence();

		void OnScenePlay();
		void OnSceneSimulate();
		void OnSceneStop();
		void OnScenePause();
		void ResetRendererDebugViews();
		void SyncEditorDebugViewsFromRenderer();
		void SuspendRendererDebugViewsForPlay();
		void RestoreRendererDebugViewsAfterPlay();

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
		Scope<PanelManager> m_PanelManager;

		bool m_VSync = true;
		// Frames per second the main loop is paced to when VSync is off. 0 == unlimited.
		int m_TargetFrameRate = 0;
		// Swapchain image count. Under MAILBOX this sets the frame-rate ceiling at roughly
		// (count - 1) x display refresh, so it is a perf control, not just memory.
		int m_SwapChainBufferCount = 3;
		// IMMEDIATE (tears, can exceed refresh) vs MAILBOX (tear-free) when VSync is off.
		bool m_PreferImmediatePresentMode = false;

		Ref<Renderer2D> m_Renderer2D;

		Ref<Viewport> m_EditorViewport;
		Ref<SceneRenderer> m_SceneRenderer;
		Ref<SceneHierarchyPanel> m_SceneHierarchyPanel;
		Ref<SceneRendererPanel> m_SceneRendererPanel;
		Ref<RendererDebuggerPanel> m_RendererDebuggerPanel;
		Ref<StatisticsPanel> m_StatisticsPanel;
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

		glm::vec4 m_SquareColor = { 0.2f, 0.3f, 0.8f, 1.0f };

		int m_GizmoType = -1;

		bool m_ShowPhysicsColliders = false;
		bool m_ShowBoundingBoxes = false;
		bool m_ShowEntityIcons = true;
		bool m_ShowViewportPerformanceHUD = true;

		// Editor layout mode: Simple (a fixed, minimal default arrangement) vs Advanced (a fuller
		// workspace with the diagnostic panels docked). Persisted as Editor.SimpleLayout.
		bool m_SimpleLayout = true;
		// ResetDefaultDockLayout needs the live dockspace id, only valid inside OnImGuiRender, so a
		// mode switch requested from a menu defers the rebuild to the next frame via this flag.
		bool m_PendingLayoutReset = false;
		bool m_ShowRuntimeExportWindow = false;
		bool m_UseGizmoSnap = false;
		float m_TranslationSnapValue = 0.5f;
		float m_RotationSnapValue = 45.0f;
		AssetHandle m_RuntimeExportIcon = 0;
		char m_RuntimeExportGameNameBuffer[256] = {};

		enum class SceneState
		{
			Edit = 0, Play = 1, Simulate = 2
		};
		SceneState m_SceneState = SceneState::Edit;

		struct RendererDebugViewState
		{
			bool ShowGrid = false;
			bool ShowSelectedInWireframe = false;
			bool ShowPhysicsColliders = false;
			SceneRendererOptions::PhysicsColliderView PhysicsColliderMode = SceneRendererOptions::PhysicsColliderView::SelectedEntity;
			bool ShowPhysicsCollidersOnTop = false;
			bool ShowShadowCascades = false;
			bool ShowCascadeFrustums = false;
			bool ShowLightComplexity = false;
			bool ShowMaterialComplexity = false;
			bool ShowBoundingBoxes = false;
			bool ShowEntityIcons = true;
			SceneRenderer::DebugViewMode RendererDebugView = SceneRenderer::DebugViewMode::Final;
			Viewport::DisplayMode DisplayMode = Viewport::DisplayMode::Lit;
		};
		RendererDebugViewState m_PlayModeDebugViewState;
		bool m_PlayModeDebugViewsSuspended = false;

		ImVec4 m_AnimatedTitlebarColor = ImGui::ColorConvertU32ToFloat4(Colors::Theme::titlebar);
		ImVec2 m_TitleBarDragRectMin = { 0.0f, 0.0f };
		ImVec2 m_TitleBarDragRectMax = { 0.0f, 0.0f };
		// Local-space rect of the titlebar transport, excluded from the drag zone so its buttons
		// stay clickable instead of initiating a window move.
		ImVec2 m_TitleBarTransportRectMin = { 0.0f, 0.0f };
		ImVec2 m_TitleBarTransportRectMax = { 0.0f, 0.0f };
		float m_TitlebarHeight = 57.0f;

		bool m_ShowImGuiMetrics = false;
		bool m_ShowImGuiStyleEditor = false;
		bool m_ShowAboutPopup = false;
		bool m_SecondViewportEnabled = false;

		// Editor resources
		Ref<Texture2D> m_IconPlay, m_IconPause, m_IconStep, m_IconSimulate, m_IconStop;
	};
}
