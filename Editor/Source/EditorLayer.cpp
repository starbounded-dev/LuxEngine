    #include "EditorLayer.h"

#include "Lux/Scene/SceneSerializer.h"
#include "Lux/Core/Application.h"
#include "Lux/Scripting/ScriptEngine.h"
#include "Lux/Renderer/UI/Font.h"
#include "Lux/Renderer/SceneRenderer.h"

#include "Lux/Utilities/FileSystem.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Asset/TextureImporter.h"
#include "Lux/Asset/SceneImporter.h"
#include "Lux/Scene/Prefab.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <GLFW/glfw3.h>

#include <imgui/imgui.h>
#include "imgui/imgui_internal.h"
#include <GLFW/glfw3.h>
#include "ImGuizmo.h"
#include "Lux/Debug/Profiler.h"
#include "Lux/Editor/EditorResources.h"
#include "Lux/ImGui/ImGuiFonts.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/ImGui/ImGuiCore.h"
#include "Lux/Utilities/CommandLineParser.h"
#include "Lux/Utilities/FileDialogs.h"
#include "Lux/Math/Math.h"
#include "Panels/TextEditorPanel.h"
#include "Panels/SceneHierarchyPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/SceneRendererPanel.h"

namespace Lux {

#define MAX_PROJECT_NAME_LENGTH 255
#define MAX_PROJECT_FILEPATH_LENGTH 512

	static char* s_ProjectNameBuffer = new char[MAX_PROJECT_NAME_LENGTH];
	static char* s_OpenProjectFilePathBuffer = new char[MAX_PROJECT_FILEPATH_LENGTH];
	static char* s_NewProjectFilePathBuffer = new char[MAX_PROJECT_FILEPATH_LENGTH];

#define SCENE_HIERARCHY_PANEL_ID "SceneHierarchyPanel"
#define ECS_DEBUG_PANEL_ID "ECSDebugPanel"
#define CONSOLE_PANEL_ID "EditorConsolePanel"
#define CONTENT_BROWSER_PANEL_ID "ContentBrowserPanel"
#define PROJECT_SETTINGS_PANEL_ID "ProjectSettingsPanel"
#define PHYSICS_DEBUG_PANEL_ID "PhysicsDebugPanel"
#define ASSET_MANAGER_PANEL_ID "AssetManagerPanel"
#define MATERIALS_PANEL_ID "MaterialsPanel"
#define AUDIO_EVENTS_EDITOR_PANEL_ID "AudioEventsEditor"
#define APPLICATION_SETTINGS_PANEL_ID "ApplicationSettingsPanel"
#define SCRIPT_ENGINE_DEBUG_PANEL_ID "ScriptEngineDebugPanel"
#define SCENE_RENDERER_PANEL_ID "SceneRendererPanel"
#define PHYSICS_CAPTURES_PANEL_ID "PhysicsCapturesPanel"

	namespace {
		ImTextureID GetImGuiTextureID(const Lux::Ref<Lux::Texture2D>& texture)
		{
			auto* imguiRenderer = Lux::Application::Get().GetImGuiLayer()->GetImGuiRenderer();
			return imguiRenderer->CreateFrameTexture(texture->GetImage()->GetHandle().Get(), nvrhi::AllSubresources);
		}

		ImTextureID GetImGuiTextureID(const Lux::Ref<Lux::Image2D>& image)
		{
			auto* imguiRenderer = Lux::Application::Get().GetImGuiLayer()->GetImGuiRenderer();
			return imguiRenderer->CreateFrameTexture(image->GetHandle().Get(), nvrhi::AllSubresources);
		}

		std::string GetSceneDisplayName(const std::filesystem::path& scenePath)
		{
			if (scenePath.empty())
				return "Untitled Scene";

			return scenePath.stem().string();
		}

		std::string GetProjectDisplayName()
		{
			Ref<Project> activeProject = Project::GetActive();
			if (!activeProject)
				return "No Project";

			const auto& name = activeProject->GetConfig().Name;
			return name.empty() ? "Untitled Project" : name;
		}
	}

	struct SceneRendererRuntimeState
	{
		Ref<SceneRenderer> Renderer;
		Ref<Scene> SceneContext;
		bool PanelOpen = true;
	};

	static SceneRendererRuntimeState s_SceneRendererState;

	static void ResetSceneRenderer()
	{
		s_SceneRendererState.Renderer.reset();
		s_SceneRendererState.SceneContext.reset();
	}

	static void EnsureSceneRenderer(const Ref<Scene>& scene, const glm::vec2& viewportSize)
	{
		if (!scene)
		{
			ResetSceneRenderer();
			return;
		}

		if (!s_SceneRendererState.Renderer)
		{
			SceneRendererSpecification spec{};
			if (viewportSize.x > 1.0f && viewportSize.y > 1.0f)
			{
				spec.ViewportWidth = (uint32_t)viewportSize.x;
				spec.ViewportHeight = (uint32_t)viewportSize.y;
			}

			s_SceneRendererState.Renderer = Ref<SceneRenderer>::Create(scene, spec);
			s_SceneRendererState.SceneContext = scene;
		}
		else if (s_SceneRendererState.SceneContext.Raw() != scene.Raw())
		{
			s_SceneRendererState.Renderer->SetScene(scene);
			s_SceneRendererState.SceneContext = scene;
		}

		if (viewportSize.x > 1.0f && viewportSize.y > 1.0f)
			s_SceneRendererState.Renderer->SetViewportSize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
	}

	static Ref<Font> s_Font;
	static bool s_ShowFontAtlasInStats = false;

	EditorLayer::EditorLayer()
		: Layer("EditorLayer"), m_EditorCamera(60.0f, 1600.0f, 900.0f, 0.1f, 10000.0f), m_SquareColor({ 0.2f, 0.3f, 0.8f, 1.0f })
	{
		s_Font = Font::GetDefaultFont();
	}

	void EditorLayer::OnAttach()
	{
		LUX_PROFILE_FUNCTION("EditorLayer::OnAttach");

		EditorResources::Init();

		/////////// Configure Panels ///////////
		m_PanelManager = CreateScope<PanelManager>();

		m_SceneHierarchyPanel = m_PanelManager->AddPanel<SceneHierarchyPanel>(PanelCategory::View, SCENE_HIERARCHY_PANEL_ID, "Scene Hierarchy", true);
		m_PanelManager->AddPanel<ContentBrowserPanel>(PanelCategory::View, CONTENT_BROWSER_PANEL_ID, "Content Browser", true);
		m_PanelManager->AddPanel<TextEditorPanel>(PanelCategory::View, "TextEditorPanel", "Text Editor", true);

		m_SceneRendererPanel = m_PanelManager->AddPanel<SceneRendererPanel>(PanelCategory::View, SCENE_RENDERER_PANEL_ID, "Scene Renderer", true);

		// Console panel for log messages
		m_PanelManager->AddPanel<ConsolePanel>(PanelCategory::View, CONSOLE_PANEL_ID, "Console", true);

		// Render statistics panel
		Ref<RenderStatsPanel> renderStatsPanel = m_PanelManager->AddPanel<RenderStatsPanel>(PanelCategory::View, "RenderStatsPanel", "Render Stats", true);
		renderStatsPanel->SetRenderer2D(m_Renderer2D);
		renderStatsPanel->SetSceneRenderer(m_SceneRenderer);

		// Material Editor panel
		m_PanelManager->AddPanel<MaterialEditorPanel>(PanelCategory::View, "MaterialEditorPanel", "Material Editor", true);

		// Light Settings panel
		m_PanelManager->AddPanel<LightSettingsPanel>(PanelCategory::View, "LightSettingsPanel", "Light Settings", true);

		m_IconPlay = TextureImporter::LoadTexture2D("Resources/Editor/Viewport/Play.png");
		m_IconPause = TextureImporter::LoadTexture2D("Resources/Editor/Viewport/Pause.png");
		m_IconSimulate = TextureImporter::LoadTexture2D("Resources/Editor/Viewport/Simulate.png");
		m_IconStep = TextureImporter::LoadTexture2D("Resources/Editor/Viewport/Step.png");
		m_IconStop = TextureImporter::LoadTexture2D("Resources/Editor/Viewport/Stop.png");

		m_Renderer2D = Ref<Renderer2D>::Create();
		m_Renderer2D->SetLineWidth(4.0f);

		FramebufferSpecification fbSpec;
		fbSpec.Attachments = { ImageFormat::RGBA32F, ImageFormat::Depth };
		fbSpec.Width = 1280;
		fbSpec.Height = 720;
		fbSpec.ClearColorOnLoad = true;
		fbSpec.DebugName = "EditorFramebuffer";
		m_Framebuffer = Framebuffer::Create(fbSpec);

		m_Renderer2D->SetTargetFramebuffer(m_Framebuffer);

		EnsureSceneRenderer(m_ActiveScene, m_ViewportSize);
		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetContext(s_SceneRendererState.Renderer);

		m_EditorCamera.SetActive(true);

		m_EditorScene = Ref<Scene>::Create();
		m_EditorScene->SetTargetFramebuffer(m_Framebuffer);
		m_ActiveScene = m_EditorScene;

		SceneRendererSpecification sceneRendererSpec;
		sceneRendererSpec.ViewportWidth = 1280;
		sceneRendererSpec.ViewportHeight = 720;

		m_SceneRenderer = Ref<SceneRenderer>::Create(m_ActiveScene, sceneRendererSpec);
		m_PanelManager->SetSceneContext(m_EditorScene);
		m_PanelManager->OnProjectChanged(Project::GetActive());

		EnsureSceneRenderer(m_ActiveScene, m_ViewportSize);
		if (s_SceneRendererState.Renderer)
			s_SceneRendererState.Renderer->GetOptions().ShowPhysicsColliders = m_ShowPhysicsColliders;
	}

	void EditorLayer::OnDetach()
	{
		LUX_PROFILE_FUNCTION("EditorLayer::OnDetach");

		ResetSceneRenderer();

		m_ActiveScene.reset();
		m_EditorScene.reset();
		m_Framebuffer.reset();
		m_CheckerboardTexture.reset();
		m_IconPlay.reset();
		m_IconStop.reset();
		m_IconSimulate.reset();
		m_IconPause.reset();
		m_IconStep.reset();
		m_SquareVA.reset();
		m_FlatColorShader.reset();
		m_SceneRendererPanel.reset();
		m_SceneHierarchyPanel.reset();
		s_Font.reset();
		EditorResources::Shutdown();
	}

	void EditorLayer::OnUpdate(Timestep ts)
	{
		LUX_PROFILE_FUNCTION("EditorLayer::OnUpdate");

		if (!m_ActiveScene || !m_Framebuffer)
			return;

		if (m_ViewportSize.x > 1.0f && m_ViewportSize.y > 1.0f)
		{
			uint32_t viewportWidth = (uint32_t)m_ViewportSize.x;
			uint32_t viewportHeight = (uint32_t)m_ViewportSize.y;

			if (m_Framebuffer->GetWidth() != viewportWidth || m_Framebuffer->GetHeight() != viewportHeight)
			{
				m_Framebuffer->Resize(viewportWidth, viewportHeight);
				m_EditorCamera.SetViewportBounds(0, 0, viewportWidth, viewportHeight);
			}

			m_ActiveScene->SetTargetFramebuffer(m_Framebuffer);
			m_ActiveScene->OnViewportResize(viewportWidth, viewportHeight);

			if (m_SceneRenderer)
				m_SceneRenderer->SetViewportSize(viewportWidth, viewportHeight);
		}

		EnsureSceneRenderer(m_ActiveScene, m_ViewportSize);
		if (s_SceneRendererState.Renderer)
			s_SceneRendererState.Renderer->GetOptions().ShowPhysicsColliders = m_ShowPhysicsColliders;

		m_Renderer2D->ResetStats();

		Entity selectedEntity = {};
		if (m_SceneHierarchyPanel)
			selectedEntity = m_SceneHierarchyPanel->GetSelectedEntity();
		auto isEntitySelected = [selectedEntity](Entity entity) -> bool {
			return selectedEntity && entity == selectedEntity;
			};

		switch (m_SceneState)
		{
		case SceneState::Edit:
		{
			m_EditorCamera.OnUpdate(ts);

			if (m_SceneRenderer && m_SceneRenderer->IsReady())
				m_ActiveScene->Render3D(m_EditorCamera, m_SceneRenderer, isEntitySelected);

			m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);
			break;
		}
		case SceneState::Simulate:
		{
			m_EditorCamera.OnUpdate(ts);

			if (m_SceneRenderer && m_SceneRenderer->IsReady())
				m_ActiveScene->Render3D(m_EditorCamera, m_SceneRenderer, isEntitySelected);

			m_ActiveScene->OnUpdateSimulation(ts, m_EditorCamera);
			break;
		}
		case SceneState::Play:
		{
			if (m_SceneRenderer && m_SceneRenderer->IsReady())
				m_ActiveScene->Render3DRuntime(m_SceneRenderer);

			m_ActiveScene->OnUpdateRuntime(ts);
			break;
		}
		}

		OnOverlayRender();
	}

	void EditorLayer::UI_DrawMenubar()
	{
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Open Project...", "Ctrl+O")) OpenProject();
				ImGui::Separator();
				if (ImGui::MenuItem("New Scene", "Ctrl+N")) NewScene();
				if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveScene();
				if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) SaveSceneAs();
				ImGui::Separator();
				if (ImGui::MenuItem("Exit")) Application::Get().Close();
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Script"))
			{
				if (ImGui::MenuItem("Reload assembly", "Ctrl+R")) ScriptEngine::ReloadAssembly();
				ImGui::EndMenu();
			}

			// Add a custom drag zone to the remaining space of the MenuBar so we can drag the window!
			float availWidth = ImGui::GetContentRegionAvail().x;
			if (availWidth > 100.0f)
			{
				ImGui::InvisibleButton("##titleBarDragZone", ImVec2(availWidth - 100.0f, ImGui::GetFrameHeight()));
				m_TitleBarHovered = ImGui::IsItemHovered();

				if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
				{
					auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
					ImVec2 point = ImGui::GetMousePos();

					// Simple window dragging logic
					int x, y;
					glfwGetWindowPos(window, &x, &y);
					glfwSetWindowPos(window, x + (int)ImGui::GetIO().MouseDelta.x, y + (int)ImGui::GetIO().MouseDelta.y);
				}

				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_TitleBarHovered)
				{
					auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
					bool maximized = (bool)glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
					if (maximized) glfwRestoreWindow(window);
					else glfwMaximizeWindow(window);
				}
			}

			ImGui::EndMenuBar();
		}
	}

	float EditorLayer::UI_DrawTitlebar()
	{
		// Since we integrated the drag zone into the MenuBar above, we don't need to draw 
		// a massive rectangle that covers everything. We just return 0 to prevent offsetting.
		return 0.0f;
	}

	void EditorLayer::UI_HandleManualWindowResize()
	{
		auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		const bool maximized = (bool)glfwGetWindowAttrib(window, GLFW_MAXIMIZED);

		// A helper to let ImGui drag the right/bottom borders if the OS border is stripped
		// Ensure UI::UpdateWindowManualResize logic exists in ImGuiUtilities.h, else comment out.
	}

	bool EditorLayer::UI_TitleBarHitTest(int x, int y) const
	{
		return m_TitleBarHovered;
	}

	void EditorLayer::OnImGuiRender()
	{
		LUX_PROFILE_FUNCTION("EditorLayer::OnImGuiRender");

		ImGuizmo::BeginFrame();

		static bool dockspaceOpen = true;
		static bool opt_fullscreen_persistent = true;
		bool opt_fullscreen = opt_fullscreen_persistent;
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

        GLFWwindow* nativeWindow = Application::Get().GetWindow().GetNativeWindow();
		const bool isWindowMaximized = nativeWindow && glfwGetWindowAttrib(nativeWindow, GLFW_MAXIMIZED);

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		// FIX: Added ImGuiWindowFlags_MenuBar back to the window flags!
		
		if (opt_fullscreen)
		{
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->Pos);
			ImGui::SetNextWindowSize(viewport->Size);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
				ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}

		if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
			window_flags |= ImGuiWindowFlags_NoBackground;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const bool dockspaceVisible = ImGui::Begin("Lux Editor", &dockspaceOpen, window_flags);
		ImGui::PopStyleVar();

		if (opt_fullscreen)
			ImGui::PopStyleVar(2);

		if (dockspaceVisible)
		{
			// Render the MenuBar (which now contains our custom drag zone)
			UI_DrawMenubar();

			ImGuiIO& io = ImGui::GetIO();
			ImGuiStyle& style = ImGui::GetStyle();
			const float minWinSizeX = style.WindowMinSize.x;
			style.WindowMinSize.x = 370.0f;
			if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
			{
				const ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
				ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
			}
			style.WindowMinSize.x = minWinSizeX;

			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("File"))
				{
					if (ImGui::MenuItem("Open Project...", "Ctrl+O"))
						OpenProject();

					ImGui::Separator();

					if (ImGui::MenuItem("New Scene", "Ctrl+N"))
						NewScene();

					if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
						SaveScene();

					if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
						SaveSceneAs();

					ImGui::Separator();

					if (ImGui::MenuItem("Exit"))
						Application::Get().Close();

					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Script"))
				{
					if (ImGui::MenuItem("Reload assembly", "Ctrl+R"))
						ScriptEngine::ReloadAssembly();

					ImGui::EndMenu();
				}

				ImGui::EndMenuBar();
			}

			m_PanelManager->OnImGuiRender();

			// Stats panel
			ImGui::Begin("Stats");
			auto stats = m_Renderer2D->GetDrawStats();
			ImGui::Text("Renderer2D Stats:");
			ImGui::Text("Draw Calls: %d", stats.DrawCalls);
			ImGui::Text("Quads: %d", stats.QuadCount);
			ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
			ImGui::Text("Indices: %d", stats.GetTotalIndexCount());
			ImGui::Separator();
			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
			if (ImGui::Checkbox("VSync", &m_VSync)) Application::Get().GetWindow().SetVSync(m_VSync);
			if (ImGui::Checkbox("Show Physics Colliders", &m_ShowPhysicsColliders))
			{
				if (s_SceneRendererState.Renderer) s_SceneRendererState.Renderer->GetOptions().ShowPhysicsColliders = m_ShowPhysicsColliders;
			}
			ImGui::Separator();
			ImGui::Checkbox("Show Font Atlas", &s_ShowFontAtlasInStats);
			if (s_ShowFontAtlasInStats)
				ImGui::Image(GetImGuiTextureID(s_Font->GetFontAtlas()), { 512, 512 }, { 0, 1 }, { 1, 0 });
			ImGui::End();

			// --- Viewport Panel ---
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
			ImGui::Begin("Viewport");

			auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
			auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
			auto viewportOffset = ImGui::GetWindowPos();
			m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
			m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };

			m_ViewportFocused = ImGui::IsWindowFocused();
			m_ViewportHovered = ImGui::IsWindowHovered();
			Application::Get().GetImGuiLayer()->AllowInputEvents(m_ViewportFocused || m_ViewportHovered);

			ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
			if (viewportPanelSize.x > 1.0f && viewportPanelSize.y > 1.0f)
				m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

			ImTextureID texID = GetImGuiTextureID(m_Framebuffer->GetImage(0));
			ImGui::Image(texID, ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, ImVec2{ 0, 0 }, ImVec2{ 1, 1 });

			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
				{
					AssetHandle handle = *(AssetHandle*)payload->Data;
					const AssetType type = AssetManager::GetAssetType(handle);
					if (type == AssetType::Scene) OpenScene(handle);
					else if (type == AssetType::Prefab && m_SceneState == SceneState::Edit)
					{
						Ref<Prefab> prefab = AssetManager::GetAsset<Prefab>(handle);
						if (prefab)
						{
							Entity instantiated = m_EditorScene->InstantiatePrefab(prefab);
							if (m_SceneHierarchyPanel) m_SceneHierarchyPanel->SetSelectedEntity(instantiated);
						}
					}
				}
				ImGui::EndDragDropTarget();
			}

			UI_GizmosToolbar();
			UI_CentralToolbar();

			// --- Gizmos ---
			Entity selectedEntity = {};
			if (m_SceneHierarchyPanel) selectedEntity = m_SceneHierarchyPanel->GetSelectedEntity();
			if (selectedEntity && m_GizmoType != -1 && m_SceneState == SceneState::Edit)
			{
				ImGuizmo::SetOrthographic(false);
				ImGuizmo::SetDrawlist();
				ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
					m_ViewportBounds[1].x - m_ViewportBounds[0].x,
					m_ViewportBounds[1].y - m_ViewportBounds[0].y);

				const glm::mat4& cameraProjection = m_EditorCamera.GetProjectionMatrix();
				glm::mat4 cameraView = m_EditorCamera.GetViewMatrix();

				auto& tc = selectedEntity.GetComponent<TransformComponent>();
				glm::mat4 transform = tc.GetTransform();

				bool snap = Input::IsKeyPressed(Key::LeftControl);
				float snapValue = (m_GizmoType == ImGuizmo::OPERATION::ROTATE) ? 45.0f : 0.5f;
				float snapValues[3] = { snapValue, snapValue, snapValue };

				ImGuizmo::Manipulate(
					glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
					(ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL, glm::value_ptr(transform),
					nullptr, snap ? snapValues : nullptr);

				if (ImGuizmo::IsUsing())
				{
					glm::vec3 translation, scale;
					glm::quat rotationQuat;
					Math::DecomposeTransform(transform, translation, rotationQuat, scale);
					glm::vec3 rotationEuler = glm::eulerAngles(rotationQuat);
					glm::vec3 deltaRotation = rotationEuler - tc.Rotation;
					tc.Translation = translation;
					tc.Rotation += deltaRotation;
					tc.Scale = scale;
				}
			}

			ImGui::End(); // Viewport
			ImGui::PopStyleVar();
		}

		ImGui::End(); // Lux Editor
	}

	void EditorLayer::UI_Toolbar()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

		const float edgeOffset = 4.0f;
		const float windowHeight = 32.0f;

		ImGui::SetNextWindowPos(ImVec2(m_ViewportBounds[0].x + 14, m_ViewportBounds[0].y + edgeOffset));
		ImGui::SetNextWindowSize(ImVec2(100.0f, windowHeight));
		ImGui::SetNextWindowBgAlpha(0.0f);
		ImGui::Begin("##viewport_tools", 0, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking);

		if (ImGui::Button("S")) m_GizmoType = -1;
		ImGui::SameLine();
		if (ImGui::Button("T")) m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
		ImGui::SameLine();
		if (ImGui::Button("R")) m_GizmoType = ImGuizmo::OPERATION::ROTATE;
		ImGui::SameLine();
		if (ImGui::Button("E")) m_GizmoType = ImGuizmo::OPERATION::SCALE;

		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);
	}

	void EditorLayer::UI_CentralToolbar()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

		float toolbarX = (m_ViewportBounds[0].x + m_ViewportBounds[1].x) / 2.0f;
		const float edgeOffset = 4.0f;
		const float windowHeight = 32.0f;
		const float backgroundWidth = 120.0f;

		ImGui::SetNextWindowPos(ImVec2(toolbarX - (backgroundWidth / 2.0f), m_ViewportBounds[0].y + edgeOffset));
		ImGui::SetNextWindowSize(ImVec2(backgroundWidth, windowHeight));
		ImGui::SetNextWindowBgAlpha(0.0f);
		ImGui::Begin("##viewport_central_toolbar", 0, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking);

		float size = 24.0f;
		bool hasPlayButton = m_SceneState == SceneState::Edit || m_SceneState == SceneState::Play;
		bool hasSimulateButton = m_SceneState == SceneState::Edit || m_SceneState == SceneState::Simulate;
		bool hasPauseButton = m_SceneState != SceneState::Edit;
		ImVec4 tintColor = ImVec4(1, 1, 1, 1);

		if (hasPlayButton)
		{
			Ref<Texture2D> icon = (m_SceneState == SceneState::Edit || m_SceneState == SceneState::Simulate) ? m_IconPlay : m_IconStop;
			if (ImGui::ImageButton("##play", GetImGuiTextureID(icon), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tintColor))
			{
				if (m_SceneState == SceneState::Edit || m_SceneState == SceneState::Simulate)
					OnScenePlay();
				else if (m_SceneState == SceneState::Play)
					OnSceneStop();
			}
		}

		if (hasSimulateButton)
		{
			if (hasPlayButton)
				ImGui::SameLine();

			Ref<Texture2D> icon = (m_SceneState == SceneState::Edit || m_SceneState == SceneState::Play) ? m_IconSimulate : m_IconStop;
			if (ImGui::ImageButton("##simulate", GetImGuiTextureID(icon), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tintColor))
			{
				if (m_SceneState == SceneState::Edit || m_SceneState == SceneState::Play)
					OnSceneSimulate();
				else if (m_SceneState == SceneState::Simulate)
					OnSceneStop();
			}
		}

		if (hasPauseButton)
		{
			bool isPaused = m_ActiveScene->IsPaused();
			ImGui::SameLine();
			if (ImGui::ImageButton("##pause", GetImGuiTextureID(m_IconPause), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tintColor))
			{
				m_ActiveScene->SetPaused(!isPaused);
			}

			if (isPaused)
			{
				ImGui::SameLine();
				if (ImGui::ImageButton("##step", GetImGuiTextureID(m_IconStep), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tintColor))
					m_ActiveScene->Step();
			}
		}

		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);
	}

	void EditorLayer::OnEvent(Event& e)
	{
		if (m_SceneState == SceneState::Edit || m_SceneState == SceneState::Simulate)
			m_EditorCamera.OnEvent(e);

		m_PanelManager->OnEvent(e);

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<KeyPressedEvent>(LUX_BIND_EVENT_FN(EditorLayer::OnKeyPressed));
		dispatcher.Dispatch<MouseButtonPressedEvent>(LUX_BIND_EVENT_FN(EditorLayer::OnMouseButtonPressed));
		dispatcher.Dispatch<WindowTitleBarHitTestEvent>(LUX_BIND_EVENT_FN(EditorLayer::OnTitleBarHitTest));
	}

	bool EditorLayer::OnKeyPressed(KeyPressedEvent& e)
	{
		bool control = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
		bool shift = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);

		switch (e.GetKeyCode())
		{
		case Key::N: if (control) NewScene();    break;
		case Key::O: if (control) OpenProject(); break;
		case Key::S:
			if (control)
			{
				if (shift) SaveSceneAs();
				else       SaveScene();
			}
			break;
		case Key::D: if (control) OnDuplicateEntity(); break;

		case Key::Q: if (!ImGuizmo::IsUsing()) m_GizmoType = -1;                            break;
		case Key::W: if (!ImGuizmo::IsUsing()) m_GizmoType = ImGuizmo::OPERATION::TRANSLATE; break;
		case Key::E: if (!ImGuizmo::IsUsing()) m_GizmoType = ImGuizmo::OPERATION::ROTATE;   break;
		case Key::R:
			if (control) ScriptEngine::ReloadAssembly();
			else if (!ImGuizmo::IsUsing()) m_GizmoType = ImGuizmo::OPERATION::SCALE;
			break;

		default: break;
		}

		return false;
	}

	bool EditorLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
	{
		if (e.GetMouseButton() == MouseButton::Left)
		{
			if (m_ViewportHovered && !ImGuizmo::IsOver() && !Input::IsKeyPressed(Key::LeftAlt))
				if (m_SceneHierarchyPanel)
					m_SceneHierarchyPanel->SetSelectedEntity(m_HoveredEntity);
		}
		return false;
	}

	bool EditorLayer::OnTitleBarHitTest(WindowTitleBarHitTestEvent& e)
	{
		const float x = (float)e.GetX();
		const float y = (float)e.GetY();

		const bool inDragZone = x >= m_TitleBarDragRectMin.x && x <= m_TitleBarDragRectMax.x
			&& y >= m_TitleBarDragRectMin.y && y <= m_TitleBarDragRectMax.y;

		if (inDragZone)
			e.SetHit(true);

		return inDragZone;
	}

	void EditorLayer::OnOverlayRender()
	{
		if (m_SceneState == SceneState::Play)
		{
			Entity camera = m_ActiveScene->GetPrimaryCameraEntity();
			if (!camera)
				return;
			const auto& cam = camera.GetComponent<CameraComponent>().Camera;
			const glm::mat4 cameraTransform = camera.GetComponent<TransformComponent>().GetTransform();
			glm::mat4 viewMatrix = glm::inverse(cameraTransform);
			m_Renderer2D->BeginScene(cam.GetProjectionMatrix() * viewMatrix, viewMatrix);
		}
		else
		{
			m_Renderer2D->BeginScene(m_EditorCamera.GetViewProjection(), m_EditorCamera.GetViewMatrix());
		}

		if (m_ShowPhysicsColliders)
		{
			// Box Colliders
			{
				auto view = m_ActiveScene->GetAllEntitiesWith<TransformComponent, BoxCollider2DComponent>();
				for (auto entity : view)
				{
					auto [tc, bc2d] = view.get<TransformComponent, BoxCollider2DComponent>(entity);

					glm::vec3 scale = tc.Scale * glm::vec3(bc2d.Size * 2.0f, 1.0f);

					glm::mat4 transform = glm::translate(glm::mat4(1.0f), tc.Translation)
						* glm::rotate(glm::mat4(1.0f), tc.Rotation.z, glm::vec3(0.0f, 0.0f, 1.0f))
						* glm::translate(glm::mat4(1.0f), glm::vec3(bc2d.Offset, 0.001f))
						* glm::scale(glm::mat4(1.0f), scale);

					glm::vec4 color(0, 1, 0, 1);
					glm::vec4 corners[4] = {
						{-0.5f, -0.5f, 0.0f, 1.0f}, { 0.5f, -0.5f, 0.0f, 1.0f},
						{ 0.5f,  0.5f, 0.0f, 1.0f}, {-0.5f,  0.5f, 0.0f, 1.0f}
					};
					for (int i = 0; i < 4; i++)
					{
						glm::vec3 p0 = transform * corners[i];
						glm::vec3 p1 = transform * corners[(i + 1) % 4];
						m_Renderer2D->DrawLine(p0, p1, color);
					}
				}
			}

			// Circle Colliders
			{
				auto view = m_ActiveScene->GetAllEntitiesWith<TransformComponent, CircleCollider2DComponent>();
				for (auto entity : view)
				{
					auto [tc, cc2d] = view.get<TransformComponent, CircleCollider2DComponent>(entity);

					glm::vec3 translation = tc.Translation + glm::vec3(cc2d.Offset, 0.001f);
					glm::vec3 scale = tc.Scale * glm::vec3(cc2d.Radius * 2.0f);

					glm::mat4 transform = glm::translate(glm::mat4(1.0f), translation)
						* glm::scale(glm::mat4(1.0f), scale);

					m_Renderer2D->DrawCircle(transform, glm::vec4(0, 1, 0, 1));
				}
			}

			// Selected entity outline
			Entity selectedEntity = {};
			if (m_SceneHierarchyPanel)
				selectedEntity = m_SceneHierarchyPanel->GetSelectedEntity();
			if (selectedEntity)
			{
				const TransformComponent& transform = selectedEntity.GetComponent<TransformComponent>();
				glm::mat4 t = transform.GetTransform();
				glm::vec4 color(1.0f, 0.5f, 0.0f, 1.0f);
				glm::vec4 corners[4] = {
					{-0.5f, -0.5f, 0.0f, 1.0f}, { 0.5f, -0.5f, 0.0f, 1.0f},
					{ 0.5f,  0.5f, 0.0f, 1.0f}, {-0.5f,  0.5f, 0.0f, 1.0f}
				};
				for (int i = 0; i < 4; i++)
				{
					glm::vec3 p0 = t * corners[i];
					glm::vec3 p1 = t * corners[(i + 1) % 4];
					m_Renderer2D->DrawLine(p0, p1, color);
				}
			}
		}

		m_Renderer2D->EndScene();
	}

	void EditorLayer::NewProject()
	{
		Project::New();
	}

	void EditorLayer::OpenProject(const std::filesystem::path& path)
	{
		if (Project::Load(path))
		{
			AssetHandle startScene = Project::GetActive()->GetConfig().StartScene;
			if (startScene)
				OpenScene(startScene);
			m_PanelManager->OnProjectChanged(Project::GetActive());
		}
	}

	bool EditorLayer::OpenProject()
	{
		std::string filepath = FileDialogs::OpenFile("Lux Project (*.luxproj)\0*.luxproj\0");
		if (filepath.empty())
			return false;

		OpenProject(filepath);
		return true;
	}

	void EditorLayer::SaveProject()
	{
		// Project::SaveActive();
	}

	void EditorLayer::NewScene()
	{
		m_EditorScene = CreateRef<Scene>();
		m_EditorScene->SetTargetFramebuffer(m_Framebuffer);

		if (m_ViewportSize.x > 1.0f && m_ViewportSize.y > 1.0f)
			m_EditorScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

		m_ActiveScene = m_EditorScene;
		m_PanelManager->SetSceneContext(m_ActiveScene);
		m_EditorScenePath = std::filesystem::path();

		if (m_SceneRenderer)
			m_SceneRenderer->SetScene(m_ActiveScene);

		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetContext(m_SceneRenderer);

		EnsureSceneRenderer(m_ActiveScene, m_ViewportSize);
	}

	void EditorLayer::OpenScene()
	{
	}

	void EditorLayer::OpenScene(AssetHandle handle)
	{
		LUX_CORE_ASSERT(handle);

		if (m_SceneState != SceneState::Edit)
			OnSceneStop();

		Ref<Scene> readOnlyScene = AssetManager::GetAsset<Scene>(handle);
		Ref<Scene> newScene = Scene::Copy(readOnlyScene);

		m_EditorScene = newScene;
		m_EditorScene->SetTargetFramebuffer(m_Framebuffer);

		if (m_ViewportSize.x > 1.0f && m_ViewportSize.y > 1.0f)
			m_EditorScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

		m_ActiveScene = m_EditorScene;
		m_PanelManager->SetSceneContext(m_EditorScene);
		m_EditorScenePath = Project::GetActive()->GetEditorAssetManager()->GetFilePath(handle);

		if (m_SceneRenderer)
			m_SceneRenderer->SetScene(m_ActiveScene);

		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetContext(m_SceneRenderer);

		EnsureSceneRenderer(m_ActiveScene, m_ViewportSize);
	}

	void EditorLayer::SaveScene()
	{
		if (!m_EditorScenePath.empty())
			SerializeScene(m_ActiveScene, m_EditorScenePath);
		else
			SaveSceneAs();
	}

	void EditorLayer::SaveSceneAs()
	{
		std::string filepath = FileDialogs::SaveFile("Lux Scene (*.luxscene)\0*.luxscene\0");
		if (!filepath.empty())
		{
			SerializeScene(m_ActiveScene, filepath);
			m_EditorScenePath = filepath;
		}
	}

	void EditorLayer::SerializeScene(Ref<Scene> scene, const std::filesystem::path& path)
	{
		SceneImporter::SaveScene(scene, path);
	}

	void EditorLayer::OnScenePlay()
	{
		if (m_SceneState == SceneState::Simulate)
			OnSceneStop();

		m_SceneState = SceneState::Play;

		m_ActiveScene = Scene::Copy(m_EditorScene);
		m_ActiveScene->SetTargetFramebuffer(m_Framebuffer);

		if (m_ViewportSize.x > 1.0f && m_ViewportSize.y > 1.0f)
			m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

		m_ActiveScene->OnRuntimeStart();
		m_PanelManager->SetSceneContext(m_ActiveScene);

		if (m_SceneRenderer)
			m_SceneRenderer->SetScene(m_ActiveScene);

		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetContext(m_SceneRenderer);

		EnsureSceneRenderer(m_ActiveScene, m_ViewportSize);
	}

	void EditorLayer::OnSceneSimulate()
	{
		if (m_SceneState == SceneState::Play)
			OnSceneStop();

		m_SceneState = SceneState::Simulate;

		m_ActiveScene = Scene::Copy(m_EditorScene);
		m_ActiveScene->SetTargetFramebuffer(m_Framebuffer);

		if (m_ViewportSize.x > 1.0f && m_ViewportSize.y > 1.0f)
			m_ActiveScene->OnViewportResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

		m_ActiveScene->OnSimulationStart();
		m_PanelManager->SetSceneContext(m_ActiveScene);

		if (m_SceneRenderer)
			m_SceneRenderer->SetScene(m_ActiveScene);

		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetContext(m_SceneRenderer);

		EnsureSceneRenderer(m_ActiveScene, m_ViewportSize);
	}

	void EditorLayer::OnSceneStop()
	{
		LUX_CORE_ASSERT(m_SceneState == SceneState::Play || m_SceneState == SceneState::Simulate);

		if (m_SceneState == SceneState::Play)
			m_ActiveScene->OnRuntimeStop();
		else if (m_SceneState == SceneState::Simulate)
			m_ActiveScene->OnSimulationStop();

		m_SceneState = SceneState::Edit;
		m_ActiveScene = m_EditorScene;

		m_PanelManager->SetSceneContext(m_ActiveScene);

		if (m_SceneRenderer)
			m_SceneRenderer->SetScene(m_ActiveScene);

		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetContext(m_SceneRenderer);

		EnsureSceneRenderer(m_ActiveScene, m_ViewportSize);
	}

	void EditorLayer::OnScenePause()
	{
		if (m_SceneState == SceneState::Edit)
			return;

		m_ActiveScene->SetPaused(true);
	}

	void EditorLayer::OnDuplicateEntity()
	{
		if (m_SceneState != SceneState::Edit)
			return;

		Entity selectedEntity = {};
		if (m_SceneHierarchyPanel)
			selectedEntity = m_SceneHierarchyPanel->GetSelectedEntity();
		if (selectedEntity)
		{
			Entity newEntity = m_EditorScene->DuplicateEntity(selectedEntity);
			if (m_SceneHierarchyPanel)
				m_SceneHierarchyPanel->SetSelectedEntity(newEntity);
		}
	}
}
