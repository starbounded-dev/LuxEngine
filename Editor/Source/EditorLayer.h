#pragma once

#include "Lux.h"

#include "Lux/Project/Project.h"

#include <functional>
#include <map>

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

		// One undo step. A scene edit carries a per-entity diff; a non-scene edit (renderer settings,
		// …) carries closures instead — when CustomUndo is set, the diff fields are ignored.
		struct EntityDelta
		{
			UUID Handle = 0;
			std::string Before;   // empty => entity did not exist before the edit
			std::string After;    // empty => entity does not exist after the edit
		};
		struct UndoCommand
		{
			std::string Label;
			bool MetaChanged = false;
			std::string MetaBefore;
			std::string MetaAfter;
			std::vector<EntityDelta> Entities;
			std::function<void()> CustomUndo;
			std::function<void()> CustomRedo;
			size_t ApproxBytes = 0;   // heap payload, for the memory budget (see TrimUndoStack)
		};

		// Undo/redo (snapshot-based, granular per-entity storage). See docs/Editor/Undo-Redo.md.
		// The edit-mode history targets m_EditorScene; a separate transient play-mode history targets
		// the runtime m_ActiveScene and is discarded on Stop. UndoSceneEdit/RedoSceneEdit operate on
		// whichever is active for the current SceneState.
		std::map<UUID, std::string> CaptureSceneEntities(const Ref<Scene>& scene, std::string& outMeta) const;
		void ResetUndoHistory();                                  // baseline = current, clear stacks
		void CommitSceneSnapshot();                               // diff vs baseline; push a step for the changed entities
		void PollSceneEditForUndo();                              // per-frame: commit once an active edit finishes
		void UndoSceneEdit();
		void RedoSceneEdit();
		void RestoreSceneState(const std::string& meta, const std::map<UUID, std::string>& entities);
		void RestoreSelection(const std::vector<UUID>& handles);  // select the entities an undo/redo touched
		void AdoptEditorScene(const Ref<Scene>& scene);           // retarget panels/viewport/renderer

		// Play-mode undo (Phase 7): a transient stack over the runtime scene, discarded on Stop. An
		// undo restores by rebuilding the runtime scene from a snapshot and restarting its runtime, so
		// physics/scripts reset to that point.
		std::vector<UndoCommand>& ActiveUndoStack() { return m_SceneState == SceneState::Edit ? m_UndoStack : m_PlayUndoStack; }
		std::vector<UndoCommand>& ActiveRedoStack() { return m_SceneState == SceneState::Edit ? m_RedoStack : m_PlayRedoStack; }
		void BeginPlayUndoHistory();                              // baseline the runtime scene, clear play stacks
		void CommitPlaySnapshot();                                // diff the runtime scene, push a play step
		void RestoreRuntimeState(const std::string& meta, const std::map<UUID, std::string>& entities);
		void AdoptRuntimeScene(const Ref<Scene>& scene);          // stop old runtime, swap, restart, retarget

		// Non-scene undo commands push through this (renderer/project settings, and future subsystems).
		void PushUndoCommand(const std::string& label, std::function<void()> undo, std::function<void()> redo);
		void TrimUndoStack(std::vector<UndoCommand>& stack);      // evict oldest steps past the size/byte budget
		// Renderer/project settings (Phase 6): captured/compared as a ProjectSceneRendererSettings,
		// which excludes the transient debug-view toggles by construction.
		ProjectSceneRendererSettings CaptureRendererSettings() const;
		void CaptureRendererSettingsBaseline();
		void CommitRendererSettings();
		void ApplyRendererSettings(const ProjectSceneRendererSettings& settings);

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

		// Undo/redo: a labelled command stack with granular, per-entity storage. Each step records
		// only the entities that actually changed (before/after YAML; an empty string means the entity
		// was absent — i.e. created or deleted), plus the scene metadata if it changed — so history is
		// O(change), not O(scene). m_Baseline* is the current committed state kept whole; a commit
		// diffs the scene against it and stores the difference. Restore reassembles the target state
		// and runs the whole-scene deserialize, which is why it stays safe against the two-way
		// parent/child links. m_UndoCommitPending defers the commit until the active edit finishes.
		// (EntityDelta / UndoCommand are defined near the top of the class, above the undo methods.)
		std::vector<UndoCommand> m_UndoStack;
		std::vector<UndoCommand> m_RedoStack;
		std::string m_BaselineMeta;
		std::map<UUID, std::string> m_BaselineEntities;
		ProjectSceneRendererSettings m_RendererSettingsBaseline;

		// Transient play-mode history (over the runtime m_ActiveScene); baselined on Play/Simulate,
		// discarded on Stop.
		std::vector<UndoCommand> m_PlayUndoStack;
		std::vector<UndoCommand> m_PlayRedoStack;
		std::string m_PlayBaselineMeta;
		std::map<UUID, std::string> m_PlayBaselineEntities;
		std::string m_PendingUndoLabel = "Edit";
		bool m_UndoCommitPending = false;
		bool m_GizmoWasUsing = false;
		// History is bounded by both a step count and a memory budget; a commit evicts the oldest steps
		// until it is under both (but always keeps at least one, even if a single step is huge).
		static constexpr size_t s_MaxUndoDepth = 256;
		static constexpr size_t s_MaxUndoBytes = 128ull * 1024 * 1024;   // 128 MB of snapshot payload
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
