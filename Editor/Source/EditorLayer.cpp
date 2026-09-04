#include "EditorLayer.h"
#include "RuntimeExportUtils.h"

#include "Lux/Scene/SceneSerializer.h"
#include "Lux/Editor/EditorStack.h"
#include "Lux/Editor/SelectionManager.h"
#include "Lux/Core/Application.h"
#include "Lux/Scripting/ScriptEngine.h"
#include "Lux/Scripting/ScriptBuilder.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/Renderer/ShaderPack.h"
#include "Lux/Serialization/AssetPack.h"
#include "Lux/Project/ProjectSerializer.h"
#include "Lux/Social/DiscordSocial.h"

#include "Lux/Utilities/FileSystem.h"

#include <cstring>
#include <format>

#include "Lux/Asset/AssetManager.h"
#include "Lux/Core/Math/AABB.h"
#include "Lux/Core/Math/Ray.h"
#include "Lux/Renderer/Mesh.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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
#include "Lux/Utilities/StringUtils.h"
#include "Lux/Math/Math.h"
#include "Panels/TextEditorPanel.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/SceneRendererPanel.h"
#include "Panels/RendererDebuggerPanel.h"
#include "Panels/ProfilerPanel.h"
#include "Panels/UndoHistoryPanel.h"

#include "Lux/Scene/Prefab.h"
#include "Panels/ApplicationSettingsPanel.h"
#include "Panels/AssetManagerPanel.h"
#include "Panels/ProjectSettingsWindow.h"
#include "Lux/Editor/SceneHierarchyPanel.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

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
#define AUDIO_EVENTS_EDITOR_PANEL_ID "AudioEventsEditor"
#define APPLICATION_SETTINGS_PANEL_ID "ApplicationSettingsPanel"
#define SCRIPT_ENGINE_DEBUG_PANEL_ID "ScriptEngineDebugPanel"
#define SCENE_RENDERER_PANEL_ID "SceneRendererPanel"
#define RENDERER_DEBUGGER_PANEL_ID "RendererDebuggerPanel"
#define PHYSICS_CAPTURES_PANEL_ID "PhysicsCapturesPanel"

	namespace {
		constexpr int s_MaxRecentProjects = 10;

		std::string GetRecentProjectKey(size_t index)
		{
			return "RecentProjects." + std::to_string(index);
		}

		std::filesystem::path NormalizeProjectPath(const std::filesystem::path& path)
		{
			if (path.empty())
				return {};

			std::error_code ec;
			std::filesystem::path absolutePath = path.is_absolute() ? path : std::filesystem::absolute(path, ec);
			if (ec)
				return path.lexically_normal();

			return absolutePath.lexically_normal();
		}

		std::vector<std::filesystem::path> LoadLegacyRecentProjects()
		{
			auto& settings = Application::Get().GetSettings();
			const int count = std::max(settings.GetInt("RecentProjects.Count", 0), 0);

			std::vector<std::filesystem::path> projects;
			projects.reserve((size_t)std::min(count, s_MaxRecentProjects));

			for (int i = 0; i < count && (int)projects.size() < s_MaxRecentProjects; i++)
			{
				std::string value = settings.Get(GetRecentProjectKey((size_t)i));
				if (value.empty())
					continue;

				std::filesystem::path normalizedPath = NormalizeProjectPath(value);
				if (normalizedPath.empty())
					continue;

				if (std::find(projects.begin(), projects.end(), normalizedPath) == projects.end())
					projects.emplace_back(std::move(normalizedPath));
			}

			return projects;
		}

		bool ShouldAutoOpenMostRecentProject()
		{
			auto& settings = Application::Get().GetSettings();
			return settings.GetInt("Editor.AutoOpenMostRecentProject", 1) != 0;
		}

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

		using RuntimeExport::RuntimeProjectFile;
		using RuntimeExport::RuntimeAssetPackFile;
		using RuntimeExport::RuntimeShaderPackFile;
		using RuntimeExport::FileExists;
		using RuntimeExport::SanitizeBuildName;
		using RuntimeExport::CopyFileIfExists;
		using RuntimeExport::CopyDirectoryRecursive;
		using RuntimeExport::FindFirstExistingDirectory;
		using RuntimeExport::FindRepositoryRootFrom;
		using RuntimeExport::GetRuntimeOutputDirectory;
		using RuntimeExport::GetRuntimeExecutablePath;
		using RuntimeExport::IsRuntimeExecutableOutdated;
		using RuntimeExport::BuildRuntimeExecutable;
		using RuntimeExport::ResolveScriptProjectFile;
		using RuntimeExport::IsScriptModuleOutdated;
		using RuntimeExport::BuildScriptModule;

		constexpr const char* s_RuntimeProjectFile = RuntimeProjectFile;
		constexpr const char* s_RuntimeAssetPackFile = RuntimeAssetPackFile;
		constexpr const char* s_RuntimeShaderPackFile = RuntimeShaderPackFile;
		constexpr RuntimeExportTarget s_RuntimeExportTargets[] = {
			RuntimeExportTarget::Debug,
			RuntimeExportTarget::Release,
			RuntimeExportTarget::Dist
		};

		bool StartupSceneUsesScripts(Ref<Project> project)
		{
			if (!project || !project->GetConfig().StartSceneHandle)
				return false;

			Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager();
			if (!editorAssetManager)
				return false;

			const AssetMetadata metadata = editorAssetManager->GetMetadata(project->GetConfig().StartSceneHandle);
			if (!metadata.IsValid())
				return false;

			std::ifstream sceneFile(project->GetAssetDirectory() / metadata.FilePath);
			if (!sceneFile.is_open())
				return false;

			std::string line;
			while (std::getline(sceneFile, line))
			{
				if (line.find("ScriptComponent") != std::string::npos)
					return true;
			}

			return false;
		}

		std::filesystem::path ResolveRuntimeIconSource(const ProjectRuntimeExportSettings& settings)
		{
			if (settings.IconHandle)
			{
				if (Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager())
				{
					const AssetMetadata metadata = editorAssetManager->GetMetadata(settings.IconHandle);
					if (metadata.IsValid())
						return editorAssetManager->GetFileSystemPath(metadata);
				}
			}

			if (!settings.IconPath.empty())
				return Project::GetActiveAssetDirectory() / settings.IconPath;

			return {};
		}

		std::string EscapeYamlQuotedString(std::string_view value)
		{
			std::string result;
			result.reserve(value.size());
			for (char c : value)
			{
				if (c == '\\' || c == '"')
					result.push_back('\\');
				result.push_back(c);
			}
			return result;
		}

		bool WriteRuntimeSettingsFile(const std::filesystem::path& filepath, const ProjectRuntimeExportSettings& settings, const std::filesystem::path& runtimeIconPath)
		{
			std::error_code ec;
			std::filesystem::create_directories(filepath.parent_path(), ec);
			if (ec)
			{
				LUX_CONSOLE_LOG_ERROR("Failed to create runtime settings directory '{}': {}", filepath.parent_path().string(), ec.message());
				return false;
			}

			std::ofstream out(filepath);
			if (!out.is_open())
			{
				LUX_CONSOLE_LOG_ERROR("Failed to write runtime settings file '{}'.", filepath.string());
				return false;
			}

			out << "Runtime:\n";
			out << "  GameName: \"" << EscapeYamlQuotedString(settings.GameName) << "\"\n";
			out << "  WindowWidth: " << settings.WindowWidth << "\n";
			out << "  WindowHeight: " << settings.WindowHeight << "\n";
			out << "  Fullscreen: " << (settings.Fullscreen ? "true" : "false") << "\n";
			out << "  VSync: " << (settings.VSync ? "true" : "false") << "\n";
			out << "  IconPath: \"" << EscapeYamlQuotedString(runtimeIconPath.generic_string()) << "\"\n";
			return true;
		}

		bool WriteRuntimeShaderPack(const std::filesystem::path& shaderPackPath)
		{
			std::error_code ec;
			std::filesystem::create_directories(shaderPackPath.parent_path(), ec);
			if (ec)
			{
				LUX_CONSOLE_LOG_ERROR("Failed to create shader pack directory '{}': {}", shaderPackPath.parent_path().string(), ec.message());
				return false;
			}

			Ref<ShaderLibrary> shaderLibrary = Renderer::GetShaderLibrary();
			if (!shaderLibrary)
			{
				LUX_CONSOLE_LOG_ERROR("Runtime export failed: renderer shader library is not available.");
				return false;
			}

			ShaderPack::CreateFromLibrary(shaderLibrary, shaderPackPath);
			if (!FileExists(shaderPackPath))
			{
				LUX_CONSOLE_LOG_ERROR("Runtime export failed while writing shader pack '{}'.", shaderPackPath.string());
				return false;
			}

			LUX_CONSOLE_LOG_INFO("  ShaderPack.lsp: created");
			return true;
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

		Ref<Texture2D> LoadTextureFromPath(const std::filesystem::path& path, bool srgb = true)
		{
			TextureSpecification spec;
			spec.Format = srgb ? ImageFormat::SRGBA : ImageFormat::RGBA;
			spec.GenerateMips = !srgb;
			spec.DebugName = path.string();
			return Texture2D::Create(spec, path);
		}

		void ClearSceneRendererDebugOptions(SceneRendererOptions& options)
		{
			options.ShowGrid = false;
			options.ShowSelectedInWireframe = false;
			options.ShowPhysicsColliders = false;
			options.PhysicsColliderMode = SceneRendererOptions::PhysicsColliderView::SelectedEntity;
			options.ShowPhysicsCollidersOnTop = false;
			options.ShowShadowCascades = false;
			options.ShowCascadeFrustums = false;
			options.ShowLightComplexity = false;
			options.ShowMaterialComplexity = false;
		}
	}

	EditorLayer::EditorLayer()
		: Layer("EditorLayer"), m_SquareColor({ 0.2f, 0.3f, 0.8f, 1.0f })
	{
	}

	void EditorLayer::OnAttach()
	{
		LUX_PROFILE_FUNCTION("EditorLayer::OnAttach");

		EditorResources::Init();
		LoadEditorPreferences();
		LoadUserPreferences();

		// Apply the persisted async transfer-queue preference (default on). The
		// setting only takes effect where the GPU exposes a dedicated transfer
		// queue; otherwise uploads fall back to the graphics queue. See
		// Renderer::UseAsyncTransferQueue / ApplicationSettingsPanel Threading page.
		Renderer::SetAsyncTransferQueueEnabled(
			Application::Get().GetSettings().Get("Renderer.AsyncTransferQueue", "false") != "false");

		/////////// Configure Panels ///////////
		m_PanelManager = CreateScope<PanelManager>();

		m_SceneHierarchyPanel = m_PanelManager->AddPanel<SceneHierarchyPanel>(PanelCategory::View, SCENE_HIERARCHY_PANEL_ID, "Scene Hierarchy", true);
		Ref<ContentBrowserPanel> contentBrowserPanel = m_PanelManager->AddPanel<ContentBrowserPanel>(PanelCategory::View, CONTENT_BROWSER_PANEL_ID, "Content Browser", true);
		Ref<TextEditorPanel> textEditorPanel = m_PanelManager->AddPanel<TextEditorPanel>(PanelCategory::View, "TextEditorPanel", "Beam", true);
		m_ConsolePanel = m_PanelManager->AddPanel<EditorConsolePanel>(PanelCategory::View, CONSOLE_PANEL_ID, "Log", true);

		m_SceneRendererPanel = m_PanelManager->AddPanel<SceneRendererPanel>(PanelCategory::View, SCENE_RENDERER_PANEL_ID, "Scene Renderer", false);
		m_RendererDebuggerPanel = m_PanelManager->AddPanel<RendererDebuggerPanel>(PanelCategory::View, RENDERER_DEBUGGER_PANEL_ID, "Renderer Debugger", false);
		m_ProfilerPanel = m_PanelManager->AddPanel<ProfilerPanel>(PanelCategory::View, "ProfilerPanel", "Profiler", false);

		{
			UndoHistoryPanel::Bindings historyBindings;
			historyBindings.UndoLabels = [this]() {
				const std::vector<UndoCommand>& stack = ActiveUndoStack();
				std::vector<std::string> labels;
				labels.reserve(stack.size());
				for (const UndoCommand& command : stack)
					labels.push_back(command.Label);
				return labels;
			};
			historyBindings.RedoLabels = [this]() {
				const std::vector<UndoCommand>& stack = ActiveRedoStack();
				std::vector<std::string> labels;
				labels.reserve(stack.size());
				for (auto it = stack.rbegin(); it != stack.rend(); ++it)
					labels.push_back(it->Label);
				return labels;
			};
			historyBindings.Undo = [this](int steps) { for (int i = 0; i < steps; i++) UndoSceneEdit(); };
			historyBindings.Redo = [this](int steps) { for (int i = 0; i < steps; i++) RedoSceneEdit(); };
			historyBindings.IsAvailable = [this]() { return true; };   // works in Edit and Play/Simulate
			m_PanelManager->AddPanel<UndoHistoryPanel>(PanelCategory::View, "UndoHistoryPanel", "History", false, historyBindings);
		}
		if (m_SceneRendererPanel)
		{
			m_SceneRendererPanel->SetDebugViewCallbacks(
				[this]() { ResetRendererDebugViews(); },
				[this]() { SyncEditorDebugViewsFromRenderer(); });
		}

		ApplicationSettingsPanel::EditorPreferencesBindings editorPreferencesBindings{};
		editorPreferencesBindings.VSync = &m_VSync;
		editorPreferencesBindings.TargetFrameRate = &m_TargetFrameRate;
		editorPreferencesBindings.SwapChainBufferCount = &m_SwapChainBufferCount;
		editorPreferencesBindings.PreferImmediatePresentMode = &m_PreferImmediatePresentMode;
		editorPreferencesBindings.UseGizmoSnap = &m_UseGizmoSnap;
		editorPreferencesBindings.TranslationSnapValue = &m_TranslationSnapValue;
		editorPreferencesBindings.RotationSnapValue = &m_RotationSnapValue;
		editorPreferencesBindings.ShowBoundingBoxes = &m_ShowBoundingBoxes;
		editorPreferencesBindings.ShowEntityIcons = &m_ShowEntityIcons;
		editorPreferencesBindings.ShowViewportPerformanceHUD = &m_ShowViewportPerformanceHUD;
		editorPreferencesBindings.ShowPhysicsColliders = &m_ShowPhysicsColliders;
		editorPreferencesBindings.SimpleLayout = &m_SimpleLayout;
		editorPreferencesBindings.OnLayoutModeChanged = [this](bool simple)
			{
				SetEditorLayoutMode(simple);
			};
		editorPreferencesBindings.OnPreferencesChanged = [this]()
			{
				ApplyEditorPreferences();
				SaveEditorPreferences();
			};

		m_PanelManager->AddPanel<ApplicationSettingsPanel>(PanelCategory::View, APPLICATION_SETTINGS_PANEL_ID, "Application Settings", false, contentBrowserPanel, editorPreferencesBindings, m_UserPreferences);
		m_PanelManager->AddPanel<AssetManagerPanel>(PanelCategory::View, ASSET_MANAGER_PANEL_ID, "Asset Manager", false);
		m_PanelManager->AddPanel<ProjectSettingsWindow>(PanelCategory::View, PROJECT_SETTINGS_PANEL_ID, "Project Settings", false);

		// Light Settings panel
		m_PanelManager->AddPanel<LightSettingsPanel>(PanelCategory::View, "LightSettingsPanel", "Light Settings", false);

		// Command palette (Ctrl+Shift+P) — populated after every panel exists so panel toggles resolve.
		m_CommandPalette = CreateScope<CommandPalette>();
		RegisterCommands();

		m_IconPlay = LoadTextureFromPath("Resources/Editor/Viewport/Play.png");
		m_IconPause = LoadTextureFromPath("Resources/Editor/Viewport/Pause.png");
		m_IconSimulate = LoadTextureFromPath("Resources/Editor/Viewport/Simulate.png");
		m_IconStep = LoadTextureFromPath("Resources/Editor/Viewport/Step.png");
		m_IconStop = LoadTextureFromPath("Resources/Editor/Viewport/Stop.png");

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
		m_EditorScene = Ref<Scene>::Create();
		m_ActiveScene = m_EditorScene;

		SceneRendererSpecification sceneRendererSpec;
		sceneRendererSpec.ViewportWidth = 1280;
		sceneRendererSpec.ViewportHeight = 720;

		m_EditorViewport = Ref<Viewport>::Create("Viewport");
		m_EditorViewport->Init(m_ActiveScene, fbSpec, sceneRendererSpec);
		m_Framebuffer = m_EditorViewport->GetFramebuffer();
		m_SceneRenderer = m_EditorViewport->GetSceneRenderer();

		// Now safe to call - m_Renderer2D and the viewport framebuffer are valid.
		m_Renderer2D->SetTargetFramebuffer(m_Framebuffer);

		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetContext(m_SceneRenderer);
		if (m_RendererDebuggerPanel)
			m_RendererDebuggerPanel->SetContext(m_SceneRenderer);
		if (m_ProfilerPanel)
			m_ProfilerPanel->SetContext(m_SceneRenderer);

		m_PanelManager->SetSceneContext(m_EditorScene);
		m_PanelManager->OnProjectChanged(Project::GetActive());
		ApplyEditorPreferences();

		if (contentBrowserPanel)
		{
			// Route content-browser asset delete/move/rename onto the editor undo stack.
			contentBrowserPanel->SetUndoPush([this](const std::string& label, std::function<void()> undo, std::function<void()> redo)
			{
				PushUndoCommand(label, std::move(undo), std::move(redo));
			});

			contentBrowserPanel->RegisterItemActivateCallbackForType(AssetType::Scene, [this](const AssetMetadata& metadata)
			{
				OpenScene(metadata.Handle);
			});

			contentBrowserPanel->RegisterItemActivateCallbackForType(AssetType::Prefab, [this](const AssetMetadata& metadata)
			{
				EnterPrefabEditMode(metadata.Handle);
			});

			if (textEditorPanel)
			{
				contentBrowserPanel->RegisterItemActivateCallbackForType(AssetType::ScriptFile, [this, textEditorPanel](const AssetMetadata& metadata) mutable
				{
					textEditorPanel->OpenFile(Project::GetActive()->GetEditorAssetManager()->GetFileSystemPath(metadata));
					if (PanelData* panelData = m_PanelManager->GetPanelData(Hash::GenerateFNVHash("TextEditorPanel")))
						panelData->IsOpen = true;
				});
			}

		}

		if (m_SceneRenderer)
			m_SceneRenderer->GetOptions().ShowPhysicsColliders = m_ShowPhysicsColliders;

		if (std::filesystem::path startupProject = GetStartupProjectPath(); !startupProject.empty())
			OpenProject(startupProject);

		// Baseline the undo history against whatever scene ended up loaded (startup scene, or the
		// empty default) so the first edit has a valid state to undo back to.
		ResetUndoHistory();
		m_EntityBookmarks.clear();   // bookmarks are per-scene UUIDs
	}

	void EditorLayer::OnDetach()
	{
		LUX_PROFILE_FUNCTION("EditorLayer::OnDetach");
		SaveEditorPreferences();

		// Release the active project + asset manager now, while the Vulkan device is still alive.
		// Project::s_AssetManager is a static Ref that would otherwise be destroyed at program exit —
		// long after the device — and freeing its cached scenes' GPU resources (IndexBuffers, etc.)
		// against a dead device segfaults on shutdown. SetActive(nullptr) runs its Shutdown() and drops
		// the reference here instead.
		Project::SetActive(nullptr);

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
		if (m_EditorViewport)
			m_EditorViewport->Shutdown();
		m_EditorViewport.reset();
		m_SceneRenderer.reset();
		m_RendererDebuggerPanel.reset();
		m_ProfilerPanel.reset();
		m_SceneRendererPanel.reset();
		m_SceneHierarchyPanel.reset();
		EditorResources::Shutdown();
	}

	void EditorLayer::OnUpdate(Timestep ts)
	{
		LUX_PROFILE_FUNCTION("EditorLayer::OnUpdate");

		// Before the early-out below: presence should still reflect "no project open".
		UpdateDiscordPresence();

		if (!m_ActiveScene || !m_EditorViewport)
			return;

		m_EditorViewport->SyncSceneViewport(m_ActiveScene);
		m_Framebuffer = m_EditorViewport->GetFramebuffer();
		m_SceneRenderer = m_EditorViewport->GetSceneRenderer();

		EditorCamera& viewportCamera = m_EditorViewport->GetCamera();
		viewportCamera.SetActive(m_EditorViewport->IsFocused() || m_EditorViewport->IsHovered());

		if (m_SceneRenderer)
		{
			m_SceneRenderer->GetOptions().ShowPhysicsColliders = m_ShowPhysicsColliders;
			m_SceneRenderer->GetOptions().ShowSelectedInWireframe = m_EditorViewport->IsSelectedWireframeMode();
		}

		m_Renderer2D->ResetStats();

		// Create selection predicate for 3D rendering (highlights selected entity)
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
			viewportCamera.OnUpdate(ts);

			// When SceneRenderer is ready, let it own the full visible frame and
			// composite the 2D pass on top of the 3D result. Falling back to the
			// old Renderer2D-only path keeps the editor usable during init.
			if (m_SceneRenderer && m_SceneRenderer->IsReady())
				m_ActiveScene->OnRenderEditor(m_SceneRenderer, viewportCamera, isEntitySelected);
			else
				m_ActiveScene->OnUpdateEditor(ts, viewportCamera);
			break;
		}
		case SceneState::Simulate:
		{
			viewportCamera.OnUpdate(ts);

			// Update simulation first so the rendered frame matches the latest
			// physics state, then render the composed 3D + 2D scene.
			m_ActiveScene->OnUpdateSimulation(ts, viewportCamera);
			if (m_SceneRenderer && m_SceneRenderer->IsReady())
				m_ActiveScene->OnRenderSimulation(m_SceneRenderer, viewportCamera, isEntitySelected);
			break;
		}
		case SceneState::Play:
		{
			// Runtime has to update before rendering so sprites, circles and text
			// appear in their current positions in the main viewport.
			m_ActiveScene->OnUpdateRuntime(ts);
			if (m_SceneRenderer && m_SceneRenderer->IsReady())
				m_ActiveScene->OnRenderRuntime(m_SceneRenderer);
			break;
		}
		}

		{
			LUX_PROFILE_SCOPE("EditorLayer::OnOverlayRender");
			OnOverlayRender();
		}
		{
			LUX_PROFILE_SCOPE("SceneRenderer::WaitForThreads");
			SceneRenderer::WaitForThreads();
		}
	}

	void EditorLayer::OnImGuiRender()
	{
		LUX_PROFILE_FUNCTION("EditorLayer::OnImGuiRender");

		// Must be called once per frame before any other ImGuizmo function.
		// Without this, IsOver() / IsUsing() return stale state and Manipulate()
		// produces garbage transforms.
		ImGuizmo::BeginFrame();

		static bool dockspaceOpen = true;
		static bool opt_fullscreen_persistent = true;
		bool opt_fullscreen = opt_fullscreen_persistent;
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

		GLFWwindow* nativeWindow = Application::Get().GetWindow().GetNativeWindow();
		const bool isWindowMaximized = nativeWindow && glfwGetWindowAttrib(nativeWindow, GLFW_MAXIMIZED);

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
		if (opt_fullscreen)
		{
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->Pos);
			ImGui::SetNextWindowSize(viewport->Size);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, isWindowMaximized ? 0.0f : 3.0f);
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
			ImGuiIO& io = ImGui::GetIO();
			ImGuiStyle& style = ImGui::GetStyle();
			const float minWinSizeX = style.WindowMinSize.x;
			style.WindowMinSize.x = 370.0f;

			UI_DrawTitlebar();
			if (!isWindowMaximized)
			{
				ImGuiEx::ScopedColour borderColour(ImGuiCol_Border, IM_COL32(50, 50, 50, 255));
				ImGuiEx::RenderWindowOuterBorders(ImGui::GetCurrentWindow());
			}

			ImGui::SetCursorPosY(m_TitlebarHeight);
			if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
			{
				const ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
				ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

				// DockSpace() creates the node as an unsplit leaf when imgui.ini had nothing saved
				// for it; a node that's already split means a saved layout was restored, so this
				// only ever fires on first run (or after Editor/imgui.ini is cleared).
				if (!ImGui::DockBuilderGetNode(dockspace_id)->IsSplitNode())
					ResetDefaultDockLayout(dockspace_id);

				// A layout-mode switch requested from a menu last frame is applied here, where the
				// dockspace id is valid.
				if (m_PendingLayoutReset)
				{
					ResetDefaultDockLayout(dockspace_id);
					m_PendingLayoutReset = false;
				}
			}
			style.WindowMinSize.x = minWinSizeX;

			m_PanelManager->OnImGuiRender();

			// After the panels have drawn (and possibly signalled an edit), decide whether to record
			// an undo snapshot. Runs here so it sees this frame's edits and the final active-item state.
			PollSceneEditForUndo();
			UI_UndoToast();
			UI_ScriptToast();
			UI_PrefabEditBanner();

			RenderRuntimeExportWindow();

			if (m_ShowImGuiMetrics)
				ImGui::ShowMetricsWindow(&m_ShowImGuiMetrics);
			if (m_ShowImGuiStyleEditor)
				ImGui::ShowStyleEditor();
			if (m_ShowAboutPopup)
				ImGui::OpenPopup("About LuxEngine");

			ImGuiViewport* mainViewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(mainViewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

			if (ImGui::BeginPopupModal("About LuxEngine", &m_ShowAboutPopup, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text("LuxEngine Editor");
				ImGui::Separator();
				ImGui::Text("Version: %s", Application::GetConfigurationName());
				ImGui::Text("Platform: %s", Application::GetPlatformName());
				ImGui::TextWrapped("Credits: Inspired by Hazel architecture and editor workflows.");

				if (ImGui::Button("Close"))
				{
					m_ShowAboutPopup = false;
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			// Command palette overlay — drawn late, brings itself to front via SetNextWindowFocus.
			// The Ctrl+Shift+P chord is read through ImGui's own input rather than the engine key
			// event, so it fires regardless of which panel currently holds keyboard focus.
			if (m_CommandPalette)
			{
				if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P))
					m_CommandPalette->Toggle();

				m_CommandPalette->OnImGuiRender();
			}

			// Viewport panel
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
			const bool viewportReady = m_EditorViewport && m_EditorViewport->BeginImGui();
			if (m_EditorViewport)
			{
				Application::Get().GetImGuiLayer()->AllowInputEvents(m_EditorViewport->IsFocused() || m_EditorViewport->IsHovered());

				if (viewportReady)
				{
					const glm::vec2& viewportSize = m_EditorViewport->GetSize();
					const glm::vec2* viewportBounds = m_EditorViewport->GetBounds();

					Ref<Image2D> viewportImage = m_EditorViewport->GetDisplayImage();
					bool renderedViewportImage = false;
					if (viewportImage)
					{
						// Draw into the letterboxed rect rather than filling the panel, so a render
						// whose aspect differs from the panel (a fixed 1920x1080 target on a 4K or
						// ultrawide viewport) keeps its framing instead of being stretched. The
						// panel background shows through as the bars.
						const glm::vec2* imageBounds = m_EditorViewport->GetImageBounds();
						const glm::vec2 imageSize = m_EditorViewport->GetImageSize();
						const ImVec2 cursor = ImGui::GetCursorPos();
						ImGui::SetCursorPos(ImVec2{
							cursor.x + (imageBounds[0].x - viewportBounds[0].x),
							cursor.y + (imageBounds[0].y - viewportBounds[0].y) });

						ImTextureID texID = GetImGuiTextureID(viewportImage);
						ImGui::Image(texID, ImVec2{ imageSize.x, imageSize.y }, ImVec2{ 0, 0 }, ImVec2{ 1, 1 });
						renderedViewportImage = true;
					}

					if (renderedViewportImage && ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
						{
							AssetHandle handle = *(AssetHandle*)payload->Data;
							const AssetType assetType = AssetManager::GetAssetType(handle);
							if (assetType == AssetType::Scene)
							{
								OpenScene(handle);
							}
							else if ((assetType == AssetType::MeshSource || assetType == AssetType::StaticMesh) && m_ActiveScene)
							{
								std::string entityName = "Static Mesh";
								if (Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager())
								{
									const AssetMetadata metadata = editorAssetManager->GetMetadata(handle);
									if (metadata.IsValid() && !metadata.FilePath.empty())
										entityName = metadata.FilePath.stem().string();
								}

								Entity entity = m_ActiveScene->CreateEntity(entityName);
								auto& staticMesh = entity.AddComponent<StaticMeshComponent>();
								staticMesh.StaticMesh = handle;

								if (m_SceneHierarchyPanel)
									m_SceneHierarchyPanel->SetSelectedEntity(entity);
							}
						}
						ImGui::EndDragDropTarget();
					}

					if (m_EditorViewport->IsHovered())
						m_HoveredEntity = CastMousePick();
					else
						m_HoveredEntity = {};

					UI_ViewportPerformanceHUD();

					// Gizmos
					Entity selectedEntity = {};
					if (m_SceneHierarchyPanel)
						selectedEntity = m_SceneHierarchyPanel->GetSelectedEntity();
					// Folders have no user-editable transform, so they get no gizmo.
					if (selectedEntity && m_GizmoType != -1 && !selectedEntity.HasComponent<FolderComponent>())
					{
						ImGuizmo::SetOrthographic(false);
						ImGuizmo::SetDrawlist();
						// The image rect, not the panel rect - the gizmo has to line up with the
						// rendered image, which is inset by the letterbox bars when the render
						// aspect differs from the panel's.
						const glm::vec2* gizmoBounds = m_EditorViewport->GetImageBounds();
						const glm::vec2 gizmoSize = m_EditorViewport->GetImageSize();
						ImGuizmo::SetRect(gizmoBounds[0].x, gizmoBounds[0].y, gizmoSize.x, gizmoSize.y);

						EditorCamera& viewportCamera = m_EditorViewport->GetCamera();
						const glm::mat4& cameraProjection = viewportCamera.GetProjectionMatrix();
						glm::mat4 cameraView = viewportCamera.GetViewMatrix();

						auto& tc = selectedEntity.GetComponent<TransformComponent>();
						glm::mat4 transform = tc.GetTransform();

						const bool controlSnap = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
						bool snap = m_UseGizmoSnap || controlSnap;
						float snapValue = (m_GizmoType == ImGuizmo::OPERATION::ROTATE) ? m_RotationSnapValue : m_TranslationSnapValue;
						float snapValues[3] = { snapValue, snapValue, snapValue };

						ImGuizmo::Manipulate(
							glm::value_ptr(cameraView),
							glm::value_ptr(cameraProjection),
							(ImGuizmo::OPERATION)m_GizmoType,
							ImGuizmo::LOCAL,
							glm::value_ptr(transform),
							nullptr,
							snap ? snapValues : nullptr);

						if (ImGuizmo::IsUsing())
						{
							glm::vec3 translation, scale;
							glm::quat rotationQuat;
							Math::DecomposeTransform(transform, translation, rotationQuat, scale);

							glm::vec3 rotationEuler = glm::eulerAngles(rotationQuat);
							glm::vec3 deltaRotation = rotationEuler - tc.GetRotationEuler();
							tc.Translation = translation;
							tc.SetRotationEuler(tc.GetRotationEuler() + deltaRotation);
							tc.Scale = scale;
						}

						// Record one undo step when a gizmo drag finishes (the transform is mutated above
						// but not through a property widget, so it doesn't raise the ImGuiEx signal).
						const bool gizmoUsing = ImGuizmo::IsUsing();
						if (m_GizmoWasUsing && !gizmoUsing)
						{
							const char* gizmoLabel = m_GizmoType == ImGuizmo::OPERATION::ROTATE ? "Rotate"
								: m_GizmoType == ImGuizmo::OPERATION::SCALE ? "Scale" : "Move";
							EditorStack::Get().MarkSceneEdited(gizmoLabel);
						}
						m_GizmoWasUsing = gizmoUsing;
					}
				}

				m_EditorViewport->EndImGui();
			}
			ImGui::PopStyleVar();

			// Gizmo tools + transport live in the titlebar. The other viewport overlays only show
			// while the viewport is the visible tab, so they never sit over Beam.
			if (m_EditorViewport->IsVisible())
			{
				UI_ViewportSettings();
				UI_ViewportOrientationGizmo();
				UI_ViewportSelectionBadge();
			}
		}

		ImGui::End(); // Lux Editor
	}

	void EditorLayer::ResetDefaultDockLayout(ImGuiID dockspaceId)
	{
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

		ImGuiID center = dockspaceId;
		const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
		const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
		const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);

		// Core panels, present in both layouts.
		ImGui::DockBuilderDockWindow("Scene Hierarchy", left);
		ImGui::DockBuilderDockWindow("Properties", right);
		ImGui::DockBuilderDockWindow("Content Browser", bottom);
		ImGui::DockBuilderDockWindow("Log", bottom);
		ImGui::DockBuilderDockWindow("Viewport", center);
		ImGui::DockBuilderDockWindow("Beam", center);

		// Advanced mode fills out the workspace with the diagnostic panels: a left-bottom group
		// under the hierarchy (Scene Renderer / Light Settings / Profiler) and the Renderer
		// Debugger tabbed into the bottom dock. In Simple mode these stay closed (see
		// SetEditorLayoutMode) and are absent from the default arrangement.
		if (!m_SimpleLayout)
		{
			const ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.5f, nullptr, nullptr);
			ImGui::DockBuilderDockWindow("Scene Renderer", leftBottom);
			ImGui::DockBuilderDockWindow("Light Settings", leftBottom);
			ImGui::DockBuilderDockWindow("Profiler", leftBottom);
			ImGui::DockBuilderDockWindow("Renderer Debugger", bottom);
		}

		ImGui::DockBuilderFinish(dockspaceId);
	}

	void EditorLayer::SetEditorLayoutMode(bool simple)
	{
		m_SimpleLayout = simple;

		// The advanced-only panels follow the mode: opened when entering Advanced, closed when
		// returning to Simple. Everything else keeps whatever the user set.
		static const char* const s_AdvancedPanels[] = { "Scene Renderer", "Light Settings", "Profiler", "Renderer Debugger" };
		auto& viewPanels = m_PanelManager->GetPanels(PanelCategory::View);
		for (auto& [id, panelData] : viewPanels)
		{
			for (const char* advancedName : s_AdvancedPanels)
			{
				if (std::strcmp(panelData.Name, advancedName) == 0)
				{
					panelData.IsOpen = !simple;
					break;
				}
			}
		}

		SaveEditorPreferences();
		m_PendingLayoutReset = true;
	}

	void EditorLayer::RegisterCommands()
	{
		m_CommandPalette->Clear();

		auto add = [this](const char* name, const char* category, const char* shortcut,
			std::function<void()> action, std::function<bool()> enabled = nullptr)
		{
			m_CommandPalette->Register({ name, category, shortcut, std::move(action), std::move(enabled) });
		};

		// File
		add("New Scene", "File", "Ctrl+N", [this] { NewScene(); });
		add("Save Scene", "File", "Ctrl+S", [this] { SaveScene(); });
		add("Save Scene As…", "File", "Ctrl+Shift+S", [this] { SaveSceneAs(); });
		add("Open Project…", "File", "Ctrl+O", [this] { OpenProject(); });
		add("Save Project", "File", "", [this] { SaveProject(); });
		add("Export Runtime…", "File", "", [this] { ExportRuntime(); });
		add("Exit", "File", "", []
		{
			Application::Get().QueueEvent([] { Application::Get().DispatchEvent<WindowCloseEvent, true>(); });
		});

		// Edit
		add("Undo", "Edit", "Ctrl+Z", [this] { UndoSceneEdit(); }, [this] { return !ActiveUndoStack().empty(); });
		add("Redo", "Edit", "Ctrl+Shift+Z", [this] { RedoSceneEdit(); }, [this] { return !ActiveRedoStack().empty(); });
		add("Reload C# Assembly", "Edit", "Ctrl+R", [this]
		{
			ReloadScriptsWithFeedback();
		});
		add("Reload All Shaders", "Edit", "Ctrl+Shift+R", [] { Renderer::ReloadShaders(true); });

		// Scene transport
		add("Play", "Scene", "", [this] { OnScenePlay(); }, [this] { return m_SceneState == SceneState::Edit; });
		add("Simulate Physics", "Scene", "", [this] { OnSceneSimulate(); }, [this] { return m_SceneState == SceneState::Edit; });
		add("Stop", "Scene", "", [this] { OnSceneStop(); }, [this] { return m_SceneState != SceneState::Edit; });

		// View
		add("Reset Window Layout", "View", "", [this] { ResetDefaultDockLayout(ImGui::GetID("MyDockSpace")); });
		add("Bookmark Selected Entity", "View", "Ctrl+B", [this] { ToggleEntityBookmark(); },
			[this] { return SelectionManager::GetSelectionCount(SelectionContext::Scene) > 0; });
		add("Create Prefab from Selection", "Edit", "", [this] { CreatePrefabFromSelection(); },
			[this] { return SelectionManager::GetSelectionCount(SelectionContext::Scene) > 0; });

		// A toggle-open command for every registered panel, resolved by ID at run time.
		for (auto& [id, panelData] : m_PanelManager->GetPanels(PanelCategory::View))
		{
			const uint32_t panelID = id;
			add(panelData.Name, "Panel", "", [this, panelID]
			{
				if (PanelData* data = m_PanelManager->GetPanelData(panelID))
					data->IsOpen = true;
			});
		}

		// Tools
		add("ImGui Metrics", "Tools", "", [this] { m_ShowImGuiMetrics = true; });
		add("ImGui Style Editor", "Tools", "", [this] { m_ShowImGuiStyleEditor = true; });
		add("About LuxEngine", "Help", "", [this] { m_ShowAboutPopup = true; });
	}

	void EditorLayer::SetCameraBookmark(int slot)
	{
		if (slot < 1 || slot > 9 || !m_EditorViewport)
			return;

		const EditorCamera& camera = m_EditorViewport->GetCamera();
		CameraBookmark& bookmark = m_CameraBookmarks[slot];
		bookmark.FocalPoint = camera.GetFocalPoint();
		bookmark.Distance = camera.GetDistance();
		bookmark.Pitch = camera.GetPitch();
		bookmark.Yaw = camera.GetYaw();
		bookmark.Set = true;
	}

	void EditorLayer::JumpToCameraBookmark(int slot)
	{
		if (slot < 1 || slot > 9 || !m_EditorViewport)
			return;

		const CameraBookmark& bookmark = m_CameraBookmarks[slot];
		if (!bookmark.Set)
			return;

		m_EditorViewport->GetCamera().SetOrbitState(bookmark.FocalPoint, bookmark.Distance, bookmark.Pitch, bookmark.Yaw);
	}

	void EditorLayer::CreatePrefabFromSelection()
	{
		const auto& selections = SelectionManager::GetSelections(SelectionContext::Scene);
		if (selections.empty() || !m_EditorScene)
			return;

		Entity entity = m_EditorScene->TryGetEntityWithUUID(selections.front());
		if (!entity)
			return;

		Ref<Prefab> prefab = Ref<Prefab>::Create();
		std::unordered_map<UUID, UUID> sourceToPrefab;
		prefab->Create(entity, true, &sourceToPrefab);

		auto assetManager = Project::GetEditorAssetManager();
		const std::string extension = assetManager->GetDefaultExtensionForAssetType(AssetType::Prefab);
		const std::filesystem::path targetPath = FileSystem::GetUniqueFileName(
			Project::GetActiveAssetDirectory() / (entity.GetName() + extension));
		const std::filesystem::path relativePath = std::filesystem::relative(targetPath, Project::GetActiveAssetDirectory());

		// Write the prefab's hierarchy (a scene) to disk, then register it as an asset — PrefabSerializer
		// handles both directions from here on.
		SceneSerializer serializer(prefab->GetScene());
		serializer.Serialize(targetPath);

		const AssetHandle handle = assetManager->ImportAsset(relativePath);
		if (handle)
		{
			// Link the source hierarchy to the new prefab so it becomes a live instance (each source
			// entity points at its clone inside the prefab).
			for (const auto& [sourceUUID, prefabUUID] : sourceToPrefab)
			{
				Entity linked = m_EditorScene->TryGetEntityWithUUID(sourceUUID);
				if (!linked)
					continue;

				auto& prefabComponent = linked.AddOrReplaceComponent<PrefabComponent>();
				prefabComponent.PrefabID = handle;
				prefabComponent.EntityID = prefabUUID;
			}
			EditorStack::Get().MarkSceneEdited("Create Prefab");
			LUX_CONSOLE_LOG_INFO("Created prefab '{}'", relativePath.generic_string());
		}
		else
		{
			LUX_CONSOLE_LOG_ERROR("Failed to register prefab '{}'", relativePath.generic_string());
		}
	}

	void EditorLayer::ToggleEntityBookmark()
	{
		const auto& selections = SelectionManager::GetSelections(SelectionContext::Scene);
		for (UUID id : selections)
		{
			auto it = std::find(m_EntityBookmarks.begin(), m_EntityBookmarks.end(), id);
			if (it != m_EntityBookmarks.end())
				m_EntityBookmarks.erase(it);
			else
				m_EntityBookmarks.push_back(id);
		}
	}

	void EditorLayer::JumpToEntityBookmark(UUID entityID)
	{
		if (!m_EditorScene)
			return;

		Entity entity = m_EditorScene->TryGetEntityWithUUID(entityID);
		if (!entity)
			return;

		SelectionManager::DeselectAll(SelectionContext::Scene);
		SelectionManager::Select(SelectionContext::Scene, entityID);

		if (m_EditorViewport)
			m_EditorViewport->GetCamera().Focus(m_EditorScene->GetWorldSpaceTransform(entity).Translation);
	}

	void EditorLayer::UI_DrawMenubar()
	{
		const ImVec2 menuBarMin = ImGui::GetCursorPos();
		const ImRect menuBarRect = { menuBarMin, { ImGui::GetContentRegionAvail().x + ImGui::GetCursorScreenPos().x, menuBarMin.y + ImGui::GetFrameHeightWithSpacing() } };

		ImGui::BeginGroup();

		ImGuiEx::ScopedColourStack menuColors(
			ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(Colors::Theme::accent),
			ImGuiCol_HeaderHovered, ImGui::ColorConvertU32ToFloat4(Colors::Theme::accent),
			ImGuiCol_HeaderActive, ImGui::ColorConvertU32ToFloat4(Colors::Theme::accent),
			ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colors::Theme::text));

		if (ImGuiEx::BeginMenuBar(menuBarRect))
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Create Project"))
					NewProject();
				if (ImGui::MenuItem("Open Project...", "Ctrl+O"))
					OpenProject();
				if (ImGui::MenuItem("Save Project"))
					SaveProject();
				if (ImGui::MenuItem("Export Runtime..."))
					ExportRuntime();

				if (ImGui::BeginMenu("Recent Projects"))
				{
					const std::vector<RecentProject> recentProjects = GetRecentProjects();
					if (recentProjects.empty())
					{
						ImGui::MenuItem("No recent projects", nullptr, false, false);
					}
					else
					{
						for (const auto& recentProject : recentProjects)
						{
							const std::filesystem::path projectPath = NormalizeProjectPath(recentProject.FilePath);
							const std::string fullPath = projectPath.generic_string();
							const std::string displayName = recentProject.Name.empty() ? (projectPath.stem().string().empty() ? fullPath : projectPath.stem().string()) : recentProject.Name;
							std::error_code ec;
							const bool exists = !projectPath.empty() && std::filesystem::exists(projectPath, ec) && !ec;

							ImGui::PushID(fullPath.c_str());
							if (ImGui::MenuItem(displayName.c_str(), nullptr, false, exists))
								OpenProject(projectPath);

							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::TextUnformatted(fullPath.c_str());
								if (!exists)
									ImGui::TextDisabled("Project file not found");
								ImGui::EndTooltip();
							}
							ImGui::PopID();
						}
					}

					ImGui::EndMenu();
				}

				ImGui::Separator();
				if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
					SaveScene();
				if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
					SaveSceneAs();

				ImGui::Separator();
				if (ImGui::MenuItem("Exit"))
					Application::Get().QueueEvent([]()
					{
						Application::Get().DispatchEvent<WindowCloseEvent, true>();
					});

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Edit"))
			{
				const std::vector<UndoCommand>& undoStack = ActiveUndoStack();   // edit or play, per mode
				const std::vector<UndoCommand>& redoStack = ActiveRedoStack();
				const std::string undoLabel = undoStack.empty() ? "Undo" : "Undo " + undoStack.back().Label;
				const std::string redoLabel = redoStack.empty() ? "Redo" : "Redo " + redoStack.back().Label;
				if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, !undoStack.empty()))
					UndoSceneEdit();
				if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Shift+Z", false, !redoStack.empty()))
					RedoSceneEdit();
				ImGui::Separator();

				if (ImGui::MenuItem("Reload C# Assembly", "Ctrl+R"))
					ReloadScriptsWithFeedback();
				if (ImGui::MenuItem("Reload All Shaders", "Ctrl+Shift+R"))
					Renderer::ReloadShaders(true);
				ImGui::Separator();
				if (ImGui::MenuItem("Create Prefab from Selection", nullptr, false,
					SelectionManager::GetSelectionCount(SelectionContext::Scene) > 0))
					CreatePrefabFromSelection();
				ImGui::MenuItem("Second Viewport", nullptr, &m_SecondViewportEnabled);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("View"))
			{
				if (ImGui::MenuItem("Reset Layout"))
					ResetDefaultDockLayout(ImGui::GetID("MyDockSpace"));

				if (ImGui::BeginMenu("Camera Bookmarks"))
				{
					for (int slot = 1; slot <= 9; slot++)
					{
						const CameraBookmark& bookmark = m_CameraBookmarks[slot];
						char jumpLabel[32];
						std::snprintf(jumpLabel, sizeof(jumpLabel), "Jump to %d", slot);
						if (ImGui::MenuItem(jumpLabel, std::to_string(slot).c_str(), false, bookmark.Set))
							JumpToCameraBookmark(slot);

						char setLabel[40];
						std::snprintf(setLabel, sizeof(setLabel), "Set %d from current view", slot);
						char setShortcut[16];
						std::snprintf(setShortcut, sizeof(setShortcut), "Ctrl+%d", slot);
						if (ImGui::MenuItem(setLabel, setShortcut))
							SetCameraBookmark(slot);

						if (slot != 9)
							ImGui::Separator();
					}
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Entity Bookmarks"))
				{
					const bool hasSelection = SelectionManager::GetSelectionCount(SelectionContext::Scene) > 0;
					if (ImGui::MenuItem("Bookmark Selected", "Ctrl+B", false, hasSelection))
						ToggleEntityBookmark();
					ImGui::Separator();

					bool anyShown = false;
					if (m_EditorScene)
					{
						for (UUID id : m_EntityBookmarks)
						{
							Entity entity = m_EditorScene->TryGetEntityWithUUID(id);
							if (!entity)
								continue;   // stale UUID (e.g. deleted) — skip
							anyShown = true;
							ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(id)));
							if (ImGui::MenuItem(entity.GetName().c_str()))
								JumpToEntityBookmark(id);
							ImGui::PopID();
						}
					}
					if (!anyShown)
						ImGui::MenuItem("No bookmarks", nullptr, false, false);
					else if (ImGui::MenuItem("Clear All"))
						m_EntityBookmarks.clear();

					ImGui::EndMenu();
				}

				ImGui::Separator();

				auto& viewPanels = m_PanelManager->GetPanels(PanelCategory::View);
				for (auto& [id, panelData] : viewPanels)
					ImGui::MenuItem(panelData.Name, nullptr, &panelData.IsOpen);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Tools"))
			{
				if (ImGui::MenuItem("Command Palette", "Ctrl+Shift+P") && m_CommandPalette)
					m_CommandPalette->Open();
				ImGui::Separator();
				ImGui::MenuItem("ImGui Metrics", nullptr, &m_ShowImGuiMetrics);
				ImGui::MenuItem("ImGui Style Editor", nullptr, &m_ShowImGuiStyleEditor);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Help"))
			{
				if (ImGui::MenuItem("About"))
					m_ShowAboutPopup = true;
				ImGui::EndMenu();
			}

			ImGuiEx::EndMenuBar();
		}

		ImGui::EndGroup();
	}

	void EditorLayer::UI_DrawTitlebar()
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 windowPos = window->Pos;
		const ImVec2 windowMax = ImVec2(window->Pos.x + window->Size.x, window->Pos.y + m_TitlebarHeight);

		ImU32 stateAccentColor = Colors::Theme::titlebar;
		if (m_SceneState == SceneState::Play)
			stateAccentColor = Colors::Theme::titlebarGreen;
		else if (m_SceneState == SceneState::Simulate)
			stateAccentColor = Colors::Theme::titlebarOrange;

		const ImVec4 targetColor = ImGui::ColorConvertU32ToFloat4(stateAccentColor);
		const float dt = ImGui::GetIO().DeltaTime;
		m_AnimatedTitlebarColor = ImLerp(m_AnimatedTitlebarColor, targetColor, std::clamp(dt * 8.0f, 0.0f, 1.0f));
		const ImU32 activeTitlebarColor = ImGui::ColorConvertFloat4ToU32(m_AnimatedTitlebarColor);
		drawList->AddRectFilled(windowPos, windowMax, Colors::Theme::titlebar);
		drawList->AddRectFilledMultiColor(windowPos, ImVec2(windowPos.x + 380.0f, windowMax.y),
			activeTitlebarColor, Colors::Theme::titlebar, Colors::Theme::titlebar, activeTitlebarColor);

		drawList->AddLine(ImVec2(windowPos.x, windowPos.y + m_TitlebarHeight), ImVec2(windowPos.x + window->Size.x, windowPos.y + m_TitlebarHeight), Colors::Theme::backgroundDark);

		// --- LUX wordmark: a vector mark + the Display wordmark (replaces the Hazel logo). ---
		const float kNoWrap = std::numeric_limits<float>::max();
		const float markX = windowPos.x + 14.0f;
		const float markSize = 16.0f;
		const float markCY = windowPos.y + m_TitlebarHeight * 0.5f;
		{
			// An "L" in the accent lime plus a play-chevron in text colour — the mock's Lux glyph.
			const float x = markX, y = markCY - markSize * 0.5f, s = markSize;
			drawList->AddLine(ImVec2(x + s * 0.14f, y + s * 0.12f), ImVec2(x + s * 0.14f, y + s * 0.88f), Colors::Theme::accent, 2.0f);
			drawList->AddLine(ImVec2(x + s * 0.14f, y + s * 0.88f), ImVec2(x + s * 0.56f, y + s * 0.88f), Colors::Theme::accent, 2.0f);
			drawList->AddLine(ImVec2(x + s * 0.56f, y + s * 0.12f), ImVec2(x + s * 0.90f, y + s * 0.50f), Colors::Theme::textBrighter, 2.0f);
			drawList->AddLine(ImVec2(x + s * 0.90f, y + s * 0.50f), ImVec2(x + s * 0.56f, y + s * 0.88f), Colors::Theme::textBrighter, 2.0f);
		}
		const float fontScale = ImGuiEx::Fonts::GetScale();
		ImFont* displayFont = ImGuiEx::Fonts::Get("Display");
		const float wordmarkSize = 17.0f * fontScale;
		const char* wordmarkText = "LUX";
		const float wordmarkX = markX + markSize + 9.0f;
		const ImVec2 wordmarkSizeVec = displayFont->CalcTextSizeA(wordmarkSize, kNoWrap, 0.0f, wordmarkText);
		drawList->AddText(displayFont, wordmarkSize, ImVec2(wordmarkX, markCY - wordmarkSizeVec.y * 0.5f), Colors::Theme::textBrighter, wordmarkText);

		GLFWwindow* nativeWindow = Application::Get().GetWindow().GetNativeWindow();
		const bool isMaximized = nativeWindow && glfwGetWindowAttrib(nativeWindow, GLFW_MAXIMIZED);

		const float menuBarX = wordmarkX + wordmarkSizeVec.x + 18.0f;
		const float iconWidth = 14.0f;
		const float iconHeight = 14.0f;
		const float buttonWidth = 46.0f;
		const float buttonHeight = m_TitlebarHeight;
		const float buttonY = 0.0f;
		const float closeButtonX = window->Size.x - buttonWidth;
		const float maximizeButtonX = closeButtonX - buttonWidth;
		const float minimizeButtonX = maximizeButtonX - buttonWidth;
		const float titlebarGap = 12.0f;

		const float dragZoneMinX = 70.0f;
		const float dragZoneMaxX = std::max(dragZoneMinX, minimizeButtonX - titlebarGap);

		// Vertically centre the menu bar in the (tall) titlebar so it lines up with the logo,
		// breadcrumb, and window controls instead of hugging the top.
		const float menuBarY = std::max(0.0f, (m_TitlebarHeight - ImGui::GetFrameHeight()) * 0.5f);

		float menuBarRight = menuBarX;
#ifdef LUX_PLATFORM_LINUX
		// On Linux/Wayland, the compositor handles window dragging via
		// glfwSetTitlebarHitTestCallback — no InvisibleButton needed.
		// Draw the menu bar first, then set the drag zone to start AFTER it
		// so menu clicks aren't intercepted as window drags.
		ImGui::SetCursorPos(ImVec2(menuBarX, menuBarY));
		UI_DrawMenubar();
		menuBarRight = ImGui::GetItemRectMax().x - windowPos.x;
		m_TitleBarDragRectMin = ImVec2(menuBarRight, 0.0f);
		m_TitleBarDragRectMax = ImVec2(dragZoneMaxX, m_TitlebarHeight);
#else
		m_TitleBarDragRectMin = ImVec2(dragZoneMinX, 0.0f);
		m_TitleBarDragRectMax = ImVec2(dragZoneMaxX, m_TitlebarHeight);

		ImGui::SetNextItemAllowOverlap();
		ImGui::SetCursorPos(ImVec2(dragZoneMinX, 0.0f));
		ImGui::InvisibleButton("##titleBarDragZone", ImVec2(std::max(0.0f, dragZoneMaxX - dragZoneMinX), m_TitlebarHeight));

		ImGui::SuspendLayout();
		{
			ImGui::SetCursorPos(ImVec2(menuBarX, menuBarY));
			UI_DrawMenubar();
			menuBarRight = ImGui::GetItemRectMax().x - windowPos.x;
		}
		ImGui::ResumeLayout();
#endif

		// --- Breadcrumb: Project / Scene, in the mono face, just after the menu bar. ---
		{
			ImFont* monoFont = ImGuiEx::Fonts::Get("Mono");
			const float bcSize = 13.0f * fontScale;
			const std::string projectName = GetProjectDisplayName();
			const std::string sceneName = GetSceneDisplayName(m_EditorScenePath);

			float x = windowPos.x + menuBarRight + 16.0f;
			// Clip to the drag-zone's right edge so a long project/scene name can't overrun the
			// window controls (the old project box ellipsized; clipping is the equivalent guard).
			drawList->PushClipRect(ImVec2(x, windowPos.y), ImVec2(windowPos.x + dragZoneMaxX, windowPos.y + m_TitlebarHeight), true);
			drawList->AddLine(ImVec2(x, windowPos.y + 12.0f), ImVec2(x, windowPos.y + m_TitlebarHeight - 12.0f), Colors::Theme::muted, 1.0f);
			x += 12.0f;

			const float glyphH = monoFont->CalcTextSizeA(bcSize, kNoWrap, 0.0f, "X").y;
			const float textY = markCY - glyphH * 0.5f;

			drawList->AddText(monoFont, bcSize, ImVec2(x, textY), Colors::Theme::textDarker, projectName.c_str());
			x += monoFont->CalcTextSizeA(bcSize, kNoWrap, 0.0f, projectName.c_str()).x + 6.0f;
			drawList->AddText(monoFont, bcSize, ImVec2(x, textY), Colors::Theme::muted, "/");
			x += monoFont->CalcTextSizeA(bcSize, kNoWrap, 0.0f, "/").x + 6.0f;
			const ImVec2 sceneSz = monoFont->CalcTextSizeA(bcSize, kNoWrap, 0.0f, sceneName.c_str());
			drawList->AddText(monoFont, bcSize, ImVec2(x, textY), Colors::Theme::textBrighter, sceneName.c_str());
			// Subtle lime accent under the active scene segment.
			drawList->AddLine(ImVec2(x, textY + sceneSz.y + 2.0f), ImVec2(x + sceneSz.x, textY + sceneSz.y + 2.0f), Colors::Theme::accent, 1.5f);
			drawList->PopClipRect();
		}

		const ImU32 buttonColN = ImGuiEx::ColourWithMultipliedValue(Colors::Theme::text, 0.9f);
		const ImU32 buttonColH = ImGuiEx::ColourWithMultipliedValue(Colors::Theme::text, 1.2f);
		const ImU32 buttonColP = Colors::Theme::textDarker;

		auto drawWindowControlButton = [&](const char* id, const Ref<Texture2D>& icon, float localX, auto&& onClick)
			{
				ImGui::SetCursorPos(ImVec2(localX, buttonY));
				ImGui::InvisibleButton(id, ImVec2(buttonWidth, buttonHeight));
				if (icon)
				{
					const ImVec2 buttonMin = ImGui::GetItemRectMin();
					const ImVec2 iconMin(
						buttonMin.x + (buttonWidth - iconWidth) * 0.5f,
						buttonMin.y + (buttonHeight - iconHeight) * 0.5f);
					const ImVec2 iconMax(iconMin.x + iconWidth, iconMin.y + iconHeight);
					ImGuiEx::DrawButtonImage(icon, buttonColN, buttonColH, buttonColP, iconMin, iconMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
				}

				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
					onClick();
			};

		ImGui::SetCursorPos(ImVec2(minimizeButtonX, buttonY));
		ImGui::InvisibleButton("##minimizeWindow", ImVec2(buttonWidth, buttonHeight));
		if (EditorResources::MinimizeIcon)
		{
			const float iconHeight = (float)EditorResources::MinimizeIcon->GetHeight();
			const ImVec2 buttonMin = ImGui::GetItemRectMin();
			const float padY = (14.0f - iconHeight) * 0.5f;
			const ImVec2 iconMin(
				buttonMin.x + (buttonWidth - iconWidth) * 0.5f,
				buttonMin.y + (buttonHeight - 14.0f) * 0.5f);
			const ImVec2 iconMax(iconMin.x + iconWidth, iconMin.y + 14.0f);
			ImGuiEx::DrawButtonImage(EditorResources::MinimizeIcon, buttonColN, buttonColH, buttonColP,
				ImGuiEx::RectExpanded(ImRect(iconMin, iconMax), 0.0f, -padY));
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			if (nativeWindow)
				Application::Get().QueueEvent([nativeWindow]() { glfwIconifyWindow(nativeWindow); });
		}

		drawWindowControlButton("##maximizeRestoreWindow", isMaximized ? EditorResources::RestoreIcon : EditorResources::MaximizeIcon, maximizeButtonX, [nativeWindow, isMaximized]()
			{
				if (!nativeWindow)
					return;
				Application::Get().QueueEvent([nativeWindow, isMaximized]()
					{
						if (isMaximized)
							glfwRestoreWindow(nativeWindow);
						else
							Application::Get().GetWindow().Maximize();
					});
			});

		drawWindowControlButton("##closeWindow", EditorResources::CloseIcon, closeButtonX, []()
			{
				Application::Get().QueueEvent([]()
				{
					Application::Get().DispatchEvent<WindowCloseEvent, true>();
				});
			});

		// Play / simulate / stop, centred in the titlebar (drawn last so it overlaps the drag zone).
		UI_TitlebarTransport(window->Size.x);
	}

	namespace
	{
		// Flat vector tool glyphs shared by the gizmo overlay and the titlebar transport. Gizmo
		// tools are stroked outlines; transport controls are filled shapes.
		enum class ToolGlyph { Select, Move, Rotate, Scale, Play, Simulate, Stop };

		void DrawToolGlyph(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ToolGlyph glyph, ImU32 col)
		{
			const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
			const float s = mx.x - mn.x;
			const float t = 1.6f; // stroke width for outline glyphs
			switch (glyph)
			{
			case ToolGlyph::Select:
			{
				const ImVec2 tail(c.x - s * 0.20f, c.y - s * 0.22f);
				const ImVec2 head(c.x + s * 0.16f, c.y + s * 0.24f);
				dl->AddLine(tail, head, col, t);
				dl->AddLine(head, ImVec2(head.x - s * 0.16f, head.y), col, t);
				dl->AddLine(head, ImVec2(head.x, head.y - s * 0.16f), col, t);
				break;
			}
			case ToolGlyph::Move:
			{
				const float r = s * 0.30f;
				dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col, t);
				dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, t);
				break;
			}
			case ToolGlyph::Rotate:
				dl->AddCircle(c, s * 0.28f, col, 0, t);
				break;
			case ToolGlyph::Scale:
			{
				const float sq = s * 0.26f, off = s * 0.14f;
				dl->AddRect(ImVec2(c.x - off - sq, c.y - off - sq), ImVec2(c.x - off, c.y - off), col, 0.0f, 0, t);
				dl->AddRect(ImVec2(c.x + off, c.y + off), ImVec2(c.x + off + sq, c.y + off + sq), col, 0.0f, 0, t);
				break;
			}
			case ToolGlyph::Play:
			{
				const float hw = s * 0.22f, hh = s * 0.27f;
				dl->AddTriangleFilled(ImVec2(c.x - hw, c.y - hh), ImVec2(c.x - hw, c.y + hh), ImVec2(c.x + hw * 1.6f, c.y), col);
				break;
			}
			case ToolGlyph::Simulate:
			{
				const float hw = s * 0.15f, hh = s * 0.25f;
				dl->AddTriangleFilled(ImVec2(c.x - hw * 2.2f, c.y - hh), ImVec2(c.x - hw * 2.2f, c.y + hh), ImVec2(c.x - hw * 0.2f, c.y), col);
				dl->AddTriangleFilled(ImVec2(c.x - hw * 0.2f, c.y - hh), ImVec2(c.x - hw * 0.2f, c.y + hh), ImVec2(c.x + hw * 1.8f, c.y), col);
				break;
			}
			case ToolGlyph::Stop:
			{
				const float r = s * 0.23f;
				dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 1.0f);
				break;
			}
			}
		}
	}

	// Gizmo tools (select / move / rotate / scale) as a floating overlay at the viewport's
	// top-left. Only drawn while the viewport is the visible tab (gated by the caller).
	// The viewport toolbar, centred in the titlebar: gizmo tools (select / move / rotate / scale) +
	// a divider + transport (play / simulate / stop). The gizmo half only appears while the viewport
	// is the visible tab; the transport is always shown. The whole group's local rect is recorded so
	// the window drag zone excludes it (OnTitleBarHitTest), keeping the buttons clickable.
	void EditorLayer::UI_TitlebarTransport(float titlebarWidth)
	{
		const ImVec2 buttonSize(24.0f, 24.0f);
		const float transportSpacing = 6.0f;
		const float gizmoSpacing = 3.0f;
		const float dividerBlock = 16.0f; // 8px gap + line + 8px gap

		const bool showGizmo = m_EditorViewport && m_EditorViewport->IsVisible();

		const float transportWidth = buttonSize.x * 3.0f + transportSpacing * 2.0f;
		const float gizmoWidth = buttonSize.x * 4.0f + gizmoSpacing * 3.0f;
		const float totalWidth = transportWidth + (showGizmo ? gizmoWidth + dividerBlock : 0.0f);

		const float startX = (titlebarWidth - totalWidth) * 0.5f;
		const float startY = (m_TitlebarHeight - buttonSize.y) * 0.5f;

		auto gizmoButton = [&](const char* id, ToolGlyph glyph, int mode)
			{
				ImGui::SetNextItemAllowOverlap();
				ImGui::InvisibleButton(id, buttonSize);
				const bool hovered = ImGui::IsItemHovered();
				const bool active = m_GizmoType == mode;
				ImDrawList* dl = ImGui::GetWindowDrawList();
				const ImVec2 mn = ImGui::GetItemRectMin();
				const ImVec2 mx = ImGui::GetItemRectMax();
				if (active)
					dl->AddRectFilled(mn, mx, Colors::Theme::selectionMuted, 2.0f);
				else if (hovered)
					dl->AddRectFilled(mn, mx, IM_COL32(255, 255, 255, 16), 2.0f);
				const ImU32 col = active ? Colors::Theme::accent : (hovered ? Colors::Theme::textBrighter : Colors::Theme::text);
				DrawToolGlyph(dl, mn, mx, glyph, col);
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
					m_GizmoType = mode;
			};

		auto controlButton = [&](const char* id, ToolGlyph glyph, ImU32 normalCol, ImU32 hoverCol, bool active, const std::function<void()>& onClick)
			{
				ImGui::SetNextItemAllowOverlap();
				ImGui::InvisibleButton(id, buttonSize);
				const bool hovered = ImGui::IsItemHovered();
				ImDrawList* dl = ImGui::GetWindowDrawList();
				const ImVec2 mn = ImGui::GetItemRectMin();
				const ImVec2 mx = ImGui::GetItemRectMax();
				if (hovered)
					dl->AddRectFilled(mn, mx, IM_COL32(255, 255, 255, 16), 2.0f);
				DrawToolGlyph(dl, mn, mx, glyph, hovered ? hoverCol : normalCol);
				if (active)
					dl->AddRect(mn, mx, Colors::Theme::accent, 2.0f, 0, 1.5f);
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
					onClick();
			};

		ImGui::SetCursorPos(ImVec2(startX, startY));

		if (showGizmo)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gizmoSpacing, 0.0f));
			gizmoButton("##tb_gizmo_select", ToolGlyph::Select, -1);
			ImGui::SameLine();
			gizmoButton("##tb_gizmo_translate", ToolGlyph::Move, ImGuizmo::TRANSLATE);
			ImGui::SameLine();
			gizmoButton("##tb_gizmo_rotate", ToolGlyph::Rotate, ImGuizmo::ROTATE);
			ImGui::SameLine();
			gizmoButton("##tb_gizmo_scale", ToolGlyph::Scale, ImGuizmo::SCALE);
			ImGui::PopStyleVar();

			ImGui::SameLine(0.0f, 8.0f);
			{
				ImDrawList* dl = ImGui::GetWindowDrawList();
				const ImVec2 p = ImGui::GetCursorScreenPos();
				dl->AddLine(ImVec2(p.x, p.y + 4.0f), ImVec2(p.x, p.y + buttonSize.y - 4.0f), Colors::Theme::muted, 1.0f);
				ImGui::Dummy(ImVec2(1.0f, buttonSize.y));
			}
			ImGui::SameLine(0.0f, 8.0f);
		}

		const bool playing = m_SceneState == SceneState::Play;
		const bool simulating = m_SceneState == SceneState::Simulate;
		const bool editing = m_SceneState == SceneState::Edit;

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(transportSpacing, 0.0f));
		controlButton("##tb_play", ToolGlyph::Play,
			playing ? Colors::Theme::muted : Colors::Theme::accent,
			playing ? Colors::Theme::textDarker : Colors::Theme::accent,
			playing, [this]() { if (m_SceneState != SceneState::Play) OnScenePlay(); });
		ImGui::SameLine();
		controlButton("##tb_simulate", ToolGlyph::Simulate,
			simulating ? Colors::Theme::accent : Colors::Theme::textDarker,
			simulating ? Colors::Theme::accent : Colors::Theme::textBrighter,
			simulating, [this]() { if (m_SceneState != SceneState::Simulate) OnSceneSimulate(); });
		ImGui::SameLine();
		controlButton("##tb_stop", ToolGlyph::Stop,
			editing ? Colors::Theme::textDarker : Colors::Theme::text,
			editing ? Colors::Theme::textDarker : Colors::Theme::textBrighter,
			editing, [this]() { if (m_SceneState != SceneState::Edit) OnSceneStop(); });
		ImGui::PopStyleVar();

		// Local-space rect (window is at 0,0) over the whole group, matching the hit-test event space.
		m_TitleBarTransportRectMin = ImVec2(startX - 4.0f, 0.0f);
		m_TitleBarTransportRectMax = ImVec2(startX + totalWidth + 4.0f, m_TitlebarHeight);
	}

	void EditorLayer::UI_ViewportPerformanceHUD()
	{
		if (!m_ShowViewportPerformanceHUD || !m_EditorViewport || !m_SceneRenderer || !m_EditorViewport->IsVisible())
			return;

		const glm::vec2& viewportSize = m_EditorViewport->GetSize();
		const glm::vec2* viewportBounds = m_EditorViewport->GetBounds();
		if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return;

		const auto& stats = m_SceneRenderer->GetStatistics();
		const auto& memory = stats.MemoryStats;
		const ImGuiIO& io = ImGui::GetIO();
		const float fps = io.Framerate;
		const float frameTimeMs = fps > 0.0f ? 1000.0f / fps : 0.0f;
		const float renderScale = m_SceneRenderer->GetRenderResolutionScale() * 100.0f;
		const auto& appTimers = Application::Get().GetPerformanceTimers();

		// How the two thread timings combine depends on the threading policy. Under
		// MultiThreaded they overlap, so the frame is gated by the slower of the two;
		// summing them double-counted the frame and made this HUD contradict its own
		// frame time. Under SingleThreaded the render work runs inline on the main
		// thread, so the spans are sequential and do add up.
		const bool renderThreadIsConcurrent =
			Application::Get().GetSpecification().CoreThreadingPolicy == ThreadingPolicy::MultiThreaded;
		const float wholeCPUTime = renderThreadIsConcurrent
			? std::max(appTimers.MainThreadWorkTime, appTimers.RenderThreadWorkTime)
			: appTimers.MainThreadWorkTime + appTimers.RenderThreadWorkTime;

		float wholeGPUTime = stats.TotalGPUTime;
		if (ImGuiLayer* imguiLayer = Application::Get().GetImGuiLayer())
		{
			if (ImGuiRenderer* imguiRenderer = imguiLayer->GetImGuiRenderer())
				wholeGPUTime += imguiRenderer->GetGPUTime();
		}

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

		ImGui::SetNextWindowPos(ImVec2(viewportBounds[1].x - 12.0f, viewportBounds[1].y - 12.0f), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.48f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
		ImGui::Begin("##viewport_performance_hud", nullptr, flags);
		// Numeric readout in the monospace face (JetBrains Mono), like the mock's fps/ms pill.
		ImGuiEx::Fonts::PushFont("Mono");
		ImGui::Text("FPS %.0f  %.2f ms", fps, frameTimeMs);
		ImGui::Text("CPU %.2f ms  GPU %.2f ms", wholeCPUTime, wholeGPUTime);
		ImGui::Text("Draws %u  Visible %u", stats.DrawCalls, stats.VisibleInstances);
		ImGui::Text("GPU Visible %u", stats.GPUVisibleInstances);
		if (memory.BudgetBytes > 0)
			ImGui::Text("VRAM %s / %s", Utils::BytesToString(memory.UsedBytes).c_str(), Utils::BytesToString(memory.BudgetBytes).c_str());
		else
			ImGui::Text("VRAM %s", Utils::BytesToString(memory.UsedBytes).c_str());
		ImGui::Text("Scale %.0f%%  %ux%u", renderScale, m_SceneRenderer->GetOutputViewportWidth(), m_SceneRenderer->GetOutputViewportHeight());
		ImGuiEx::Fonts::PopFont();
		ImGui::End();
		ImGui::PopStyleVar();
	}

	void EditorLayer::UI_ViewportSettings()
	{
		if (!m_EditorViewport)
			return;

		const glm::vec2& viewportSize = m_EditorViewport->GetSize();
		const glm::vec2* viewportBounds = m_EditorViewport->GetBounds();
		if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return;

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

		ImGui::SetNextWindowPos(ImVec2(viewportBounds[1].x - 44.0f, viewportBounds[0].y + 12.0f), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.55f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
		ImGui::Begin("##viewport_settings_toolbar", nullptr, flags);

		const ImVec2 buttonSize(24.0f, 24.0f);
		ImGui::InvisibleButton("##viewport_settings_btn", buttonSize);
		ImGuiEx::DrawButtonImage(EditorResources::GearIcon, IM_COL32(215, 215, 215, 220), IM_COL32(255, 255, 255, 255), IM_COL32(235, 235, 235, 255), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			ImGui::OpenPopup("##viewport_settings_popup");

		if (ImGui::BeginPopup("##viewport_settings_popup"))
		{
			bool settingsChanged = false;

			if (ImGui::Checkbox("Enable Gizmo Snap", &m_UseGizmoSnap))
				settingsChanged = true;
			if (m_UseGizmoSnap)
			{
				if (ImGui::DragFloat("Translate Snap", &m_TranslationSnapValue, 0.05f, 0.05f, 10.0f, "%.2f"))
					settingsChanged = true;
				if (ImGui::DragFloat("Rotate Snap", &m_RotationSnapValue, 1.0f, 1.0f, 180.0f, "%.0f"))
					settingsChanged = true;
			}

			ImGui::Separator();
			if (m_PlayModeDebugViewsSuspended)
				ImGui::TextDisabled("Debug views are suspended during Play.");

			if (m_PlayModeDebugViewsSuspended)
				ImGui::BeginDisabled();

			if (ImGui::Checkbox("Show Bounding Boxes", &m_ShowBoundingBoxes))
				settingsChanged = true;
			if (ImGui::Checkbox("Show Entity Icons", &m_ShowEntityIcons))
				settingsChanged = true;

			if (m_SceneRenderer)
			{
				auto& options = m_SceneRenderer->GetOptions();
				ImGui::Checkbox("Show Grid", &options.ShowGrid);
				if (ImGui::Checkbox("Show Physics Colliders", &options.ShowPhysicsColliders))
				{
					m_ShowPhysicsColliders = options.ShowPhysicsColliders;
					settingsChanged = true;
				}
			}

			if (ImGui::Checkbox("Performance HUD", &m_ShowViewportPerformanceHUD))
				settingsChanged = true;

			int displayMode = (int)m_EditorViewport->GetDisplayMode();
			if (ImGui::Combo("Display Mode", &displayMode, "Lit\0Selected Wireframe\0"))
				m_EditorViewport->SetDisplayMode((Viewport::DisplayMode)displayMode);

			if (m_PlayModeDebugViewsSuspended)
				ImGui::EndDisabled();

			// The doc places the "back to Simple" affordance in the viewport toolbar, shown only
			// while Advanced mode is active.
			if (!m_SimpleLayout)
			{
				ImGui::Separator();
				if (ImGui::MenuItem("Switch to Simple Mode"))
					SetEditorLayoutMode(true);
			}

			if (settingsChanged)
			{
				ApplyEditorPreferences();
				SaveEditorPreferences();
			}

			ImGui::EndPopup();
		}

		ImGui::End();
		ImGui::PopStyleVar();
	}

	void EditorLayer::UI_ViewportOrientationGizmo()
	{
		if (!m_EditorViewport)
			return;

		const glm::vec2& viewportSize = m_EditorViewport->GetSize();
		const glm::vec2* viewportBounds = m_EditorViewport->GetBounds();
		if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return;

		const float gizmoRadius = 22.0f;
		const float windowExtent = (gizmoRadius + 8.0f) * 2.0f;

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

		// Top-right, tucked below the settings gear so the two don't overlap.
		ImGui::SetNextWindowPos(ImVec2(viewportBounds[1].x - 12.0f, viewportBounds[0].y + 48.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		ImGui::SetNextWindowBgAlpha(0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("##viewport_orientation_gizmo", nullptr, flags);
		ImGui::Dummy(ImVec2(windowExtent, windowExtent));

		const ImVec2 winMin = ImGui::GetItemRectMin();
		const ImVec2 center(winMin.x + windowExtent * 0.5f, winMin.y + windowExtent * 0.5f);
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// Rotation-only basis from the camera view matrix: transforming a world axis by it gives the
		// axis direction in view space. Screen projection is orthographic — (x, -y), z only for
		// draw ordering so nearer axis heads paint over farther ones.
		const glm::mat4& view = m_EditorViewport->GetCamera().GetViewMatrix();

		struct Axis { glm::vec3 world; ImU32 color; const char* label; };
		const Axis axes[3] = {
			{ { 1.0f, 0.0f, 0.0f }, IM_COL32(210, 74, 74, 255), "X" },
			{ { 0.0f, 1.0f, 0.0f }, IM_COL32(120, 190, 90, 255), "Y" },
			{ { 0.0f, 0.0f, 1.0f }, IM_COL32(90, 140, 220, 255), "Z" },
		};

		struct Projected { ImVec2 tip; float depth; ImU32 color; const char* label; };
		Projected projected[3];
		for (int i = 0; i < 3; i++)
		{
			const glm::vec3 v = glm::vec3(view * glm::vec4(axes[i].world, 0.0f));
			projected[i] = {
				ImVec2(center.x + v.x * gizmoRadius, center.y - v.y * gizmoRadius),
				v.z, axes[i].color, axes[i].label };
		}

		int order[3] = { 0, 1, 2 };
		std::sort(order, order + 3, [&](int a, int b) { return projected[a].depth < projected[b].depth; });

		for (int idx = 0; idx < 3; idx++)
		{
			const Projected& p = projected[order[idx]];
			drawList->AddLine(center, p.tip, p.color, 2.0f);
			drawList->AddCircleFilled(p.tip, 4.0f, p.color);
			const ImVec2 labelSize = ImGui::CalcTextSize(p.label);
			drawList->AddText(ImVec2(p.tip.x - labelSize.x * 0.5f, p.tip.y - labelSize.y * 0.5f), Colors::Theme::titlebar, p.label);
		}

		ImGui::End();
		ImGui::PopStyleVar();
	}

	void EditorLayer::UI_ViewportSelectionBadge()
	{
		if (!m_EditorViewport || !m_SceneHierarchyPanel)
			return;

		const glm::vec2& viewportSize = m_EditorViewport->GetSize();
		const glm::vec2* viewportBounds = m_EditorViewport->GetBounds();
		if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return;

		Entity selectedEntity = m_SceneHierarchyPanel->GetSelectedEntity();
		if (!selectedEntity)
			return;

		const std::string badgeText = Utils::String::ToUpperCopy(selectedEntity.Name()) + " SELECTED";

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

		// Top-left corner: the gizmo/tool toolbar moved to the centre, so this owns the corner now.
		ImGui::SetNextWindowPos(ImVec2(viewportBounds[0].x + 12.0f, viewportBounds[0].y + 12.0f), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("##viewport_selection_badge", nullptr, flags);

		const ImVec2 textSize = ImGui::CalcTextSize(badgeText.c_str());
		const ImVec2 pad(8.0f, 3.0f);
		ImGui::Dummy(ImVec2(textSize.x + pad.x * 2.0f, textSize.y + pad.y * 2.0f));

		const ImVec2 rectMin = ImGui::GetItemRectMin();
		const ImVec2 rectMax = ImGui::GetItemRectMax();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(rectMin, rectMax, Colors::Theme::accent, 2.0f);
		drawList->AddText(ImVec2(rectMin.x + pad.x, rectMin.y + pad.y), Colors::Theme::titlebar, badgeText.c_str());

		ImGui::End();
		ImGui::PopStyleVar();
	}

	void EditorLayer::UI_Toolbar()
	{
		// The other overlays only when the viewport is the visible tab; the gizmo tools + transport
		// are in the titlebar.
		if (!m_EditorViewport || !m_EditorViewport->IsVisible())
			return;
		UI_ViewportSettings();
		UI_ViewportOrientationGizmo();
		UI_ViewportSelectionBadge();
	}

	void EditorLayer::OnEvent(Event& e)
	{
		if ((m_SceneState == SceneState::Edit || m_SceneState == SceneState::Simulate) && m_EditorViewport && m_EditorViewport->IsHovered())
			m_EditorViewport->GetCamera().OnEvent(e);

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
		case Key::B: if (control) ToggleEntityBookmark(); break;

		// Undo/redo. Skipped while a text field is active so ImGui's own in-field undo keeps Ctrl+Z.
		case Key::Z:
			if (control && !ImGui::GetIO().WantTextInput)
			{
				if (shift) RedoSceneEdit();
				else       UndoSceneEdit();
			}
			break;
		case Key::Y: if (control && !ImGui::GetIO().WantTextInput) RedoSceneEdit(); break;

		case Key::Q: if (!ImGuizmo::IsUsing()) m_GizmoType = -1;                            break;
		case Key::W: if (!ImGuizmo::IsUsing()) m_GizmoType = ImGuizmo::OPERATION::TRANSLATE; break;
		case Key::E: if (!ImGuizmo::IsUsing()) m_GizmoType = ImGuizmo::OPERATION::ROTATE;   break;
		case Key::R:
			if (control && shift) Renderer::ReloadShaders(true);
			else if (control)
			{
				ReloadScriptsWithFeedback();
			}
			else if (!ImGuizmo::IsUsing()) m_GizmoType = ImGuizmo::OPERATION::SCALE;
			break;

		default: break;
		}

		// Viewport camera bookmarks: Ctrl+<1-9> stores the current view, <1-9> jumps to it. Gated on
		// the viewport being hovered and no text field active, so digits typed elsewhere are untouched.
		const int keyCode = static_cast<int>(e.GetKeyCode());
		if (keyCode >= static_cast<int>(Key::D1) && keyCode <= static_cast<int>(Key::D9)
			&& m_EditorViewport && m_EditorViewport->IsHovered() && !ImGui::GetIO().WantTextInput)
		{
			const int slot = keyCode - static_cast<int>(Key::D0);
			if (control && !shift)
				SetCameraBookmark(slot);
			else if (!control && !shift)
				JumpToCameraBookmark(slot);
		}

		return false;
	}

	bool EditorLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
	{
		if (e.GetMouseButton() == MouseButton::Left)
		{
			if (m_EditorViewport && m_EditorViewport->IsHovered() && !ImGuizmo::IsOver() && !Input::IsKeyPressed(Key::LeftAlt))
			{
				m_HoveredEntity = CastMousePick();
				if (m_SceneHierarchyPanel)
					m_SceneHierarchyPanel->SetSelectedEntity(m_HoveredEntity);
			}
		}
		return false;
	}

	Entity EditorLayer::CastMousePick()
	{
		if (!m_ActiveScene || !m_EditorViewport)
			return {};

		// NDC has to be derived from the rendered image, not the panel: when the render aspect
		// differs from the panel's the image is letterboxed, so panel-relative coordinates are
		// offset by the bars and the picked ray misses what the cursor is actually over. Clicks
		// that land on a bar fall outside the image and correctly pick nothing.
		const glm::vec2* viewportBounds = m_EditorViewport->GetImageBounds();
		const float viewportWidth = viewportBounds[1].x - viewportBounds[0].x;
		const float viewportHeight = viewportBounds[1].y - viewportBounds[0].y;
		if (viewportWidth <= 1.0f || viewportHeight <= 1.0f)
			return {};

		const float mouseX = Input::GetMouseX() - viewportBounds[0].x;
		const float mouseY = Input::GetMouseY() - viewportBounds[0].y;
		if (mouseX < 0.0f || mouseY < 0.0f || mouseX > viewportWidth || mouseY > viewportHeight)
			return {};

		const float ndcX = (mouseX / viewportWidth) * 2.0f - 1.0f;
		const float ndcY = 1.0f - (mouseY / viewportHeight) * 2.0f;

		glm::mat4 inverseViewProjection = glm::inverse(m_EditorViewport->GetCamera().GetUnReversedViewProjection());
		glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
		glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		if (nearPoint.w == 0.0f || farPoint.w == 0.0f)
			return {};

		nearPoint /= nearPoint.w;
		farPoint /= farPoint.w;

		const glm::vec3 rayOrigin = glm::vec3(nearPoint);
		const glm::vec3 rayVector = glm::vec3(farPoint - nearPoint);
		if (glm::dot(rayVector, rayVector) <= std::numeric_limits<float>::epsilon())
			return {};
		const glm::vec3 rayDirection = glm::normalize(rayVector);

		Entity closestEntity = {};
		float closestDistance = std::numeric_limits<float>::max();

		auto view = m_ActiveScene->GetAllEntitiesWith<TransformComponent>();
		for (auto entityID : view)
		{
			Entity entity(entityID, m_ActiveScene.Raw());
			float distance = 0.0f;
			if (!RayIntersectsEntity(entity, rayOrigin, rayDirection, distance))
				continue;

			if (distance < closestDistance)
			{
				closestDistance = distance;
				closestEntity = entity;
			}
		}

		return closestEntity;
	}

	bool EditorLayer::RayIntersectsEntity(Entity entity, const glm::vec3& rayOrigin, const glm::vec3& rayDirection, float& outDistance) const
	{
		outDistance = std::numeric_limits<float>::max();
		if (!entity || !entity.HasComponent<TransformComponent>())
			return false;

		const TransformComponent& transformComponent = entity.GetComponent<TransformComponent>();
		const glm::mat4 worldTransform = transformComponent.GetTransform();
		bool hit = false;

		auto testAABB = [&](const AABB& localAABB, const glm::mat4& localTransform = glm::mat4(1.0f))
			{
				const glm::mat4 inverseTransform = glm::inverse(worldTransform * localTransform);
				const glm::vec3 localOrigin = glm::vec3(inverseTransform * glm::vec4(rayOrigin, 1.0f));
				const glm::vec3 localDirectionVector = glm::vec3(inverseTransform * glm::vec4(rayDirection, 0.0f));
				if (glm::dot(localDirectionVector, localDirectionVector) <= std::numeric_limits<float>::epsilon())
					return;
				const glm::vec3 localDirection = glm::normalize(localDirectionVector);

				Ray localRay(localOrigin, localDirection);
				float t = 0.0f;
				if (localRay.IntersectsAABB(localAABB, t) && t >= 0.0f && t < outDistance)
				{
					outDistance = t;
					hit = true;
				}
			};

		if (entity.HasComponent<StaticMeshComponent>())
		{
			const auto& staticMeshComponent = entity.GetComponent<StaticMeshComponent>();
			Ref<StaticMesh> staticMesh = StaticMesh::GetOrCreateRuntime(staticMeshComponent.StaticMesh);
			if (staticMesh)
			{
				Ref<MeshSource> meshSource = AssetManager::GetAsset<MeshSource>(staticMesh->GetMeshSource());
				if (meshSource)
					testAABB(meshSource->GetBoundingBox());
			}
		}

		if (entity.HasComponent<BoxCollider2DComponent>())
		{
			const auto& collider = entity.GetComponent<BoxCollider2DComponent>();
			const glm::vec3 extents = glm::vec3(collider.Size, 0.05f);
			const glm::vec3 center = glm::vec3(collider.Offset, 0.0f);
			testAABB(AABB(center - extents, center + extents));
		}

		if (entity.HasComponent<CircleCollider2DComponent>())
		{
			const auto& collider = entity.GetComponent<CircleCollider2DComponent>();
			const glm::vec3 extents = glm::vec3(collider.Radius, collider.Radius, 0.05f);
			const glm::vec3 center = glm::vec3(collider.Offset, 0.0f);
			testAABB(AABB(center - extents, center + extents));
		}

		const bool shouldUseIconSelection = entity.HasComponent<CameraComponent>() ||
			entity.HasComponent<AudioSourceComponent>() ||
			entity.HasComponent<AudioListenerComponent>() ||
			entity.HasComponent<DirectionalLightComponent>() ||
			entity.HasComponent<PointLightComponent>() ||
			entity.HasComponent<SpotLightComponent>();

		if (!hit && shouldUseIconSelection)
		{
			const glm::vec3 center = transformComponent.Translation;
			const float radius = 0.35f;
			const glm::vec3 toCenter = rayOrigin - center;
			const float b = glm::dot(toCenter, rayDirection);
			const float c = glm::dot(toCenter, toCenter) - radius * radius;
			const float discriminant = b * b - c;
			if (discriminant >= 0.0f)
			{
				const float t = -b - std::sqrt(discriminant);
				if (t >= 0.0f && t < outDistance)
				{
					outDistance = t;
					hit = true;
				}
			}
		}

		return hit;
	}

	bool EditorLayer::OnTitleBarHitTest(WindowTitleBarHitTestEvent& e)
	{
		const float x = (float)e.GetX();
		const float y = (float)e.GetY();

		const bool inTransport = x >= m_TitleBarTransportRectMin.x && x <= m_TitleBarTransportRectMax.x
			&& y >= m_TitleBarTransportRectMin.y && y <= m_TitleBarTransportRectMax.y;

		const bool inDragZone = !inTransport
			&& x >= m_TitleBarDragRectMin.x && x <= m_TitleBarDragRectMax.x
			&& y >= m_TitleBarDragRectMin.y && y <= m_TitleBarDragRectMax.y;

		if (inDragZone)
			e.SetHit(true);

		return inDragZone;
	}

	void EditorLayer::OnOverlayRender()
	{
		Ref<Framebuffer> overlayTarget = m_Framebuffer;
		if (m_SceneRenderer && m_SceneRenderer->GetExternalCompositeFramebuffer())
			overlayTarget = m_SceneRenderer->GetExternalCompositeFramebuffer();

		if (overlayTarget)
			m_Renderer2D->SetTargetFramebuffer(overlayTarget);

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
			EditorCamera& viewportCamera = m_EditorViewport->GetCamera();
			m_Renderer2D->BeginScene(viewportCamera.GetViewProjection(), viewportCamera.GetViewMatrix());
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
						* glm::rotate(glm::mat4(1.0f), tc.GetRotationEuler().z, glm::vec3(0.0f, 0.0f, 1.0f))
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

		}

		Entity selectedEntity = {};
		if (m_SceneHierarchyPanel)
			selectedEntity = m_SceneHierarchyPanel->GetSelectedEntity();

		if (selectedEntity)
		{
			const TransformComponent& transform = selectedEntity.GetComponent<TransformComponent>();
			const glm::mat4 worldTransform = transform.GetTransform();

			if (m_ShowBoundingBoxes && selectedEntity.HasComponent<StaticMeshComponent>())
			{
				const auto& smc = selectedEntity.GetComponent<StaticMeshComponent>();
				Ref<StaticMesh> staticMesh = StaticMesh::GetOrCreateRuntime(smc.StaticMesh);
				if (staticMesh)
				{
					Ref<MeshSource> meshSource = AssetManager::GetAsset<MeshSource>(staticMesh->GetMeshSource());
					if (meshSource)
					{
						m_Renderer2D->DrawAABB(meshSource->GetBoundingBox(), worldTransform, glm::vec4(1.0f, 0.5f, 0.0f, 1.0f), true);
					}
				}
			}
		}

		if (m_ShowEntityIcons)
		{
			auto drawIconForView = [this](auto view, const Ref<Texture2D>& iconTexture)
				{
					if (!iconTexture)
						return;

					for (auto entityID : view)
					{
						auto& transform = view.template get<TransformComponent>(entityID);
						m_Renderer2D->DrawQuadBillboard(transform.Translation, glm::vec2(0.35f), iconTexture, 1.0f, glm::vec4(1.0f));
					}
				};

			drawIconForView(m_ActiveScene->GetAllEntitiesWith<TransformComponent, CameraComponent>(), EditorResources::CameraIcon);
			drawIconForView(m_ActiveScene->GetAllEntitiesWith<TransformComponent, AudioSourceComponent>(), EditorResources::AudioIcon);
			drawIconForView(m_ActiveScene->GetAllEntitiesWith<TransformComponent, AudioListenerComponent>(), EditorResources::AudioListenerIcon);
			drawIconForView(m_ActiveScene->GetAllEntitiesWith<TransformComponent, DirectionalLightComponent>(), EditorResources::DirectionalLightIcon);
			drawIconForView(m_ActiveScene->GetAllEntitiesWith<TransformComponent, PointLightComponent>(), EditorResources::PointLightIcon);
			drawIconForView(m_ActiveScene->GetAllEntitiesWith<TransformComponent, SpotLightComponent>(), EditorResources::SpotLightIcon);
		}

		m_Renderer2D->EndScene();
	}

	void EditorLayer::LoadEditorPreferences()
	{
		auto& settings = Application::Get().GetSettings();

		m_VSync = settings.GetInt("Editor.VSync", Application::Get().GetWindow().IsVSync() ? 1 : 0) != 0;
		m_TargetFrameRate = std::max(settings.GetInt("Editor.TargetFrameRate", 0), 0);
		m_SwapChainBufferCount = std::clamp(settings.GetInt("Editor.SwapChainBufferCount",
			(int)Application::Get().GetWindow().GetSwapChainBufferCount()), 2, 8);
		m_PreferImmediatePresentMode = settings.GetInt("Editor.PreferImmediatePresentMode",
			Application::Get().GetWindow().PrefersImmediatePresentMode() ? 1 : 0) != 0;
		m_UseGizmoSnap = settings.GetInt("Editor.UseGizmoSnap", 0) != 0;
		m_TranslationSnapValue = std::max(settings.GetFloat("Editor.TranslationSnapValue", 0.5f), 0.05f);
		m_RotationSnapValue = std::max(settings.GetFloat("Editor.RotationSnapValue", 45.0f), 1.0f);
		m_ShowBoundingBoxes = settings.GetInt("Editor.ShowBoundingBoxes", 0) != 0;
		m_ShowEntityIcons = settings.GetInt("Editor.ShowEntityIcons", 1) != 0;
		m_ShowViewportPerformanceHUD = settings.GetInt("Editor.ShowViewportPerformanceHUD", 1) != 0;
		m_ShowPhysicsColliders = settings.GetInt("Editor.ShowPhysicsColliders", 0) != 0;
		m_SimpleLayout = settings.GetInt("Editor.SimpleLayout", 1) != 0;

		ApplyEditorPreferences();
	}

	void EditorLayer::SaveEditorPreferences() const
	{
		auto& settings = Application::Get().GetSettings();
		settings.SetInt("Editor.VSync", m_VSync ? 1 : 0);
		settings.SetInt("Editor.TargetFrameRate", m_TargetFrameRate);
		settings.SetInt("Editor.SwapChainBufferCount", m_SwapChainBufferCount);
		settings.SetInt("Editor.PreferImmediatePresentMode", m_PreferImmediatePresentMode ? 1 : 0);
		settings.SetInt("Editor.UseGizmoSnap", m_UseGizmoSnap ? 1 : 0);
		settings.SetFloat("Editor.TranslationSnapValue", m_TranslationSnapValue);
		settings.SetFloat("Editor.RotationSnapValue", m_RotationSnapValue);
		settings.SetInt("Editor.ShowBoundingBoxes", m_ShowBoundingBoxes ? 1 : 0);
		settings.SetInt("Editor.ShowEntityIcons", m_ShowEntityIcons ? 1 : 0);
		settings.SetInt("Editor.ShowViewportPerformanceHUD", m_ShowViewportPerformanceHUD ? 1 : 0);
		settings.SetInt("Editor.ShowPhysicsColliders", m_ShowPhysicsColliders ? 1 : 0);
		settings.SetInt("Editor.SimpleLayout", m_SimpleLayout ? 1 : 0);
		settings.Serialize();
	}

	void EditorLayer::ApplyEditorPreferences()
	{
		Application::Get().GetWindow().SetVSync(m_VSync);

		// With VSync on the display already paces the loop, and layering a CPU limiter on
		// top would only fight it, so the limiter is disengaged rather than clamped.
		Application::Get().SetTargetFrameRate(m_VSync ? 0u : (uint32_t)std::max(m_TargetFrameRate, 0));
		Application::Get().GetWindow().SetSwapChainBufferCount((uint32_t)m_SwapChainBufferCount);
		Application::Get().GetWindow().SetPreferImmediatePresentMode(m_PreferImmediatePresentMode);

		if (m_SceneRenderer)
			m_SceneRenderer->GetOptions().ShowPhysicsColliders = m_ShowPhysicsColliders;
	}

	void EditorLayer::LoadUserPreferences()
	{
		m_UserPreferences = CreateRef<UserPreferences>();
		m_UserPreferencesPath = FileSystem::GetPersistentStoragePath() / "UserPreferences.yaml";

		UserPreferencesSerializer serializer(m_UserPreferences);
		bool loaded = serializer.Deserialize(m_UserPreferencesPath);
		bool dirty = false;

		if (!loaded)
			dirty = true;

		if (m_UserPreferences->RecentProjects.empty())
		{
			const auto legacyProjects = LoadLegacyRecentProjects();
			time_t timestamp = time(nullptr);
			for (const auto& legacyProject : legacyProjects)
			{
				RecentProject entry;
				entry.Name = legacyProject.stem().string();
				entry.FilePath = legacyProject.generic_string();
				entry.LastOpened = timestamp--;
				m_UserPreferences->RecentProjects[entry.LastOpened] = entry;
			}

			dirty = dirty || !legacyProjects.empty();
		}

		for (auto it = m_UserPreferences->RecentProjects.begin(); it != m_UserPreferences->RecentProjects.end();)
		{
			const std::filesystem::path projectPath = NormalizeProjectPath(it->second.FilePath);
			std::error_code ec;
			if (projectPath.empty() || !std::filesystem::exists(projectPath, ec) || ec)
			{
				if (!m_UserPreferences->StartupProject.empty() && NormalizeProjectPath(m_UserPreferences->StartupProject) == projectPath)
					m_UserPreferences->StartupProject.clear();

				it = m_UserPreferences->RecentProjects.erase(it);
				dirty = true;
			}
			else
			{
				it->second.FilePath = projectPath.generic_string();
				++it;
			}
		}

		if (!m_UserPreferences->StartupProject.empty())
		{
			const std::filesystem::path startupProject = NormalizeProjectPath(m_UserPreferences->StartupProject);
			std::error_code ec;
			if (startupProject.empty() || !std::filesystem::exists(startupProject, ec) || ec)
			{
				m_UserPreferences->StartupProject.clear();
				dirty = true;
			}
			else
			{
				m_UserPreferences->StartupProject = startupProject.generic_string();
			}
		}

		if (m_UserPreferences->FilePath.empty())
			m_UserPreferences->FilePath = m_UserPreferencesPath;

		if (dirty)
			serializer.Serialize(m_UserPreferencesPath);
	}

	void EditorLayer::SaveUserPreferences() const
	{
		if (!m_UserPreferences)
			return;

		UserPreferencesSerializer serializer(m_UserPreferences);
		serializer.Serialize(m_UserPreferencesPath.empty() ? (FileSystem::GetPersistentStoragePath() / "UserPreferences.yaml") : m_UserPreferencesPath);
	}

	void EditorLayer::AddRecentProject(const std::filesystem::path& projectPath)
	{
		if (!m_UserPreferences)
			return;

		const std::filesystem::path normalizedPath = NormalizeProjectPath(projectPath);
		if (normalizedPath.empty())
			return;

		for (auto it = m_UserPreferences->RecentProjects.begin(); it != m_UserPreferences->RecentProjects.end();)
		{
			if (NormalizeProjectPath(it->second.FilePath) == normalizedPath)
				it = m_UserPreferences->RecentProjects.erase(it);
			else
				++it;
		}

		RecentProject entry;
		entry.Name = normalizedPath.stem().string();
		if (Ref<Project> activeProject = Project::GetActive())
		{
			if (NormalizeProjectPath(activeProject->GetProjectFilePath()) == normalizedPath && !activeProject->GetConfig().Name.empty())
				entry.Name = activeProject->GetConfig().Name;
		}
		entry.FilePath = normalizedPath.generic_string();
		entry.LastOpened = time(nullptr);
		while (m_UserPreferences->RecentProjects.contains(entry.LastOpened))
			entry.LastOpened--;

		m_UserPreferences->RecentProjects[entry.LastOpened] = entry;

		while (m_UserPreferences->RecentProjects.size() > s_MaxRecentProjects)
		{
			auto last = std::prev(m_UserPreferences->RecentProjects.end());
			m_UserPreferences->RecentProjects.erase(last);
		}

		SaveUserPreferences();
	}

	std::vector<RecentProject> EditorLayer::GetRecentProjects() const
	{
		std::vector<RecentProject> projects;
		if (!m_UserPreferences)
			return projects;

		projects.reserve((size_t)std::min((int)m_UserPreferences->RecentProjects.size(), s_MaxRecentProjects));
		for (const auto& [_, project] : m_UserPreferences->RecentProjects)
		{
			if ((int)projects.size() >= s_MaxRecentProjects)
				break;

			projects.emplace_back(project);
		}

		return projects;
	}

	std::filesystem::path EditorLayer::GetStartupProjectPath() const
	{
		if (!m_UserPreferences)
			return {};

		if (!m_UserPreferences->StartupProject.empty())
			return NormalizeProjectPath(m_UserPreferences->StartupProject);

		if (!ShouldAutoOpenMostRecentProject())
			return {};

		for (const auto& [_, project] : m_UserPreferences->RecentProjects)
		{
			const std::filesystem::path path = NormalizeProjectPath(project.FilePath);
			std::error_code ec;
			if (!path.empty() && std::filesystem::exists(path, ec) && !ec)
				return path;
		}

		return {};
	}

	void EditorLayer::NewProject()
	{
		Project::New();
		m_PanelManager->OnProjectChanged(Project::GetActive());
		NewScene();
	}

	void EditorLayer::OpenProject(const std::filesystem::path& path)
	{
		if (Project::Load(path))
		{
			AddRecentProject(path);
			AssetHandle startScene = Project::GetActive()->GetConfig().StartSceneHandle;
			if (startScene)
				OpenScene(startScene);
			m_PanelManager->OnProjectChanged(Project::GetActive());
			if (m_SceneRenderer)
				m_SceneRenderer->ApplyProjectSettings(Project::GetActive()->GetConfig().SceneRenderer);
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
		if (!Project::GetActive())
			return;

		std::filesystem::path projectFilePath = Project::GetActive()->GetProjectFilePath();
		if (projectFilePath.empty())
		{
			std::string filepath = FileDialogs::SaveFile("Lux Project (*.luxproj)\0*.luxproj\0");
			if (filepath.empty())
				return;

			projectFilePath = filepath;
		}

		if (Project::SaveActive(projectFilePath))
			AddRecentProject(projectFilePath);
	}

	void EditorLayer::SyncRuntimeExportWindowFromProject()
	{
		Ref<Project> project = Project::GetActive();
		if (!project)
			return;

		auto& runtime = project->GetConfig().RuntimeExport;
		const std::string gameName = runtime.GameName.empty() ? project->GetConfig().Name : runtime.GameName;
		std::memset(m_RuntimeExportGameNameBuffer, 0, sizeof(m_RuntimeExportGameNameBuffer));
		std::memcpy(m_RuntimeExportGameNameBuffer, gameName.data(), std::min(gameName.size(), sizeof(m_RuntimeExportGameNameBuffer) - 1));
		m_RuntimeExportIcon = runtime.IconHandle;
	}

	void EditorLayer::RenderRuntimeExportWindow()
	{
		if (!m_ShowRuntimeExportWindow)
			return;

		Ref<Project> project = Project::GetActive();
		if (!project)
		{
			m_ShowRuntimeExportWindow = false;
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
		if (!ImGui::Begin("Export Runtime", &m_ShowRuntimeExportWindow, ImGuiWindowFlags_NoCollapse))
		{
			ImGui::End();
			return;
		}

		auto& config = project->GetConfig();
		auto& runtime = config.RuntimeExport;

		auto syncRuntimeIconPath = [&runtime](AssetHandle iconHandle)
		{
			runtime.IconHandle = iconHandle;
			runtime.IconPath.clear();

			if (!iconHandle)
				return;

			if (Ref<EditorAssetManager> editorAssetManager = Project::GetEditorAssetManager())
			{
				const AssetMetadata metadata = editorAssetManager->GetMetadata(iconHandle);
				if (metadata.IsValid())
					runtime.IconPath = metadata.FilePath.generic_string();
			}
		};

		ImGuiEx::BeginPropertyGrid();
		if (ImGuiEx::Property("Game Name", m_RuntimeExportGameNameBuffer, sizeof(m_RuntimeExportGameNameBuffer)))
			runtime.GameName = m_RuntimeExportGameNameBuffer;

		int32_t width = (int32_t)runtime.WindowWidth;
		if (ImGuiEx::Property("Window Width", width, 320, 16384))
			runtime.WindowWidth = (uint32_t)std::max(width, 320);

		int32_t height = (int32_t)runtime.WindowHeight;
		if (ImGuiEx::Property("Window Height", height, 240, 16384))
			runtime.WindowHeight = (uint32_t)std::max(height, 240);

		ImGuiEx::Property("Fullscreen", runtime.Fullscreen);
		ImGuiEx::Property("VSync", runtime.VSync);

		AssetHandle icon = runtime.IconHandle;
		ImGuiEx::PropertyAssetReferenceSettings iconSettings;
		iconSettings.ShowFullFilePath = true;
		if (ImGuiEx::PropertyAssetReference<Texture2D>("Icon", icon, "Optional PNG/JPG window icon copied beside exported runtime resources.", nullptr, iconSettings))
		{
			m_RuntimeExportIcon = icon;
			syncRuntimeIconPath(icon);
		}
		ImGuiEx::EndPropertyGrid();

		if (ImGui::BeginCombo("Target Config", RuntimeExportTargetToString(runtime.TargetConfig)))
		{
			for (RuntimeExportTarget target : s_RuntimeExportTargets)
			{
				const bool selected = runtime.TargetConfig == target;
				if (ImGui::Selectable(RuntimeExportTargetToString(target), selected))
					runtime.TargetConfig = target;
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::Spacing();
		ImGui::TextUnformatted("Export Preflight");
		ImGui::Separator();

		auto drawStatus = [](const char* label, bool ok, const std::string& okText, const char* failText)
		{
			ImGui::TextColored(ok ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f) : ImVec4(0.95f, 0.55f, 0.35f, 1.0f),
				"%s: %s", label, ok ? okText.c_str() : failText);
		};

		std::error_code ec;
		std::filesystem::path repositoryRoot = FindRepositoryRootFrom(project->GetProjectDirectory());
		if (repositoryRoot.empty())
			repositoryRoot = FindRepositoryRootFrom(std::filesystem::current_path());

		const std::filesystem::path current = std::filesystem::current_path(ec);
		const std::filesystem::path runtimeExe = GetRuntimeExecutablePath(runtime.TargetConfig);
		const std::filesystem::path assetPack = Project::GetActiveAssetDirectory() / s_RuntimeAssetPackFile;
		const std::filesystem::path resources = FindFirstExistingDirectory({
			current / "Resources",
			current / ".." / "Editor" / "Resources",
			repositoryRoot / "Editor" / "Resources",
			std::filesystem::path("Editor") / "Resources"
		});
		const std::filesystem::path dotnet = FindFirstExistingDirectory({
			current / "DotNet",
			current / ".." / "Editor" / "DotNet",
			repositoryRoot / "Editor" / "DotNet",
			std::filesystem::path("Editor") / "DotNet"
		});
		const std::filesystem::path scriptModule = Project::GetActiveScriptModuleFilePath();
		const std::filesystem::path scriptProject = ResolveScriptProjectFile(project);
		const bool startupSceneUsesScripts = StartupSceneUsesScripts(project);
		const bool scriptModuleExists = config.ScriptModulePath.empty() || FileExists(scriptModule);
		const bool scriptModuleStale = !config.ScriptModulePath.empty() && scriptModuleExists && IsScriptModuleOutdated(scriptModule, scriptProject);

		drawStatus("Startup Scene", config.StartSceneHandle != 0, "set", "missing");
		drawStatus("Startup Scene Uses Scripts", true, startupSceneUsesScripts ? "yes" : "no", "");
		drawStatus("Lux-Runtime.exe", !runtimeExe.empty(), runtimeExe.string(), "missing");
		drawStatus("AssetPack.lap", std::filesystem::exists(assetPack, ec), "created", "will be created during export");
		drawStatus("Resources", !resources.empty(), resources.string(), "missing");
		if (config.ScriptModulePath.empty())
			ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "Script Module: optional");
		else if (!scriptModuleExists)
			ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f), "Script Module: missing");
		else
			ImGui::TextColored(scriptModuleStale ? ImVec4(0.95f, 0.75f, 0.35f, 1.0f) : ImVec4(0.35f, 0.85f, 0.45f, 1.0f),
				"Script Module: %s", scriptModuleStale ? "stale" : "found");
		drawStatus("DotNet", !dotnet.empty(), dotnet.string(), "missing");

		ImGui::Spacing();
		if (ImGui::Button("Build Runtime"))
			BuildRuntimeExecutable(runtime.TargetConfig);

		ImGui::SameLine();
		if (ImGui::Button("Build Scripts"))
			BuildScriptModule(runtime.TargetConfig);

		ImGui::Separator();
		if (ImGui::Button("Export..."))
		{
			if (ExportRuntimeNow())
				m_ShowRuntimeExportWindow = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			m_ShowRuntimeExportWindow = false;

		ImGui::End();
	}

	void EditorLayer::ExportRuntime()
	{
		if (!Project::GetActive())
		{
			LUX_CONSOLE_LOG_ERROR("No active project to export.");
			return;
		}

		SyncRuntimeExportWindowFromProject();
		m_ShowRuntimeExportWindow = true;
	}

	bool EditorLayer::ExportRuntimeNow()
	{
		Ref<Project> project = Project::GetActive();
		if (!project)
		{
			LUX_CONSOLE_LOG_ERROR("No active project to export.");
			return false;
		}

		if (m_SceneState != SceneState::Edit)
			OnSceneStop();

		SaveScene();
		SaveProject();

		const std::filesystem::path selectedFolder = FileSystem::OpenFolderDialog(project->GetProjectDirectory().string().c_str());
		if (selectedFolder.empty())
			return false;

		std::error_code ec;
		ProjectRuntimeExportSettings runtimeSettings = project->GetConfig().RuntimeExport;
		if (runtimeSettings.GameName.empty())
			runtimeSettings.GameName = project->GetConfig().Name;
		runtimeSettings.WindowWidth = std::max<uint32_t>(runtimeSettings.WindowWidth, 320);
		runtimeSettings.WindowHeight = std::max<uint32_t>(runtimeSettings.WindowHeight, 240);

		const RuntimeExportTarget targetConfig = runtimeSettings.TargetConfig;
		std::filesystem::path runtimeExe = GetRuntimeExecutablePath(targetConfig);
		std::filesystem::path repositoryRoot = FindRepositoryRootFrom(project->GetProjectDirectory());
		if (repositoryRoot.empty())
			repositoryRoot = FindRepositoryRootFrom(std::filesystem::current_path());

		if (runtimeExe.empty() || (!repositoryRoot.empty() && IsRuntimeExecutableOutdated(runtimeExe, repositoryRoot)))
		{
			LUX_CONSOLE_LOG_INFO("Lux-Runtime ({}) is missing or older than runtime sources. Building before export...", RuntimeExportTargetToString(targetConfig));
			if (!BuildRuntimeExecutable(targetConfig))
				return false;
			runtimeExe = GetRuntimeExecutablePath(targetConfig);
		}

		const std::filesystem::path scriptModule = Project::GetActiveScriptModuleFilePath();
		const std::filesystem::path scriptProject = ResolveScriptProjectFile(project);
		const bool startupSceneUsesScripts = StartupSceneUsesScripts(project);
		const bool scriptProjectExists = FileExists(scriptProject);
		if (!project->GetConfig().ScriptModulePath.empty() && scriptProjectExists && IsScriptModuleOutdated(scriptModule, scriptProject))
		{
			LUX_CONSOLE_LOG_INFO("Script module is missing or stale. Building scripts before asset pack creation...");
			if (!BuildScriptModule(targetConfig) && startupSceneUsesScripts)
				return false;
		}

		const std::filesystem::path current = std::filesystem::current_path(ec);
		const std::filesystem::path resourcesSource = FindFirstExistingDirectory({
			current / "Resources",
			current / ".." / "Editor" / "Resources",
			repositoryRoot / "Editor" / "Resources",
			std::filesystem::path("Editor") / "Resources"
		});
		const std::filesystem::path dotnetSource = FindFirstExistingDirectory({
			current / "DotNet",
			current / ".." / "Editor" / "DotNet",
			repositoryRoot / "Editor" / "DotNet",
			std::filesystem::path("Editor") / "DotNet"
		});
		const bool hasStartupScene = project->GetConfig().StartSceneHandle != 0;
		const bool hasRuntimeExe = FileExists(runtimeExe);
		const bool hasResources = !resourcesSource.empty();
		const bool hasDotNet = !dotnetSource.empty();
		const bool hasScriptModule = project->GetConfig().ScriptModulePath.empty() || FileExists(scriptModule);
		const bool scriptModuleStale = !project->GetConfig().ScriptModulePath.empty() && hasScriptModule && IsScriptModuleOutdated(scriptModule, scriptProject);

		LUX_CONSOLE_LOG_INFO("Runtime export preflight:");
		LUX_CONSOLE_LOG_INFO("  Startup Scene: {}", hasStartupScene ? "set" : "missing");
		LUX_CONSOLE_LOG_INFO("  Startup Scene Uses Scripts: {}", startupSceneUsesScripts ? "yes" : "no");
		LUX_CONSOLE_LOG_INFO("  {}: {}", RuntimeExport::RuntimeExeName, hasRuntimeExe ? runtimeExe.string() : "missing");
		LUX_CONSOLE_LOG_INFO("  Resources: {}", hasResources ? resourcesSource.string() : "missing");
		if (project->GetConfig().ScriptModulePath.empty())
			LUX_CONSOLE_LOG_INFO("  Script Module: optional");
		else if (!hasScriptModule)
			LUX_CONSOLE_LOG_INFO("  Script Module: missing ({})", scriptModule.string());
		else
			LUX_CONSOLE_LOG_INFO("  Script Module: {} ({})", scriptModuleStale ? "stale" : "found", scriptModule.string());
		LUX_CONSOLE_LOG_INFO("  DotNet: {}", hasDotNet ? dotnetSource.string() : "missing");

		if (!hasStartupScene || !hasRuntimeExe || !hasResources)
		{
			LUX_CONSOLE_LOG_ERROR("Runtime export preflight failed. Set a Startup Scene and make sure Lux-Runtime and Resources are available.");
			return false;
		}

		if (startupSceneUsesScripts && (!hasScriptModule || scriptModuleStale))
		{
			LUX_CONSOLE_LOG_ERROR("Runtime export preflight failed. Startup Scene uses scripts, but the script module is {}.", hasScriptModule ? "stale" : "missing");
			return false;
		}

		if (!hasScriptModule)
			LUX_CONSOLE_LOG_WARN("Script module is missing. Export will continue because the Startup Scene does not use scripts.");
		if (!hasDotNet)
			LUX_CONSOLE_LOG_WARN("DotNet directory (Coral.Managed) is missing. Export will continue, but scripting will not run.");

		const std::string buildName = SanitizeBuildName(runtimeSettings.GameName.empty() ? project->GetConfig().Name : runtimeSettings.GameName);
		const std::filesystem::path exportRoot = selectedFolder / (buildName + RuntimeExport::PlatformExportLabel);
		const std::filesystem::path exportAssets = exportRoot / "Assets";

		std::filesystem::create_directories(exportAssets, ec);
		if (ec)
		{
			LUX_CONSOLE_LOG_ERROR("Failed to create export directory '{}': {}", exportRoot.string(), ec.message());
			return false;
		}

		std::atomic<float> assetPackProgress = 0.0f;
		Ref<AssetPack> assetPack = AssetPack::CreateFromActiveProject(assetPackProgress);
		if (!assetPack)
		{
			LUX_CONSOLE_LOG_ERROR("Runtime export failed while building the asset pack.");
			return false;
		}

		ProjectSerializer serializer(project);
		const std::filesystem::path runtimeProjectFile = exportAssets / s_RuntimeProjectFile;
		if (!serializer.SerializeRuntime(runtimeProjectFile))
		{
			LUX_CONSOLE_LOG_ERROR("Runtime export failed while writing '{}'.", runtimeProjectFile.string());
			return false;
		}

		if (!CopyFileIfExists(Project::GetActiveAssetDirectory() / s_RuntimeAssetPackFile, exportAssets / s_RuntimeAssetPackFile, true))
			return false;
		LUX_CONSOLE_LOG_INFO("  AssetPack.lap: created");

#ifdef LUX_PLATFORM_LINUX
		const std::filesystem::path exportedExe = exportRoot / buildName;
#else
		const std::filesystem::path exportedExe = exportRoot / (buildName + ".exe");
#endif
		if (!CopyFileIfExists(runtimeExe, exportedExe, true))
			LUX_CONSOLE_LOG_WARN("Build the Lux-Runtime project once before exporting a standalone executable.");

		const std::filesystem::path runtimeDirectory = runtimeExe.parent_path();
		if (!runtimeDirectory.empty() && std::filesystem::exists(runtimeDirectory, ec))
		{
#ifdef LUX_PLATFORM_LINUX
			const std::filesystem::path libDir = exportRoot / "lib";
			std::filesystem::create_directories(libDir, ec);
			for (const auto& entry : std::filesystem::directory_iterator(runtimeDirectory, ec))
			{
				if (!entry.is_regular_file(ec))
					continue;

				const std::string filename = entry.path().filename().string();
				if (filename.find(".so") != std::string::npos)
					CopyFileIfExists(entry.path(), libDir / entry.path().filename());
			}
#else
			for (const auto& entry : std::filesystem::directory_iterator(runtimeDirectory, ec))
			{
				if (!entry.is_regular_file(ec))
					continue;

				const std::filesystem::path extension = entry.path().extension();
				if (extension == ".dll" || (extension == ".pdb" && targetConfig != RuntimeExportTarget::Dist))
					CopyFileIfExists(entry.path(), exportRoot / entry.path().filename());
			}
#endif
		}

		if (!resourcesSource.empty())
			CopyDirectoryRecursive(resourcesSource, exportRoot / "Resources", targetConfig == RuntimeExportTarget::Dist);
		else
			LUX_CONSOLE_LOG_WARN("Runtime export could not find an editor Resources directory to copy.");

		if (!WriteRuntimeShaderPack(exportRoot / "Assets" / s_RuntimeShaderPackFile))
			return false;

		if (!dotnetSource.empty())
			CopyDirectoryRecursive(dotnetSource, exportRoot / "DotNet", targetConfig == RuntimeExportTarget::Dist);

		if (std::filesystem::exists(scriptModule, ec))
			CopyFileIfExists(scriptModule, exportAssets / project->GetConfig().ScriptModulePath);

		std::filesystem::path runtimeIconPath;
		if (std::filesystem::path iconSource = ResolveRuntimeIconSource(runtimeSettings); !iconSource.empty())
		{
			if (FileExists(iconSource))
			{
				const std::filesystem::path iconFilename = "Icon" + iconSource.extension().string();
				const std::filesystem::path exportedIconPath = exportRoot / "Resources" / "Runtime" / iconFilename;
				if (CopyFileIfExists(iconSource, exportedIconPath))
					runtimeIconPath = std::filesystem::path("..") / "Resources" / "Runtime" / iconFilename;
			}
			else
			{
				LUX_CONSOLE_LOG_WARN("Configured runtime icon is missing: {}", iconSource.string());
			}
		}

		if (!WriteRuntimeSettingsFile(exportAssets / "RuntimeSettings.yaml", runtimeSettings, runtimeIconPath))
			return false;

#ifdef LUX_PLATFORM_LINUX
		std::filesystem::permissions(exportedExe,
			std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
			std::filesystem::perm_options::add, ec);

		{
			const std::filesystem::path launchScript = exportRoot / ("run-" + buildName + ".sh");
			std::ofstream script(launchScript);
			if (script.is_open())
			{
				script << "#!/bin/sh\n";
				script << "SCRIPT_DIR=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n";
				script << "export LD_LIBRARY_PATH=\"$SCRIPT_DIR/lib:$LD_LIBRARY_PATH\"\n";
				script << "exec \"$SCRIPT_DIR/" << buildName << "\" \"$@\"\n";
				script.close();
				std::filesystem::permissions(launchScript,
					std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
					std::filesystem::perm_options::add, ec);
			}
		}

		{
			const std::filesystem::path desktopFile = exportRoot / (buildName + ".desktop");
			std::ofstream desktop(desktopFile);
			if (desktop.is_open())
			{
				desktop << "[Desktop Entry]\n";
				desktop << "Type=Application\n";
				desktop << "Name=" << runtimeSettings.GameName << "\n";
				desktop << "Exec=run-" << buildName << ".sh\n";
				if (!runtimeIconPath.empty())
					desktop << "Icon=" << (exportRoot / runtimeIconPath).string() << "\n";
				desktop << "Terminal=false\n";
				desktop << "Categories=Game;\n";
			}
		}
#endif

		LUX_CONSOLE_LOG_INFO("Runtime export complete: {}", exportRoot.string());
		FileSystem::OpenDirectoryInExplorer(exportRoot);
		return true;
	}

	void EditorLayer::NewScene()
	{
		m_EditorScene = CreateRef<Scene>();
		m_ActiveScene = m_EditorScene;
		m_PanelManager->SetSceneContext(m_ActiveScene);
		m_EditorScenePath = std::filesystem::path();
		ResetUndoHistory();
		m_EntityBookmarks.clear();   // bookmarks are per-scene UUIDs

		if (m_EditorViewport)
		{
			m_EditorViewport->SetScene(m_ActiveScene);
			m_EditorViewport->SyncSceneViewport(m_ActiveScene);
			m_Framebuffer = m_EditorViewport->GetFramebuffer();
			m_SceneRenderer = m_EditorViewport->GetSceneRenderer();
		}

		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetContext(m_SceneRenderer);
		if (m_RendererDebuggerPanel)
			m_RendererDebuggerPanel->SetContext(m_SceneRenderer);
		if (m_ProfilerPanel)
			m_ProfilerPanel->SetContext(m_SceneRenderer);
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
		if (!readOnlyScene)
		{
			LUX_CORE_ERROR("Failed to open scene asset {0}; scene could not be loaded or uses an incompatible schema.", handle);
			return;
		}

		Ref<Scene> newScene = Scene::Copy(readOnlyScene);
		if (!newScene)
		{
			LUX_CORE_ERROR("Failed to open scene asset {0}; scene copy failed.", handle);
			return;
		}

		ApplyEditorScene(newScene, Project::GetActive()->GetEditorAssetManager()->GetFilePath(handle));
	}

	void EditorLayer::ApplyEditorScene(Ref<Scene> scene, const std::filesystem::path& path)
	{
		m_EditorScene = scene;
		m_ActiveScene = m_EditorScene;
		m_PanelManager->SetSceneContext(m_EditorScene);
		m_EditorScenePath = path;

		if (m_EditorViewport)
		{
			m_EditorViewport->SetScene(m_ActiveScene);
			m_EditorViewport->SyncSceneViewport(m_ActiveScene);
			m_Framebuffer = m_EditorViewport->GetFramebuffer();
			m_SceneRenderer = m_EditorViewport->GetSceneRenderer();
		}

		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetContext(m_SceneRenderer);
		if (m_RendererDebuggerPanel)
			m_RendererDebuggerPanel->SetContext(m_SceneRenderer);
		if (m_ProfilerPanel)
			m_ProfilerPanel->SetContext(m_SceneRenderer);

		ResetUndoHistory();
		m_EntityBookmarks.clear();   // bookmarks are per-scene UUIDs
	}

	void EditorLayer::EnterPrefabEditMode(AssetHandle prefabHandle)
	{
		if (m_PrefabEditMode)   // no nesting for now
			return;
		if (m_SceneState != SceneState::Edit)
			OnSceneStop();

		Ref<Prefab> prefab = AssetManager::GetAsset<Prefab>(prefabHandle);
		if (!prefab || !prefab->GetScene())
		{
			LUX_CORE_ERROR("Cannot edit prefab {0}: it could not be loaded.", prefabHandle);
			return;
		}

		// Remember where to return, then edit a copy of the prefab's scene (Scene::Copy preserves
		// UUIDs, so the instances' PrefabComponent links survive the save round-trip).
		m_PreFocusScene = m_EditorScene;
		m_PreFocusScenePath = m_EditorScenePath;

		m_PrefabEditMode = true;
		m_EditingPrefabHandle = prefabHandle;
		m_EditingPrefabName = Project::GetEditorAssetManager()->GetMetadata(prefabHandle).FilePath.stem().string();

		ApplyEditorScene(Scene::Copy(prefab->GetScene()), {});
	}

	void EditorLayer::SavePrefabEdits()
	{
		if (!m_PrefabEditMode)
			return;

		Ref<Prefab> prefab = AssetManager::GetAsset<Prefab>(m_EditingPrefabHandle);
		if (!prefab)
			return;

		// Propagate before reload, while the cached prefab still holds the pre-edit (old) values.
		if (m_PreFocusScene && prefab->GetScene())
			m_PreFocusScene->PropagatePrefabEdits(m_EditingPrefabHandle, prefab->GetScene(), m_EditorScene);

		const std::filesystem::path path = Project::GetEditorAssetManager()->GetFileSystemPath(m_EditingPrefabHandle);
		SceneSerializer(m_EditorScene).Serialize(path);
		AssetManager::ReloadData(m_EditingPrefabHandle);   // cache now reflects the saved prefab
		LUX_CONSOLE_LOG_INFO("Saved prefab '{}'", m_EditingPrefabName);
	}

	void EditorLayer::ExitPrefabEditMode(bool save)
	{
		if (!m_PrefabEditMode)
			return;

		if (save)
			SavePrefabEdits();

		Ref<Scene> returnScene = m_PreFocusScene;
		const std::filesystem::path returnPath = m_PreFocusScenePath;

		m_PrefabEditMode = false;
		m_EditingPrefabHandle = 0;
		m_EditingPrefabName.clear();
		m_PreFocusScene = nullptr;
		m_PreFocusScenePath.clear();

		if (returnScene)
			ApplyEditorScene(returnScene, returnPath);
	}

	std::map<UUID, std::string> EditorLayer::CaptureSceneEntities(const Ref<Scene>& scene, std::string& outMeta) const
	{
		outMeta.clear();
		if (!scene)
			return {};

		SceneSerializer serializer(scene);
		return serializer.SerializeEntitySnapshots(outMeta);
	}

	void EditorLayer::ResetUndoHistory()
	{
		m_UndoStack.clear();
		m_RedoStack.clear();
		m_UndoCommitPending = false;
		m_PendingUndoLabel = "Edit";
		m_BaselineEntities = CaptureSceneEntities(m_EditorScene, m_BaselineMeta);
		CaptureRendererSettingsBaseline();
	}

	void EditorLayer::CommitSceneSnapshot()
	{
		if (!m_EditorScene)
			return;

		std::string meta;
		std::map<UUID, std::string> current = CaptureSceneEntities(m_EditorScene, meta);

		// Diff the current scene against the committed baseline; the step records only what changed.
		UndoCommand command;
		command.Label = m_PendingUndoLabel;

		if (meta != m_BaselineMeta)
		{
			command.MetaChanged = true;
			command.MetaBefore = m_BaselineMeta;
			command.MetaAfter = meta;
		}

		// Entities present in the baseline: changed value, or removed (absent from current).
		for (const auto& [handle, before] : m_BaselineEntities)
		{
			auto it = current.find(handle);
			const std::string after = (it != current.end()) ? it->second : std::string();
			if (before != after)
				command.Entities.push_back({ handle, before, after });
		}
		// Entities newly created (absent from the baseline).
		for (const auto& [handle, after] : current)
		{
			if (m_BaselineEntities.find(handle) == m_BaselineEntities.end())
				command.Entities.push_back({ handle, std::string(), after });
		}

		// No-op: an edit signalled from a non-scene panel left the scene YAML unchanged.
		if (!command.MetaChanged && command.Entities.empty())
			return;

		command.ApproxBytes = command.MetaBefore.size() + command.MetaAfter.size();
		for (const EntityDelta& delta : command.Entities)
			command.ApproxBytes += delta.Before.size() + delta.After.size();

		m_UndoStack.push_back(std::move(command));
		TrimUndoStack(m_UndoStack);
		m_RedoStack.clear();

		m_BaselineMeta = std::move(meta);
		m_BaselineEntities = std::move(current);
	}

	void EditorLayer::PushUndoCommand(const std::string& label, std::function<void()> undo, std::function<void()> redo)
	{
		UndoCommand command;
		command.Label = label;
		command.CustomUndo = std::move(undo);
		command.CustomRedo = std::move(redo);
		command.ApproxBytes = 2 * sizeof(ProjectSceneRendererSettings);   // non-scene commands are small

		m_UndoStack.push_back(std::move(command));
		TrimUndoStack(m_UndoStack);
		m_RedoStack.clear();
	}

	void EditorLayer::TrimUndoStack(std::vector<UndoCommand>& stack)
	{
		size_t totalBytes = 0;
		for (const UndoCommand& command : stack)
			totalBytes += command.ApproxBytes;

		// Evict oldest steps until under both the step count and the byte budget — but never drop the
		// last one, so even a single oversized step stays undoable once.
		while (stack.size() > 1 && (stack.size() > s_MaxUndoDepth || totalBytes > s_MaxUndoBytes))
		{
			totalBytes -= stack.front().ApproxBytes;
			stack.erase(stack.begin());
		}
	}

	ProjectSceneRendererSettings EditorLayer::CaptureRendererSettings() const
	{
		ProjectSceneRendererSettings settings;
		std::memset(&settings, 0, sizeof(settings));   // zero padding so memcmp comparisons are reliable
		if (m_SceneRenderer)
			m_SceneRenderer->WriteProjectSettings(settings);
		return settings;
	}

	void EditorLayer::CaptureRendererSettingsBaseline()
	{
		m_RendererSettingsBaseline = CaptureRendererSettings();
	}

	void EditorLayer::CommitRendererSettings()
	{
		if (!m_SceneRenderer)
			return;

		const ProjectSceneRendererSettings current = CaptureRendererSettings();
		if (std::memcmp(&current, &m_RendererSettingsBaseline, sizeof(ProjectSceneRendererSettings)) == 0)
			return;   // renderer/project settings unchanged (e.g. the edit was a scene edit)

		const ProjectSceneRendererSettings before = m_RendererSettingsBaseline;
		const ProjectSceneRendererSettings after = current;
		PushUndoCommand("Renderer Settings",
			[this, before]() { ApplyRendererSettings(before); },
			[this, after]() { ApplyRendererSettings(after); });

		m_RendererSettingsBaseline = current;
	}

	void EditorLayer::ApplyRendererSettings(const ProjectSceneRendererSettings& settings)
	{
		if (!m_SceneRenderer)
			return;

		m_SceneRenderer->ApplyProjectSettings(settings);   // canonical apply + refresh (same as project load)
		if (Ref<Project> project = Project::GetActive())
			m_SceneRenderer->WriteProjectSettings(project->GetConfig().SceneRenderer);   // keep project config in sync

		m_RendererSettingsBaseline = settings;   // baseline now matches the restored state
	}

	void EditorLayer::PollSceneEditForUndo()
	{
		if (EditorStack::Get().ConsumeSceneEdit())
		{
			m_UndoCommitPending = true;
			m_PendingUndoLabel = EditorStack::Get().ConsumeLabel();
		}

		// Defer the commit until the active edit finishes (mouse released, field deactivated), so a
		// continuous drag collapses into a single undo step. In Edit mode this records the edit-mode
		// history; in Play/Simulate it records the transient runtime history instead.
		if (m_UndoCommitPending && !ImGui::IsAnyItemActive())
		{
			if (m_SceneState == SceneState::Edit)
			{
				CommitSceneSnapshot();       // scene entities / meta
				CommitRendererSettings();    // renderer/project settings (non-scene)
			}
			else
			{
				CommitPlaySnapshot();        // transient runtime-scene history
			}
			m_UndoCommitPending = false;
		}
	}

	void EditorLayer::UndoSceneEdit()
	{
		std::vector<UndoCommand>& undoStack = ActiveUndoStack();   // edit or play stack, per SceneState
		std::vector<UndoCommand>& redoStack = ActiveRedoStack();
		if (undoStack.empty())
			return;

		UndoCommand command = std::move(undoStack.back());
		undoStack.pop_back();

		if (command.CustomUndo)
		{
			// Non-scene command (renderer settings) or a play-mode step — it restores its own state.
			command.CustomUndo();
		}
		else
		{
			std::vector<UUID> affected;
			affected.reserve(command.Entities.size());
			for (const EntityDelta& delta : command.Entities)
				affected.push_back(delta.Handle);

			// Roll the baseline back to the "before" side of this step, then rebuild the scene from it.
			if (command.MetaChanged)
				m_BaselineMeta = command.MetaBefore;
			for (const EntityDelta& delta : command.Entities)
			{
				if (delta.Before.empty())
					m_BaselineEntities.erase(delta.Handle);        // entity was created by the edit -> remove it
				else
					m_BaselineEntities[delta.Handle] = delta.Before;
			}

			RestoreSceneState(m_BaselineMeta, m_BaselineEntities);
			RestoreSelection(affected);
		}

		m_UndoToastText = "Undo: " + command.Label;
		m_UndoToastTime = ImGui::GetTime();
		redoStack.push_back(std::move(command));
	}

	void EditorLayer::RedoSceneEdit()
	{
		std::vector<UndoCommand>& undoStack = ActiveUndoStack();
		std::vector<UndoCommand>& redoStack = ActiveRedoStack();
		if (redoStack.empty())
			return;

		UndoCommand command = std::move(redoStack.back());
		redoStack.pop_back();

		if (command.CustomRedo)
		{
			command.CustomRedo();
		}
		else
		{
			std::vector<UUID> affected;
			affected.reserve(command.Entities.size());
			for (const EntityDelta& delta : command.Entities)
				affected.push_back(delta.Handle);

			if (command.MetaChanged)
				m_BaselineMeta = command.MetaAfter;
			for (const EntityDelta& delta : command.Entities)
			{
				if (delta.After.empty())
					m_BaselineEntities.erase(delta.Handle);        // entity was deleted by the edit -> remove it
				else
					m_BaselineEntities[delta.Handle] = delta.After;
			}

			RestoreSceneState(m_BaselineMeta, m_BaselineEntities);
			RestoreSelection(affected);
		}

		m_UndoToastText = "Redo: " + command.Label;
		m_UndoToastTime = ImGui::GetTime();
		undoStack.push_back(std::move(command));
	}

	void EditorLayer::RestoreSceneState(const std::string& meta, const std::map<UUID, std::string>& entities)
	{
		std::vector<std::string> blocks;
		blocks.reserve(entities.size());
		for (const auto& [handle, block] : entities)
			blocks.push_back(block);

		Ref<Scene> newScene = CreateRef<Scene>();
		SceneSerializer serializer(newScene);
		if (!serializer.DeserializeFromSnapshots(meta, blocks))
		{
			LUX_CORE_ERROR("Undo: failed to restore scene state; history left unchanged.");
			return;
		}

		AdoptEditorScene(newScene);

		// The restore itself is not a user edit — clear any signal/pending it might have raised.
		m_UndoCommitPending = false;
		EditorStack::Get().ConsumeSceneEdit();
	}

	void EditorLayer::RestoreSelection(const std::vector<UUID>& handles)
	{
		// Select the entities the undone/redone step touched (that still exist), so the user sees
		// what changed. AdoptEditorScene cleared the selection when it swapped the scene.
		SelectionManager::DeselectAll(SelectionContext::Scene);
		if (!m_EditorScene)
			return;

		for (UUID handle : handles)
		{
			if (m_EditorScene->TryGetEntityWithUUID(handle))
				SelectionManager::Select(SelectionContext::Scene, handle);
		}
	}

	void EditorLayer::UI_UndoToast()
	{
		constexpr double kVisibleSeconds = 1.6;
		constexpr double kFadeSeconds = 0.5;

		const double age = ImGui::GetTime() - m_UndoToastTime;
		if (m_UndoToastText.empty() || age < 0.0 || age > kVisibleSeconds + kFadeSeconds)
			return;

		const float alpha = age <= kVisibleSeconds ? 1.0f : 1.0f - static_cast<float>((age - kVisibleSeconds) / kFadeSeconds);

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const ImVec2 position(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y - 48.0f);
		ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.9f);

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;

		ImGuiEx::ScopedStyle fade(ImGuiStyleVar_Alpha, alpha);   // fades text + background uniformly
		ImGuiEx::ScopedStyle rounding(ImGuiStyleVar_WindowRounding, 6.0f);
		ImGuiEx::ScopedStyle padding(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 8.0f));
		ImGuiEx::ScopedColour text(ImGuiCol_Text, Colors::Theme::textBrighter);
		if (ImGui::Begin("##UndoToast", nullptr, flags))
			ImGui::TextUnformatted(m_UndoToastText.c_str());
		ImGui::End();
	}

	void EditorLayer::ReloadScriptsWithFeedback()
	{
		if (!Project::GetActive())
			return;

		Project::GetActive()->ReloadScriptEngine();
		if (m_ActiveScene)
			m_ActiveScene->GetScriptStorage().SynchronizeStorage();

		const ScriptEngine::ReloadStatus& status = ScriptEngine::GetInstance().GetLastReloadStatus();
		if (status.Success)
			m_ScriptToastText = std::format("{}  C# reloaded  ·  {} script{}", LUX_ICON_CHECK, status.ScriptCount, status.ScriptCount == 1 ? "" : "s");
		else
			m_ScriptToastText = std::format("{}  C# reload failed: {}", LUX_ICON_TIMES, status.Message);

		m_ScriptToastColor = status.Success ? Colors::Theme::titlebarGreen : Colors::Theme::titlebarRed;
		m_ScriptToastTime = ImGui::GetTime();
	}

	void EditorLayer::UI_ScriptToast()
	{
		constexpr double kVisibleSeconds = 2.6;   // a touch longer than the undo toast so errors register
		constexpr double kFadeSeconds = 0.5;

		const double age = ImGui::GetTime() - m_ScriptToastTime;
		if (m_ScriptToastText.empty() || age < 0.0 || age > kVisibleSeconds + kFadeSeconds)
			return;

		const float alpha = age <= kVisibleSeconds ? 1.0f : 1.0f - static_cast<float>((age - kVisibleSeconds) / kFadeSeconds);

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		// Sits above the undo toast so the two never overlap.
		const ImVec2 position(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y - 84.0f);
		ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.9f);

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;

		ImGuiEx::ScopedStyle fade(ImGuiStyleVar_Alpha, alpha);
		ImGuiEx::ScopedStyle rounding(ImGuiStyleVar_WindowRounding, 6.0f);
		ImGuiEx::ScopedStyle padding(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 8.0f));
		ImGuiEx::ScopedColour text(ImGuiCol_Text, m_ScriptToastColor);
		if (ImGui::Begin("##ScriptToast", nullptr, flags))
			ImGui::TextUnformatted(m_ScriptToastText.c_str());
		ImGui::End();
	}

	void EditorLayer::UI_PrefabEditBanner()
	{
		if (!m_PrefabEditMode)
			return;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const ImVec2 position(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + 8.0f);
		ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
		ImGui::SetNextWindowBgAlpha(0.95f);

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_NoMove;

		ImGuiEx::ScopedStyle rounding(ImGuiStyleVar_WindowRounding, 6.0f);
		ImGuiEx::ScopedStyle padding(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 8.0f));
		if (ImGui::Begin("##PrefabEditBanner", nullptr, flags))
		{
			{
				ImGuiEx::ScopedColour accent(ImGuiCol_Text, Colors::Theme::accent);
				ImGui::TextUnformatted(LUX_ICON_CUBE "  Editing Prefab:");
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(m_EditingPrefabName.c_str());
			ImGui::SameLine();
			ImGui::Dummy(ImVec2(12.0f, 0.0f));
			ImGui::SameLine();
			if (ImGui::Button("Save & Return"))
				ExitPrefabEditMode(true);
			ImGui::SameLine();
			if (ImGui::Button("Discard & Return"))
				ExitPrefabEditMode(false);
		}
		ImGui::End();
	}

	void EditorLayer::AdoptEditorScene(const Ref<Scene>& scene)
	{
		m_EditorScene = scene;
		m_ActiveScene = m_EditorScene;
		m_PanelManager->SetSceneContext(m_EditorScene);

		if (m_EditorViewport)
		{
			m_EditorViewport->SetScene(m_ActiveScene);
			m_EditorViewport->SyncSceneViewport(m_ActiveScene);
			m_Framebuffer = m_EditorViewport->GetFramebuffer();
			m_SceneRenderer = m_EditorViewport->GetSceneRenderer();
		}

		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetContext(m_SceneRenderer);
		if (m_RendererDebuggerPanel)
			m_RendererDebuggerPanel->SetContext(m_SceneRenderer);
		if (m_ProfilerPanel)
			m_ProfilerPanel->SetContext(m_SceneRenderer);

		// Keep the settings baseline aligned with the (unchanged) renderer after a scene restore, so a
		// scene undo/redo never looks like a settings change.
		CaptureRendererSettingsBaseline();
	}

	void EditorLayer::BeginPlayUndoHistory()
	{
		m_PlayUndoStack.clear();
		m_PlayRedoStack.clear();
		m_UndoCommitPending = false;
		m_PlayBaselineEntities = CaptureSceneEntities(m_ActiveScene, m_PlayBaselineMeta);
	}

	void EditorLayer::CommitPlaySnapshot()
	{
		if (!m_ActiveScene)
			return;

		std::string meta;
		std::map<UUID, std::string> current = CaptureSceneEntities(m_ActiveScene, meta);

		const bool metaChanged = meta != m_PlayBaselineMeta;
		std::vector<EntityDelta> deltas;
		for (const auto& [handle, before] : m_PlayBaselineEntities)
		{
			auto it = current.find(handle);
			const std::string after = (it != current.end()) ? it->second : std::string();
			if (before != after)
				deltas.push_back({ handle, before, after });
		}
		for (const auto& [handle, after] : current)
		{
			if (m_PlayBaselineEntities.find(handle) == m_PlayBaselineEntities.end())
				deltas.push_back({ handle, std::string(), after });
		}

		if (!metaChanged && deltas.empty())
			return;

		const std::string metaBefore = m_PlayBaselineMeta;
		const std::string metaAfter = meta;
		m_PlayBaselineMeta = meta;
		m_PlayBaselineEntities = current;

		// A play step is a closure command over the play stacks: apply the before/after deltas to the
		// play baseline, then rebuild the runtime scene from it.
		UndoCommand command;
		command.Label = m_PendingUndoLabel;
		command.CustomUndo = [this, deltas, metaChanged, metaBefore]()
		{
			if (metaChanged)
				m_PlayBaselineMeta = metaBefore;
			for (const EntityDelta& delta : deltas)
			{
				if (delta.Before.empty())
					m_PlayBaselineEntities.erase(delta.Handle);
				else
					m_PlayBaselineEntities[delta.Handle] = delta.Before;
			}
			RestoreRuntimeState(m_PlayBaselineMeta, m_PlayBaselineEntities);
		};
		command.CustomRedo = [this, deltas, metaChanged, metaAfter]()
		{
			if (metaChanged)
				m_PlayBaselineMeta = metaAfter;
			for (const EntityDelta& delta : deltas)
			{
				if (delta.After.empty())
					m_PlayBaselineEntities.erase(delta.Handle);
				else
					m_PlayBaselineEntities[delta.Handle] = delta.After;
			}
			RestoreRuntimeState(m_PlayBaselineMeta, m_PlayBaselineEntities);
		};

		command.ApproxBytes = metaBefore.size() + metaAfter.size();
		for (const EntityDelta& delta : deltas)
			command.ApproxBytes += delta.Before.size() + delta.After.size();

		m_PlayUndoStack.push_back(std::move(command));
		TrimUndoStack(m_PlayUndoStack);
		m_PlayRedoStack.clear();
	}

	void EditorLayer::RestoreRuntimeState(const std::string& meta, const std::map<UUID, std::string>& entities)
	{
		std::vector<std::string> blocks;
		blocks.reserve(entities.size());
		for (const auto& [handle, block] : entities)
			blocks.push_back(block);

		Ref<Scene> newScene = CreateRef<Scene>();
		SceneSerializer serializer(newScene);
		if (!serializer.DeserializeFromSnapshots(meta, blocks))
		{
			LUX_CORE_ERROR("Play undo: failed to restore runtime state; history left unchanged.");
			return;
		}

		AdoptRuntimeScene(newScene);

		m_UndoCommitPending = false;
		EditorStack::Get().ConsumeSceneEdit();
	}

	void EditorLayer::AdoptRuntimeScene(const Ref<Scene>& scene)
	{
		// Tear down the current runtime, swap in the rebuilt scene, and restart its runtime — so a
		// play-mode undo resets physics/scripts to the snapshot state and continues from there.
		if (m_SceneState == SceneState::Play && m_ActiveScene)
			m_ActiveScene->OnRuntimeStop();
		else if (m_SceneState == SceneState::Simulate && m_ActiveScene)
			m_ActiveScene->OnSimulationStop();

		m_ActiveScene = scene;

		if (m_SceneState == SceneState::Play)
			m_ActiveScene->OnRuntimeStart();
		else if (m_SceneState == SceneState::Simulate)
			m_ActiveScene->OnSimulationStart();

		m_PanelManager->SetSceneContext(m_ActiveScene);

		if (m_EditorViewport)
		{
			m_EditorViewport->SetScene(m_ActiveScene);
			m_EditorViewport->SyncSceneViewport(m_ActiveScene);
			m_Framebuffer = m_EditorViewport->GetFramebuffer();
			m_SceneRenderer = m_EditorViewport->GetSceneRenderer();
		}

		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetContext(m_SceneRenderer);
		if (m_RendererDebuggerPanel)
			m_RendererDebuggerPanel->SetContext(m_SceneRenderer);
		if (m_ProfilerPanel)
			m_ProfilerPanel->SetContext(m_SceneRenderer);
	}

	void EditorLayer::SaveScene()
	{
		// While editing a prefab, Ctrl+S writes the prefab asset instead of a scene file.
		if (m_PrefabEditMode)
		{
			SavePrefabEdits();
			return;
		}

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
		SceneSerializer serializer(scene);
		serializer.Serialize(Project::GetActiveAssetDirectory() / path);
	}

	void EditorLayer::ResetRendererDebugViews()
	{
		if (m_SceneRenderer)
		{
			ClearSceneRendererDebugOptions(m_SceneRenderer->GetOptions());
			m_SceneRenderer->SetDebugViewMode(SceneRenderer::DebugViewMode::Final);
		}

		m_ShowPhysicsColliders = false;
		m_ShowBoundingBoxes = false;
		m_ShowEntityIcons = false;

		if (m_EditorViewport)
			m_EditorViewport->SetDisplayMode(Viewport::DisplayMode::Lit);
	}

	void EditorLayer::SyncEditorDebugViewsFromRenderer()
	{
		if (!m_SceneRenderer)
			return;

		const auto& options = m_SceneRenderer->GetOptions();
		m_ShowPhysicsColliders = options.ShowPhysicsColliders;

		if (m_EditorViewport)
			m_EditorViewport->SetDisplayMode(options.ShowSelectedInWireframe ? Viewport::DisplayMode::SelectedWireframe : Viewport::DisplayMode::Lit);
	}

	void EditorLayer::SuspendRendererDebugViewsForPlay()
	{
		if (m_PlayModeDebugViewsSuspended)
			return;

		if (m_SceneRenderer)
		{
			const auto& options = m_SceneRenderer->GetOptions();
			m_PlayModeDebugViewState.ShowGrid = options.ShowGrid;
			m_PlayModeDebugViewState.ShowSelectedInWireframe = options.ShowSelectedInWireframe;
			m_PlayModeDebugViewState.ShowPhysicsColliders = options.ShowPhysicsColliders;
			m_PlayModeDebugViewState.PhysicsColliderMode = options.PhysicsColliderMode;
			m_PlayModeDebugViewState.ShowPhysicsCollidersOnTop = options.ShowPhysicsCollidersOnTop;
			m_PlayModeDebugViewState.ShowShadowCascades = options.ShowShadowCascades;
			m_PlayModeDebugViewState.ShowCascadeFrustums = options.ShowCascadeFrustums;
			m_PlayModeDebugViewState.ShowLightComplexity = options.ShowLightComplexity;
			m_PlayModeDebugViewState.ShowMaterialComplexity = options.ShowMaterialComplexity;
			m_PlayModeDebugViewState.RendererDebugView = m_SceneRenderer->GetDebugViewMode();
		}
		m_PlayModeDebugViewState.ShowBoundingBoxes = m_ShowBoundingBoxes;
		m_PlayModeDebugViewState.ShowEntityIcons = m_ShowEntityIcons;
		m_PlayModeDebugViewState.DisplayMode = m_EditorViewport ? m_EditorViewport->GetDisplayMode() : Viewport::DisplayMode::Lit;

		m_PlayModeDebugViewsSuspended = true;
		ResetRendererDebugViews();

		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetDebugViewsRuntimeSuspended(true);
		if (m_RendererDebuggerPanel)
			m_RendererDebuggerPanel->SetDebugViewsRuntimeSuspended(true);
	}

	void EditorLayer::RestoreRendererDebugViewsAfterPlay()
	{
		if (!m_PlayModeDebugViewsSuspended)
			return;

		if (m_SceneRenderer)
		{
			auto& options = m_SceneRenderer->GetOptions();
			options.ShowGrid = m_PlayModeDebugViewState.ShowGrid;
			options.ShowSelectedInWireframe = m_PlayModeDebugViewState.ShowSelectedInWireframe;
			options.ShowPhysicsColliders = m_PlayModeDebugViewState.ShowPhysicsColliders;
			options.PhysicsColliderMode = m_PlayModeDebugViewState.PhysicsColliderMode;
			options.ShowPhysicsCollidersOnTop = m_PlayModeDebugViewState.ShowPhysicsCollidersOnTop;
			options.ShowShadowCascades = m_PlayModeDebugViewState.ShowShadowCascades;
			options.ShowCascadeFrustums = m_PlayModeDebugViewState.ShowCascadeFrustums;
			options.ShowLightComplexity = m_PlayModeDebugViewState.ShowLightComplexity;
			options.ShowMaterialComplexity = m_PlayModeDebugViewState.ShowMaterialComplexity;
			m_SceneRenderer->SetDebugViewMode(m_PlayModeDebugViewState.RendererDebugView);
		}

		m_ShowPhysicsColliders = m_PlayModeDebugViewState.ShowPhysicsColliders;
		m_ShowBoundingBoxes = m_PlayModeDebugViewState.ShowBoundingBoxes;
		m_ShowEntityIcons = m_PlayModeDebugViewState.ShowEntityIcons;

		if (m_EditorViewport)
			m_EditorViewport->SetDisplayMode(m_PlayModeDebugViewState.DisplayMode);

		m_PlayModeDebugViewsSuspended = false;
		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetDebugViewsRuntimeSuspended(false);
		if (m_RendererDebuggerPanel)
			m_RendererDebuggerPanel->SetDebugViewsRuntimeSuspended(false);
	}

	void EditorLayer::UpdateDiscordPresence()
	{
		if (!DiscordSocial::IsReady())
			return;

		// Driven from OnUpdate rather than the scene-state transitions so that project loads,
		// scene switches and renames are all covered by one path. DiscordSocial::SetPresence
		// drops an unchanged payload, so this costs a struct compare on a steady frame.

		// Image keys reference assets uploaded to the Discord application's Art Assets page
		// (Developer Portal -> your app -> Rich Presence -> Art Assets). Upload:
		//   "lux_logo"        - the large square icon (the only asset that really matters)
		//   "mode_play"       - small badge shown while playing
		//   "mode_simulate"   - small badge shown while simulating
		// A missing key just renders no image, so partial uploads degrade gracefully.
		DiscordSocial::PresenceInfo presence;
		presence.LargeImage = "lux_logo";
		presence.LargeText = "Lux Engine " LUX_VERSION;

		const char* verb = "Editing";
		switch (m_SceneState)
		{
			case SceneState::Play:
				verb = "Playing";
				presence.SmallImage = "mode_play";
				presence.SmallText = "Play mode";
				break;
			case SceneState::Simulate:
				verb = "Simulating";
				presence.SmallImage = "mode_simulate";
				presence.SmallText = "Simulate mode";
				break;
			default:
				break;
		}

		if (!Project::GetActive())
		{
			presence.Details = "In the editor";
			presence.State = "No project open";
		}
		else
		{
			presence.Details = std::format("{} {}", verb, Project::GetProjectName());
			presence.State = m_ActiveScene ? m_ActiveScene->GetName() : std::string("No scene");
		}

		DiscordSocial::SetPresence(presence);
	}

	void EditorLayer::OnScenePlay()
	{
		if (m_PrefabEditMode)   // no play while editing a prefab in isolation
			return;
		if (m_SceneState == SceneState::Simulate)
			OnSceneStop();

		SuspendRendererDebugViewsForPlay();
		m_SceneState = SceneState::Play;

		m_ActiveScene = Scene::Copy(m_EditorScene);

		m_ActiveScene->OnRuntimeStart();
		m_PanelManager->SetSceneContext(m_ActiveScene);

		if (m_EditorViewport)
		{
			m_EditorViewport->SetScene(m_ActiveScene);
			m_EditorViewport->SyncSceneViewport(m_ActiveScene);
			m_Framebuffer = m_EditorViewport->GetFramebuffer();
			m_SceneRenderer = m_EditorViewport->GetSceneRenderer();
		}

		ResetRendererDebugViews();
		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetContext(m_SceneRenderer);
		if (m_RendererDebuggerPanel)
			m_RendererDebuggerPanel->SetContext(m_SceneRenderer);
		if (m_ProfilerPanel)
			m_ProfilerPanel->SetContext(m_SceneRenderer);

		BeginPlayUndoHistory();   // transient play-mode undo, over the runtime scene
	}

	void EditorLayer::OnSceneSimulate()
	{
		if (m_PrefabEditMode)   // no simulate while editing a prefab in isolation
			return;
		if (m_SceneState == SceneState::Play)
			OnSceneStop();

		m_SceneState = SceneState::Simulate;

		m_ActiveScene = Scene::Copy(m_EditorScene);

		m_ActiveScene->OnSimulationStart();
		m_PanelManager->SetSceneContext(m_ActiveScene);

		if (m_EditorViewport)
		{
			m_EditorViewport->SetScene(m_ActiveScene);
			m_EditorViewport->SyncSceneViewport(m_ActiveScene);
			m_Framebuffer = m_EditorViewport->GetFramebuffer();
			m_SceneRenderer = m_EditorViewport->GetSceneRenderer();
		}

		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetContext(m_SceneRenderer);
		if (m_RendererDebuggerPanel)
			m_RendererDebuggerPanel->SetContext(m_SceneRenderer);
		if (m_ProfilerPanel)
			m_ProfilerPanel->SetContext(m_SceneRenderer);

		BeginPlayUndoHistory();   // transient play-mode undo, over the runtime scene
	}

	void EditorLayer::OnSceneStop()
	{
		LUX_CORE_ASSERT(m_SceneState == SceneState::Play || m_SceneState == SceneState::Simulate);

		const bool restoreDebugViews = m_SceneState == SceneState::Play;
		if (m_SceneState == SceneState::Play)
			m_ActiveScene->OnRuntimeStop();
		else if (m_SceneState == SceneState::Simulate)
			m_ActiveScene->OnSimulationStop();

		m_SceneState = SceneState::Edit;
		m_ActiveScene = m_EditorScene;

		// The transient play-mode history is meaningless once we're back to the editor scene.
		m_PlayUndoStack.clear();
		m_PlayRedoStack.clear();
		m_UndoCommitPending = false;

		m_PanelManager->SetSceneContext(m_ActiveScene);

		if (m_EditorViewport)
		{
			m_EditorViewport->SetScene(m_ActiveScene);
			m_EditorViewport->SyncSceneViewport(m_ActiveScene);
			m_Framebuffer = m_EditorViewport->GetFramebuffer();
			m_SceneRenderer = m_EditorViewport->GetSceneRenderer();
		}

		if (restoreDebugViews)
			RestoreRendererDebugViewsAfterPlay();

		if (m_SceneRendererPanel)
			m_SceneRendererPanel->SetContext(m_SceneRenderer);
		if (m_RendererDebuggerPanel)
			m_RendererDebuggerPanel->SetContext(m_SceneRenderer);
		if (m_ProfilerPanel)
			m_ProfilerPanel->SetContext(m_SceneRenderer);
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
			EditorStack::Get().MarkSceneEdited("Duplicate Entity");
		}
	}
}
