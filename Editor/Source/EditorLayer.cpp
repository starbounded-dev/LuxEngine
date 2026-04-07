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

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui/imgui.h>
#include "imgui/imgui_internal.h"
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

	EditorLayer::EditorLayer()
		: Layer("EditorLayer"), m_EditorCamera(60.0f, 1600.0f, 900.0f, 0.1f, 10000.0f) ,m_SquareColor({ 0.2f, 0.3f, 0.8f, 1.0f })
	{
		s_Font = Font::GetDefaultFont();
	}

	void EditorLayer::OnAttach()
	{
		LUX_PROFILE_FUNCTION("EditorLayer::OnAttach");

		/////////// Configure Panels ///////////
		m_PanelManager = CreateScope<PanelManager>();

		m_PanelManager->AddPanel<TextEditorPanel>(PanelCategory::View,"TextEditorPanel","Text Editor",true);

		Ref<SceneRendererPanel> sceneRendererPanel = m_PanelManager->AddPanel<SceneRendererPanel>(PanelCategory::View, SCENE_RENDERER_PANEL_ID, "Scene Renderer", true);
		
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

		// Render statistics panel
		m_PanelManager->AddPanel<RenderStatsPanel>(PanelCategory::View, "RenderStatsPanel", "Render Stats", true);

		m_IconPlay = TextureImporter::LoadTexture2D("Resources/Editor/Viewport/Play.png");
		m_IconPause = TextureImporter::LoadTexture2D("Resources/Editor/Viewport/Pause.png");
		m_IconSimulate = TextureImporter::LoadTexture2D("Resources/Editor/Viewport/Simulate.png");
		m_IconStep = TextureImporter::LoadTexture2D("Resources/Editor/Viewport/Step.png");
		m_IconStop = TextureImporter::LoadTexture2D("Resources/Editor/Viewport/Stop.png");

		m_Renderer2D = Ref<Renderer2D>::Create();
		m_Renderer2D->SetLineWidth(4.0f);

		FramebufferSpecification fbSpec;
		// Attachment 0: RGBA colour output shown in viewport
		// Attachment 1: Depth
		// NOTE: Entity ID picking (RED_INTEGER attachment + ReadPixel) is not yet
		// supported by Framebuffer in this engine. m_HoveredEntity is cleared every
		// frame until that infrastructure is added.
		fbSpec.Attachments = { ImageFormat::RGBA32F, ImageFormat::Depth };
		fbSpec.Width = 1280;
		fbSpec.Height = 720;
		fbSpec.ClearColorOnLoad = true;
		fbSpec.DebugName = "EditorFramebuffer";
		m_Framebuffer = Framebuffer::Create(fbSpec);

		// Now safe to call - m_Renderer2D is valid.
		m_Renderer2D->SetTargetFramebuffer(m_Framebuffer);

		m_PanelManager->SetSceneContext(m_EditorScene);

		EnsureSceneRenderer(m_ActiveScene, m_ViewportSize);
		sceneRendererPanel->SetContext(s_SceneRendererState.Renderer);

		m_EditorCamera.SetActive(true);

		m_EditorScene = Ref<Scene>::Create();
		m_EditorScene->SetTargetFramebuffer(m_Framebuffer);
		m_ActiveScene = m_EditorScene;

		SceneRendererSpecification sceneRendererSpec;
		sceneRendererSpec.ViewportWidth = 1280;
		sceneRendererSpec.ViewportHeight = 720;

		m_SceneRenderer = Ref<SceneRenderer>::Create(m_ActiveScene, sceneRendererSpec);
		m_SceneRendererPanel.SetContext(m_SceneRenderer);

		// FIX: SetContext was never called in OnAttach, so the SceneHierarchyPanel
		// had a null context on startup. Entities would not appear in the hierarchy
		// and the Properties panel would never draw any components.
		m_SceneHierarchyPanel.SetContext(m_EditorScene);

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
		m_ContentBrowserPanel.reset();
		s_Font.reset();
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

		// Create selection predicate for 3D rendering (highlights selected entity)
		Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
		auto isEntitySelected = [selectedEntity](Entity entity) -> bool {
			return selectedEntity && entity == selectedEntity;
		};

		switch (m_SceneState)
		{
		case SceneState::Edit:
		{
			m_EditorCamera.OnUpdate(ts);
			
			// Render 3D content first (static meshes, lights, skybox)
			if (m_SceneRenderer && m_SceneRenderer->IsReady())
				m_ActiveScene->Render3D(m_EditorCamera, m_SceneRenderer, isEntitySelected);
			
			// Then render 2D content (sprites, circles, text) - this blends on top
			m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);
			break;
		}
		case SceneState::Simulate:
		{
			m_EditorCamera.OnUpdate(ts);
			
			// Render 3D content
			if (m_SceneRenderer && m_SceneRenderer->IsReady())
				m_ActiveScene->Render3D(m_EditorCamera, m_SceneRenderer, isEntitySelected);
			
			// Then render 2D content and update physics simulation
			m_ActiveScene->OnUpdateSimulation(ts, m_EditorCamera);
			break;
		}
		case SceneState::Play:
		{
			// Render 3D content using the runtime camera
			if (m_SceneRenderer && m_SceneRenderer->IsReady())
				m_ActiveScene->Render3DRuntime(m_SceneRenderer);
			
			// Then run the full runtime update (scripts, physics, 2D rendering)
			m_ActiveScene->OnUpdateRuntime(ts);
			break;
		}
		}

		OnOverlayRender();
	}

	void EditorLayer::OnImGuiRender()
	{
		LUX_PROFILE_FUNCTION("EditorLayer::OnImGuiRender");

		// Must be called once per frame before any other ImGuizmo function.
		// Without this, IsOver() / IsUsing() return stale state and Manipulate()
		// produces garbage transforms.
		ImGuizmo::BeginFrame();

		static bool dockspaceOpen = true;
		static bool opt_fullscreen_persistant = true;
		bool opt_fullscreen = opt_fullscreen_persistant;
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
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
		ImGui::Begin("DockSpace Demo", &dockspaceOpen, window_flags);
		ImGui::PopStyleVar();

		if (opt_fullscreen)
			ImGui::PopStyleVar(2);

		ImGuiIO& io = ImGui::GetIO();
		ImGuiStyle& style = ImGui::GetStyle();
		float minWinSizeX = style.WindowMinSize.x;
		style.WindowMinSize.x = 370.0f;
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
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

		m_SceneHierarchyPanel.OnImGuiRender();

		// FIX 3: Guard ContentBrowserPanel - it is only created after a project
		// is successfully opened via OpenProject(). Calling it unconditionally
		// when no project is loaded causes a null pointer dereference crash.
		if (m_ContentBrowserPanel)
			m_ContentBrowserPanel->OnImGuiRender();

		m_PanelManager->OnImGuiRender();

		// Stats panel
		ImGui::Begin("Stats");

#if 0
		std::string name = "None";
		if (m_HoveredEntity)
			name = m_HoveredEntity.GetComponent<TagComponent>().Tag;
		ImGui::Text("Hovered Entity: %s", name.c_str());
#endif

		auto stats = m_Renderer2D->GetDrawStats();
		ImGui::Text("Renderer2D Stats:");
		ImGui::Text("Draw Calls: %d", stats.DrawCalls);
		ImGui::Text("Quads: %d", stats.QuadCount);
		ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
		ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

		ImGui::Separator();

		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

		if (ImGui::Checkbox("VSync", &m_VSync))
			Application::Get().GetWindow().SetVSync(m_VSync);

		if (ImGui::Checkbox("Show Physics Colliders", &m_ShowPhysicsColliders))
		{
			if (s_SceneRendererState.Renderer)
				s_SceneRendererState.Renderer->GetOptions().ShowPhysicsColliders = m_ShowPhysicsColliders;
		}

		ImGui::Separator();

		ImGui::Image(GetImGuiTextureID(s_Font->GetFontAtlas()), { 512, 512 }, { 0, 1 }, { 1, 0 });

		ImGui::End(); // Stats
		  
		// Viewport panel
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

		// Guard against zero-size during panel drag/dock transitions; a zero
		// size would propagate to Framebuffer::Resize(0,0) next frame and
		// destroy all attachment images while they are still in use.
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
				OpenScene(handle);
			}
			ImGui::EndDragDropTarget();
		}

		// Gizmos
		Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
		if (selectedEntity && m_GizmoType != -1)
		{
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist();
			ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
				m_ViewportBounds[1].x - m_ViewportBounds[0].x,
				m_ViewportBounds[1].y - m_ViewportBounds[0].y);

			// FIX: Use GetProjectionMatrix() (projection only), not
			// GetViewProjection() (combined VP). ImGuizmo needs them separate.
			const glm::mat4& cameraProjection = m_EditorCamera.GetProjectionMatrix();
			glm::mat4 cameraView = m_EditorCamera.GetViewMatrix();

			auto& tc = selectedEntity.GetComponent<TransformComponent>();
			glm::mat4 transform = tc.GetTransform();

			bool snap = Input::IsKeyPressed(Key::LeftControl);
			float snapValue = (m_GizmoType == ImGuizmo::OPERATION::ROTATE) ? 45.0f : 0.5f;
			float snapValues[3] = { snapValue, snapValue, snapValue };

			// FIX: Actually call Manipulate() - it was completely missing,
			// so gizmos were never drawn or interactable at all.
			ImGuizmo::Manipulate(
				glm::value_ptr(cameraView),
				glm::value_ptr(cameraProjection),
				(ImGuizmo::OPERATION)m_GizmoType,
				ImGuizmo::LOCAL,
				glm::value_ptr(transform),
				nullptr,
				snap ? snapValues : nullptr);

			// FIX: DecomposeTransform signature is (mat4, vec3&, quat&, vec3&).
			// Decompose into a quaternion then convert to euler angles for storage.
			if (ImGuizmo::IsUsing())
			{
				glm::vec3 translation, scale;
				glm::quat rotationQuat;
				Math::DecomposeTransform(transform, translation, rotationQuat, scale);

				// Convert quat to euler and apply as a delta to avoid gimbal lock
				// accumulation that would occur from direct euler assignment.
				glm::vec3 rotationEuler = glm::eulerAngles(rotationQuat);
				glm::vec3 deltaRotation = rotationEuler - tc.Rotation;
				tc.Translation = translation;
				tc.Rotation += deltaRotation;
				tc.Scale = scale;
			}
		}

		ImGui::End(); // Viewport
		ImGui::PopStyleVar();

		UI_Toolbar();

		ImGui::End(); // DockSpace Demo
	}

	void EditorLayer::UI_Toolbar()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		auto& colors = ImGui::GetStyle().Colors;
		const auto& buttonHovered = colors[ImGuiCol_ButtonHovered];
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonHovered.x, buttonHovered.y, buttonHovered.z, 0.5f));
		const auto& buttonActive = colors[ImGuiCol_ButtonActive];
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(buttonActive.x, buttonActive.y, buttonActive.z, 0.5f));

		ImGui::Begin("##toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		bool toolbarEnabled = (bool)m_ActiveScene;

		ImVec4 tintColor = ImVec4(1, 1, 1, 1);
		if (!toolbarEnabled)
			tintColor.w = 0.5f;

		float size = ImGui::GetWindowHeight() - 4.0f;
		ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x * 0.5f) - (size * 0.5f));

		bool hasPlayButton = m_SceneState == SceneState::Edit || m_SceneState == SceneState::Play;
		bool hasSimulateButton = m_SceneState == SceneState::Edit || m_SceneState == SceneState::Simulate;
		bool hasPauseButton = m_SceneState != SceneState::Edit;

		if (hasPlayButton)
		{
			Ref<Texture2D> icon = (m_SceneState == SceneState::Edit || m_SceneState == SceneState::Simulate) ? m_IconPlay : m_IconStop;
			if (ImGui::ImageButton("##play", GetImGuiTextureID(icon), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0,0,0,0), tintColor))
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
			if (ImGui::ImageButton("##simulate", GetImGuiTextureID(icon), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0,0,0,0), tintColor))
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
			{
				if (ImGui::ImageButton("##pause", GetImGuiTextureID(m_IconPause), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0,0,0,0), tintColor) && toolbarEnabled)
					m_ActiveScene->SetPaused(!isPaused);
			}

			if (isPaused)
			{
				ImGui::SameLine();
				if (ImGui::ImageButton("##step", GetImGuiTextureID(m_IconStep), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0,0,0,0), tintColor) && toolbarEnabled)
					m_ActiveScene->Step();
			}
		}

		ImGui::SameLine();
		ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
		ImGui::SameLine();

		// Gizmo mode indicator
		const char* gizmoMode = "None";
		if (m_GizmoType == ImGuizmo::TRANSLATE) gizmoMode = "Translate";
		else if (m_GizmoType == ImGuizmo::ROTATE) gizmoMode = "Rotate";
		else if (m_GizmoType == ImGuizmo::SCALE) gizmoMode = "Scale";

		ImGui::Text("%s", gizmoMode);
		ImGui::SameLine();

		// Grid toggle
		if (s_SceneRendererState.Renderer)
		{
			bool showGrid = s_SceneRendererState.Renderer->GetOptions().ShowGrid;
			ImGui::SameLine();
			if (ImGui::Checkbox("##grid", &showGrid))
			{
				s_SceneRendererState.Renderer->GetOptions().ShowGrid = showGrid;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Toggle Grid");
		}

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(3);
		ImGui::End();
	}

	void EditorLayer::OnEvent(Event& e)
	{
		if (m_SceneState == SceneState::Edit || m_SceneState == SceneState::Simulate)
			m_EditorCamera.OnEvent(e);

		m_PanelManager->OnEvent(e);

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<KeyPressedEvent>(LUX_BIND_EVENT_FN(EditorLayer::OnKeyPressed));
		dispatcher.Dispatch<MouseButtonPressedEvent>(LUX_BIND_EVENT_FN(EditorLayer::OnMouseButtonPressed));
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
				m_SceneHierarchyPanel.SetSelectedEntity(m_HoveredEntity);
		}
		return false;
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
			if (Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity())
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
			m_ContentBrowserPanel = CreateScope<ContentBrowserPanel>(Project::GetActive());
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
		m_SceneHierarchyPanel.SetContext(m_ActiveScene);
		m_EditorScenePath = std::filesystem::path();

		if (m_SceneRenderer)
			m_SceneRenderer->SetScene(m_ActiveScene);

		auto sceneRendererPanel = m_PanelManager->GetPanel<SceneRendererPanel>("SceneRendererPanel");
		if (sceneRendererPanel)
			sceneRendererPanel->SetContext(m_SceneRenderer);

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
		m_SceneHierarchyPanel.SetContext(m_EditorScene);
		m_EditorScenePath = Project::GetActive()->GetEditorAssetManager()->GetFilePath(handle);

		if (m_SceneRenderer)
			m_SceneRenderer->SetScene(m_ActiveScene);

		auto sceneRendererPanel = m_PanelManager->GetPanel<SceneRendererPanel>("SceneRendererPanel");
		if (sceneRendererPanel)
			sceneRendererPanel->SetContext(m_SceneRenderer);

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
		m_SceneHierarchyPanel.SetContext(m_ActiveScene);

		if (m_SceneRenderer)
			m_SceneRenderer->SetScene(m_ActiveScene);

		auto sceneRendererPanel = m_PanelManager->GetPanel<SceneRendererPanel>("SceneRendererPanel");
		if (sceneRendererPanel)
			sceneRendererPanel->SetContext(m_SceneRenderer);

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
		m_SceneHierarchyPanel.SetContext(m_ActiveScene);

		if (m_SceneRenderer)
			m_SceneRenderer->SetScene(m_ActiveScene);

		auto sceneRendererPanel = m_PanelManager->GetPanel<SceneRendererPanel>("SceneRendererPanel");
		if (sceneRendererPanel)
			sceneRendererPanel->SetContext(m_SceneRenderer);

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

		m_SceneHierarchyPanel.SetContext(m_ActiveScene);

		if (m_SceneRenderer)
			m_SceneRenderer->SetScene(m_ActiveScene);

		auto sceneRendererPanel = m_PanelManager->GetPanel<SceneRendererPanel>("SceneRendererPanel");
		if (sceneRendererPanel)
			sceneRendererPanel->SetContext(m_SceneRenderer);

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

		Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
		if (selectedEntity)
		{
			Entity newEntity = m_EditorScene->DuplicateEntity(selectedEntity);
			m_SceneHierarchyPanel.SetSelectedEntity(newEntity);
		}
	}
}
