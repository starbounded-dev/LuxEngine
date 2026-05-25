#include "EditorLayer.h"

#include "Lux/Scene/SceneSerializer.h"
#include "Lux/Core/Application.h"
#include "Lux/Scripting/ScriptEngine.h"
#include "Lux/Renderer/Renderer.h"
#include "Lux/Renderer/SceneRenderer.h"
#include "Lux/Renderer/ShaderPack.h"
#include "Lux/Serialization/AssetPack.h"
#include "Lux/Project/ProjectSerializer.h"

#include "Lux/Utilities/FileSystem.h"

#include "Lux/Asset/AssetManager.h"
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
#include "Panels/ApplicationSettingsPanel.h"
#include "Panels/AssetManagerPanel.h"
#include "Panels/ProjectSettingsWindow.h"
#include "Lux/Editor/SceneHierarchyPanel.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
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

		constexpr const char* s_RuntimeProjectFile = "Project.luxruntime";
		constexpr const char* s_RuntimeAssetPackFile = "AssetPack.lap";
		constexpr const char* s_RuntimeShaderPackFile = "ShaderPack.lsp";

		std::string SanitizeBuildName(std::string value)
		{
			if (value.empty())
				value = "LuxGame";

			for (char& c : value)
			{
				const bool valid = std::isalnum((unsigned char)c) || c == '-' || c == '_';
				if (!valid)
					c = '_';
			}

			return value;
		}

		bool CopyFileIfExists(const std::filesystem::path& source, const std::filesystem::path& destination, bool required = false)
		{
			std::error_code ec;
			if (!std::filesystem::exists(source, ec) || ec)
			{
				if (required)
					LUX_CONSOLE_LOG_ERROR("Missing export file: {}", source.string());
				return false;
			}

			std::filesystem::create_directories(destination.parent_path(), ec);
			ec.clear();
			std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
			if (ec)
			{
				LUX_CONSOLE_LOG_ERROR("Failed to copy '{}' to '{}': {}", source.string(), destination.string(), ec.message());
				return false;
			}

			return true;
		}

		bool CopyDirectoryRecursive(const std::filesystem::path& source, const std::filesystem::path& destination, bool skipDebugFiles = false)
		{
			std::error_code ec;
			if (!std::filesystem::exists(source, ec) || ec)
				return false;

			for (const auto& entry : std::filesystem::recursive_directory_iterator(source, ec))
			{
				if (ec)
					break;

				const std::filesystem::path relativePath = std::filesystem::relative(entry.path(), source, ec);
				if (ec)
					continue;

				if (!relativePath.empty() && *relativePath.begin() == "Cache")
					continue;

				const std::filesystem::path target = destination / relativePath;
				if (entry.is_directory(ec))
				{
					std::filesystem::create_directories(target, ec);
					continue;
				}

				if (entry.is_regular_file(ec))
				{
					if (skipDebugFiles)
					{
						const std::filesystem::path extension = entry.path().extension();
						if (extension == ".pdb" || extension == ".ilk" || extension == ".exp")
							continue;
					}
					CopyFileIfExists(entry.path(), target);
				}
			}

			return true;
		}

		std::filesystem::path FindFirstExistingDirectory(std::initializer_list<std::filesystem::path> candidates)
		{
			std::error_code ec;
			for (const std::filesystem::path& candidate : candidates)
			{
				if (!candidate.empty() && std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec))
					return candidate;
			}

			return {};
		}

		bool FileExists(const std::filesystem::path& path)
		{
			std::error_code ec;
			return !path.empty() && std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
		}

		std::string QuoteCommandArgument(std::string value)
		{
			std::string result = "\"";
			for (char c : value)
			{
				if (c == '"')
					result += "\\\"";
				else
					result += c;
			}
			result += '"';
			return result;
		}

		bool IsBuildConfigurationDirectory(const std::filesystem::path& path)
		{
			const std::string directoryName = path.filename().string();
			return path.parent_path().filename() == "bin" && directoryName.find("-windows-x86_64") != std::string::npos;
		}

		std::filesystem::path GetRuntimeOutputDirectory(RuntimeExportTarget target)
		{
			return std::string(RuntimeExportTargetToString(target)) + "-windows-x86_64";
		}

		std::filesystem::path FindRepositoryRootFrom(std::filesystem::path start)
		{
			if (start.empty())
				return {};

			std::error_code ec;
			start = std::filesystem::absolute(start, ec).lexically_normal();
			if (ec)
				return {};

			if (std::filesystem::is_regular_file(start, ec))
				start = start.parent_path();

			for (std::filesystem::path directory = start; !directory.empty(); directory = directory.parent_path())
			{
				if (std::filesystem::exists(directory / "premake5.lua", ec)
					&& std::filesystem::exists(directory / "Core", ec)
					&& std::filesystem::exists(directory / "Lux-Runtime" / "premake5.lua", ec))
				{
					return directory;
				}

				if (directory == directory.root_path())
					break;
			}

			return {};
		}

		bool IsRuntimeExecutableOutdated(const std::filesystem::path& runtimeExe, const std::filesystem::path& repositoryRoot)
		{
			std::error_code ec;
			if (runtimeExe.empty() || !std::filesystem::exists(runtimeExe, ec))
				return true;

			const auto executableWriteTime = std::filesystem::last_write_time(runtimeExe, ec);
			if (ec)
				return true;

			const std::array<std::filesystem::path, 2> sourceRoots = {
				repositoryRoot / "Lux-Runtime",
				repositoryRoot / "Core" / "Source"
			};

			for (const auto& sourceRoot : sourceRoots)
			{
				if (!std::filesystem::exists(sourceRoot, ec))
					continue;

				for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot, ec))
				{
					if (ec)
						break;
					if (!entry.is_regular_file(ec))
						continue;

					const std::filesystem::path extension = entry.path().extension();
					if (extension != ".cpp" && extension != ".h" && extension != ".hpp" && extension != ".c" && extension != ".rc" && extension != ".lua")
						continue;

					if (entry.last_write_time(ec) > executableWriteTime && !ec)
						return true;
				}
			}

			return false;
		}

		std::filesystem::path GetRuntimeExecutablePath(RuntimeExportTarget target)
		{
			std::error_code ec;
			const std::filesystem::path current = std::filesystem::current_path(ec);
			if (ec)
				return {};

			const std::filesystem::path runtimeOutputDirectory = GetRuntimeOutputDirectory(target);
			std::vector<std::filesystem::path> candidates;

			if (std::filesystem::path root = FindRepositoryRootFrom(current); !root.empty())
				candidates.emplace_back((root / "bin" / runtimeOutputDirectory / "Lux-Runtime" / "Lux-Runtime.exe").lexically_normal());

			if (Ref<Project> activeProject = Project::GetActive())
			{
				if (std::filesystem::path root = FindRepositoryRootFrom(activeProject->GetProjectDirectory()); !root.empty())
					candidates.emplace_back((root / "bin" / runtimeOutputDirectory / "Lux-Runtime" / "Lux-Runtime.exe").lexically_normal());
			}

			const std::filesystem::path buildConfigDirectory = current.filename() == "Editor" ? current.parent_path() : current;
			if (IsBuildConfigurationDirectory(buildConfigDirectory))
				candidates.emplace_back((buildConfigDirectory / "Lux-Runtime" / "Lux-Runtime.exe").lexically_normal());

			for (const std::filesystem::path& candidate : candidates)
			{
				if (FileExists(candidate))
					return candidate;
			}

			return {};
		}

		bool BuildRuntimeExecutable(RuntimeExportTarget target)
		{
			std::filesystem::path repositoryRoot = FindRepositoryRootFrom(std::filesystem::current_path());
			if (repositoryRoot.empty())
			{
				if (Ref<Project> activeProject = Project::GetActive())
					repositoryRoot = FindRepositoryRootFrom(activeProject->GetProjectDirectory());
			}

			if (repositoryRoot.empty())
			{
				LUX_CONSOLE_LOG_ERROR("Could not locate repository root for Lux-Runtime build.");
				return false;
			}

			const std::filesystem::path projectFile = repositoryRoot / "Lux-Runtime" / "Lux-Runtime.vcxproj";
			if (!FileExists(projectFile))
			{
				LUX_CONSOLE_LOG_ERROR("Lux-Runtime project file not found: {}", projectFile.string());
				return false;
			}

			const std::filesystem::path msbuildPath = "C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe";
			const std::string msbuild = FileExists(msbuildPath) ? msbuildPath.string() : "MSBuild.exe";
			const std::string command =
				"cmd /S /C \""
				+ QuoteCommandArgument(msbuild) + " "
				+ QuoteCommandArgument(projectFile.string())
				+ " /t:Build /p:Configuration=" + RuntimeExportTargetToString(target)
				+ " /p:Platform=x64 /m:1 /nr:false /v:minimal\"";

			LUX_CONSOLE_LOG_INFO("Building Lux-Runtime ({})...", RuntimeExportTargetToString(target));
			const int result = std::system(command.c_str());
			if (result != 0)
			{
				LUX_CONSOLE_LOG_ERROR("Lux-Runtime build failed with exit code {}.", result);
				return false;
			}

			LUX_CONSOLE_LOG_INFO("Lux-Runtime build complete.");
			return true;
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

		/////////// Configure Panels ///////////
		m_PanelManager = CreateScope<PanelManager>();

		m_SceneHierarchyPanel = m_PanelManager->AddPanel<SceneHierarchyPanel>(PanelCategory::View, SCENE_HIERARCHY_PANEL_ID, "Scene Hierarchy", true);
		Ref<ContentBrowserPanel> contentBrowserPanel = m_PanelManager->AddPanel<ContentBrowserPanel>(PanelCategory::View, CONTENT_BROWSER_PANEL_ID, "Content Browser", true);
		Ref<TextEditorPanel> textEditorPanel = m_PanelManager->AddPanel<TextEditorPanel>(PanelCategory::View, "TextEditorPanel", "Text Editor", true);
		m_ConsolePanel = m_PanelManager->AddPanel<EditorConsolePanel>(PanelCategory::View, CONSOLE_PANEL_ID, "Log", true);

		m_SceneRendererPanel = m_PanelManager->AddPanel<SceneRendererPanel>(PanelCategory::View, SCENE_RENDERER_PANEL_ID, "Scene Renderer", true);
		m_RendererDebuggerPanel = m_PanelManager->AddPanel<RendererDebuggerPanel>(PanelCategory::View, RENDERER_DEBUGGER_PANEL_ID, "Renderer Debugger", false);
		if (m_SceneRendererPanel)
		{
			m_SceneRendererPanel->SetDebugViewCallbacks(
				[this]() { ResetRendererDebugViews(); },
				[this]() { SyncEditorDebugViewsFromRenderer(); });
		}

		ApplicationSettingsPanel::EditorPreferencesBindings editorPreferencesBindings{};
		editorPreferencesBindings.VSync = &m_VSync;
		editorPreferencesBindings.UseGizmoSnap = &m_UseGizmoSnap;
		editorPreferencesBindings.TranslationSnapValue = &m_TranslationSnapValue;
		editorPreferencesBindings.RotationSnapValue = &m_RotationSnapValue;
		editorPreferencesBindings.ShowBoundingBoxes = &m_ShowBoundingBoxes;
		editorPreferencesBindings.ShowEntityIcons = &m_ShowEntityIcons;
		editorPreferencesBindings.ShowViewportPerformanceHUD = &m_ShowViewportPerformanceHUD;
		editorPreferencesBindings.ShowPhysicsColliders = &m_ShowPhysicsColliders;
		editorPreferencesBindings.OnPreferencesChanged = [this]()
			{
				ApplyEditorPreferences();
				SaveEditorPreferences();
			};

		m_PanelManager->AddPanel<ApplicationSettingsPanel>(PanelCategory::View, APPLICATION_SETTINGS_PANEL_ID, "Application Settings", false, contentBrowserPanel, editorPreferencesBindings, m_UserPreferences);
		m_PanelManager->AddPanel<AssetManagerPanel>(PanelCategory::View, ASSET_MANAGER_PANEL_ID, "Asset Manager", false);
		m_PanelManager->AddPanel<ProjectSettingsWindow>(PanelCategory::View, PROJECT_SETTINGS_PANEL_ID, "Project Settings", false);

		// Light Settings panel
		m_PanelManager->AddPanel<LightSettingsPanel>(PanelCategory::View, "LightSettingsPanel", "Light Settings", true);

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

		m_PanelManager->SetSceneContext(m_EditorScene);
		m_PanelManager->OnProjectChanged(Project::GetActive());
		ApplyEditorPreferences();

		if (contentBrowserPanel)
		{
			contentBrowserPanel->RegisterItemActivateCallbackForType(AssetType::Scene, [this](const AssetMetadata& metadata)
			{
				OpenScene(metadata.Handle);
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
	}

	void EditorLayer::OnDetach()
	{
		LUX_PROFILE_FUNCTION("EditorLayer::OnDetach");
		SaveEditorPreferences();

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
		m_SceneRendererPanel.reset();
		m_SceneHierarchyPanel.reset();
		EditorResources::Shutdown();
	}

	void EditorLayer::OnUpdate(Timestep ts)
	{
		LUX_PROFILE_FUNCTION("EditorLayer::OnUpdate");

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

		OnOverlayRender();
		SceneRenderer::WaitForThreads();
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
			}
			style.WindowMinSize.x = minWinSizeX;

			m_PanelManager->OnImGuiRender();

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
						ImTextureID texID = GetImGuiTextureID(viewportImage);
						ImGui::Image(texID, ImVec2{ viewportSize.x, viewportSize.y }, ImVec2{ 0, 0 }, ImVec2{ 1, 1 });
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
					if (selectedEntity && m_GizmoType != -1)
					{
						ImGuizmo::SetOrthographic(false);
						ImGuizmo::SetDrawlist();
						ImGuizmo::SetRect(viewportBounds[0].x, viewportBounds[0].y,
							viewportBounds[1].x - viewportBounds[0].x,
							viewportBounds[1].y - viewportBounds[0].y);

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
					}
				}

				m_EditorViewport->EndImGui();
			}
			ImGui::PopStyleVar();

			UI_GizmosToolbar();
			UI_CentralToolbar();
			UI_ViewportSettings();
		}

		ImGui::End(); // Lux Editor
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
				if (ImGui::BeginMenu("Create Project"))
				{
					if (ImGui::MenuItem("Forward"))
						NewProject(RenderingTechnique::Forward);
					if (ImGui::MenuItem("Deferred"))
						NewProject(RenderingTechnique::Deferred);
					ImGui::EndMenu();
				}
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
					Application::Get().DispatchEvent<WindowCloseEvent, true>();

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Edit"))
			{
				if (ImGui::MenuItem("Reload C# Assembly", "Ctrl+R"))
					ScriptEngine::ReloadAssembly();
				if (ImGui::MenuItem("Reload All Shaders", "Ctrl+Shift+R"))
					Renderer::ReloadShaders(true);
				ImGui::MenuItem("Second Viewport", nullptr, &m_SecondViewportEnabled);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("View"))
			{
				auto& viewPanels = m_PanelManager->GetPanels(PanelCategory::View);
				for (auto& [id, panelData] : viewPanels)
					ImGui::MenuItem(panelData.Name, nullptr, &panelData.IsOpen);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Tools"))
			{
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

		ImU32 targetTitlebarColor = Colors::Theme::titlebar;
		if (m_SceneState == SceneState::Play)
			targetTitlebarColor = Colors::Theme::titlebarOrange;
		else if (m_SceneState == SceneState::Simulate)
			targetTitlebarColor = Colors::Theme::titlebarGreen;

		const ImVec4 targetColor = ImGui::ColorConvertU32ToFloat4(targetTitlebarColor);
		const float dt = ImGui::GetIO().DeltaTime;
		m_AnimatedTitlebarColor = ImLerp(m_AnimatedTitlebarColor, targetColor, std::clamp(dt * 8.0f, 0.0f, 1.0f));
		drawList->AddRectFilled(windowPos, windowMax, ImGui::ColorConvertFloat4ToU32(m_AnimatedTitlebarColor));

		drawList->AddLine(ImVec2(windowPos.x, windowPos.y + m_TitlebarHeight), ImVec2(windowPos.x + window->Size.x, windowPos.y + m_TitlebarHeight), Colors::Theme::backgroundDark);

		const float logoPadding = 16.0f;
		const float logoSize = 30.0f;
		const float logoTop = (m_TitlebarHeight - logoSize) * 0.5f;
		const ImVec2 logoMin(windowPos.x + logoPadding, windowPos.y + logoTop);
		const ImVec2 logoMax(logoMin.x + logoSize, logoMin.y + logoSize);
		if (EditorResources::HazelLogoTexture)
			drawList->AddImage(GetImGuiTextureID(EditorResources::HazelLogoTexture), logoMin, logoMax);

		const float menuBarX = logoPadding * 2.0f + logoSize + 8.0f;
		ImGui::SetCursorPos(ImVec2(menuBarX, 4.0f));
		UI_DrawMenubar();

		const std::string sceneName = GetSceneDisplayName(m_EditorScenePath);
		const ImVec2 sceneNameSize = ImGui::CalcTextSize(sceneName.c_str());
		const float sceneNameX = windowPos.x + (window->Size.x - sceneNameSize.x) * 0.5f;
		const float sceneNameY = windowPos.y + (m_TitlebarHeight - sceneNameSize.y) * 0.5f;
		drawList->AddText(ImVec2(sceneNameX, sceneNameY), Colors::Theme::textBrighter, sceneName.c_str());
		drawList->AddLine(
			ImVec2(sceneNameX - 6.0f, sceneNameY + sceneNameSize.y + 4.0f),
			ImVec2(sceneNameX + sceneNameSize.x + 6.0f, sceneNameY + sceneNameSize.y + 4.0f),
			Colors::Theme::accent, 1.5f);

		GLFWwindow* nativeWindow = Application::Get().GetWindow().GetNativeWindow();
		const bool isMaximized = nativeWindow && glfwGetWindowAttrib(nativeWindow, GLFW_MAXIMIZED);

		const float controlsWidth = 120.0f;
		const float dragZoneMinX = 70.0f;
		const float dragZoneMaxX = window->Size.x - controlsWidth - 220.0f;
		ImGui::SetCursorPos(ImVec2(dragZoneMinX, 0.0f));
		ImGui::InvisibleButton("##titleBarDragZone", ImVec2(std::max(0.0f, dragZoneMaxX - dragZoneMinX), m_TitlebarHeight));
		const ImVec2 dragMin = ImGui::GetItemRectMin();
		const ImVec2 dragMax = ImGui::GetItemRectMax();
		m_TitleBarDragRectMin = ImVec2(dragMin.x - windowPos.x, dragMin.y - windowPos.y);
		m_TitleBarDragRectMax = ImVec2(dragMax.x - windowPos.x, dragMax.y - windowPos.y);

#if !defined(LUX_PLATFORM_WINDOWS)
		if (nativeWindow && !isMaximized && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			int windowX = 0, windowY = 0;
			glfwGetWindowPos(nativeWindow, &windowX, &windowY);
			const ImVec2 delta = ImGui::GetIO().MouseDelta;
			glfwSetWindowPos(nativeWindow, windowX + (int)delta.x, windowY + (int)delta.y);
		}
#endif

		const std::string projectName = GetProjectDisplayName();
		const ImVec2 projectNameSize = ImGui::CalcTextSize(projectName.c_str());
		const ImVec2 projectBoxMin(windowPos.x + window->Size.x - controlsWidth - projectNameSize.x - 36.0f, windowPos.y + 14.0f);
		const ImVec2 projectBoxMax(projectBoxMin.x + projectNameSize.x + 20.0f, projectBoxMin.y + 26.0f);
		drawList->AddRect(projectBoxMin, projectBoxMax, Colors::Theme::muted, 6.0f, 0, 1.0f);
		drawList->AddText(ImVec2(projectBoxMin.x + 10.0f, projectBoxMin.y + 5.0f), Colors::Theme::text, projectName.c_str());

		const float buttonSize = 34.0f;
		const float buttonY = (m_TitlebarHeight - buttonSize) * 0.5f;
		const float buttonsStartX = window->Size.x - controlsWidth;
		const ImU32 normalTint = IM_COL32(220, 220, 220, 220);
		const ImU32 hoverTint = IM_COL32(255, 255, 255, 255);
		const ImU32 activeTint = IM_COL32(200, 200, 200, 255);

		auto drawWindowControlButton = [&](const char* id, const Ref<Texture2D>& icon, float localX, auto&& onClick)
			{
				ImGui::SetCursorPos(ImVec2(localX, buttonY));
				ImGui::InvisibleButton(id, ImVec2(buttonSize, buttonSize));
				if (icon)
					ImGuiEx::DrawButtonImage(icon, normalTint, hoverTint, activeTint, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));

				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
					onClick();
			};

		drawWindowControlButton("##minimizeWindow", EditorResources::MinimizeIcon, buttonsStartX + 4.0f, [nativeWindow]()
			{
				if (!nativeWindow)
					return;
				Application::Get().QueueEvent([nativeWindow]() { glfwIconifyWindow(nativeWindow); });
			});

		drawWindowControlButton("##maximizeRestoreWindow", isMaximized ? EditorResources::RestoreIcon : EditorResources::MaximizeIcon, buttonsStartX + 42.0f, [nativeWindow, isMaximized]()
			{
				if (!nativeWindow)
					return;
				Application::Get().QueueEvent([nativeWindow, isMaximized]()
					{
						if (isMaximized)
							glfwRestoreWindow(nativeWindow);
						else
							glfwMaximizeWindow(nativeWindow);
					});
			});

		drawWindowControlButton("##closeWindow", EditorResources::CloseIcon, buttonsStartX + 80.0f, []()
			{
				Application::Get().DispatchEvent<WindowCloseEvent, true>();
			});
	}

	void EditorLayer::UI_GizmosToolbar()
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

		ImGui::SetNextWindowPos(ImVec2(viewportBounds[0].x + 12.0f, viewportBounds[0].y + 12.0f), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.55f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
		ImGui::Begin("##viewport_gizmos_toolbar", nullptr, flags);

		const ImU32 normalTint = IM_COL32(215, 215, 215, 220);
		const ImU32 hoverTint = IM_COL32(255, 255, 255, 255);
		const ImU32 activeTint = IM_COL32(235, 235, 235, 255);
		const ImVec2 buttonSize(24.0f, 24.0f);

		auto gizmoButton = [&](const char* id, Ref<Texture2D> icon, int gizmoMode)
			{
				ImGui::InvisibleButton(id, buttonSize);
				ImGuiEx::DrawButtonImage(icon, normalTint, hoverTint, activeTint, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));

				if (m_GizmoType == gizmoMode)
					ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), Colors::Theme::accent, 2.0f, 0, 2.0f);

				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
					m_GizmoType = gizmoMode;
			};

		gizmoButton("##gizmo_select", EditorResources::PointerIcon, -1);
		ImGui::SameLine();
		gizmoButton("##gizmo_translate", EditorResources::MoveIcon, ImGuizmo::TRANSLATE);
		ImGui::SameLine();
		gizmoButton("##gizmo_rotate", EditorResources::RotateIcon, ImGuizmo::ROTATE);
		ImGui::SameLine();
		gizmoButton("##gizmo_scale", EditorResources::ScaleIcon, ImGuizmo::SCALE);

		ImGui::End();
		ImGui::PopStyleVar(2);
	}

	void EditorLayer::UI_CentralToolbar()
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

		const float toolbarWidth = 118.0f;
		const float posX = viewportBounds[0].x + (viewportSize.x - toolbarWidth) * 0.5f;
		const float posY = viewportBounds[0].y + 12.0f;

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.55f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 0.0f));
		ImGui::Begin("##viewport_central_toolbar", nullptr, flags);

		const ImU32 normalTint = IM_COL32(215, 215, 215, 220);
		const ImU32 hoverTint = IM_COL32(255, 255, 255, 255);
		const ImU32 activeTint = IM_COL32(235, 235, 235, 255);
		const ImVec2 buttonSize(24.0f, 24.0f);

		auto controlButton = [&](const char* id, Ref<Texture2D> icon, bool active, const std::function<void()>& onClick)
			{
				ImGui::InvisibleButton(id, buttonSize);
				ImGuiEx::DrawButtonImage(icon, normalTint, hoverTint, activeTint, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
				if (active)
					ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), Colors::Theme::accent, 2.0f, 0, 2.0f);
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
					onClick();
			};

		controlButton("##scene_play", EditorResources::PlayIcon, m_SceneState == SceneState::Play, [this]()
			{
				if (m_SceneState != SceneState::Play)
					OnScenePlay();
			});
		ImGui::SameLine();
		controlButton("##scene_simulate", EditorResources::SimulateIcon, m_SceneState == SceneState::Simulate, [this]()
			{
				if (m_SceneState != SceneState::Simulate)
					OnSceneSimulate();
			});
		ImGui::SameLine();
		controlButton("##scene_stop", EditorResources::StopIcon, m_SceneState == SceneState::Edit, [this]()
			{
				if (m_SceneState != SceneState::Edit)
					OnSceneStop();
			});

		ImGui::End();
		ImGui::PopStyleVar(2);
	}

	void EditorLayer::UI_ViewportPerformanceHUD()
	{
		if (!m_ShowViewportPerformanceHUD || !m_EditorViewport || !m_SceneRenderer)
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
		const float wholeCPUTime = appTimers.MainThreadWorkTime + appTimers.RenderThreadWorkTime;
		float wholeGPUTime = stats.TotalGPUTime;
		if (ImGuiLayer* imguiLayer = Application::Get().GetImGuiLayer())
		{
			if (ImGuiRenderer* imguiRenderer = imguiLayer->GetImGuiRenderer())
				wholeGPUTime += imguiRenderer->GetGPUTime(Renderer::GetCurrentFrameIndex());
		}

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

		ImGui::SetNextWindowPos(ImVec2(viewportBounds[1].x - 12.0f, viewportBounds[1].y - 12.0f), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.48f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
		ImGui::Begin("##viewport_performance_hud", nullptr, flags);
		ImGui::Text("FPS %.0f  %.2f ms", fps, frameTimeMs);
		ImGui::Text("CPU %.2f ms  GPU %.2f ms", wholeCPUTime, wholeGPUTime);
		ImGui::Text("Draws %u  Visible %u", stats.DrawCalls, stats.VisibleInstances);
		ImGui::Text("GPU Visible %u", stats.GPUVisibleInstances);
		if (memory.BudgetBytes > 0)
			ImGui::Text("VRAM %s / %s", Utils::BytesToString(memory.UsedBytes).c_str(), Utils::BytesToString(memory.BudgetBytes).c_str());
		else
			ImGui::Text("VRAM %s", Utils::BytesToString(memory.UsedBytes).c_str());
		ImGui::Text("Scale %.0f%%  %ux%u", renderScale, m_SceneRenderer->GetOutputViewportWidth(), m_SceneRenderer->GetOutputViewportHeight());
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

	void EditorLayer::UI_Toolbar()
	{
		UI_GizmosToolbar();
		UI_CentralToolbar();
		UI_ViewportSettings();
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

		case Key::Q: if (!ImGuizmo::IsUsing()) m_GizmoType = -1;                            break;
		case Key::W: if (!ImGuizmo::IsUsing()) m_GizmoType = ImGuizmo::OPERATION::TRANSLATE; break;
		case Key::E: if (!ImGuizmo::IsUsing()) m_GizmoType = ImGuizmo::OPERATION::ROTATE;   break;
		case Key::R:
			if (control && shift) Renderer::ReloadShaders(true);
			else if (control) ScriptEngine::ReloadAssembly();
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

		const glm::vec2* viewportBounds = m_EditorViewport->GetBounds();
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

		const bool inDragZone = x >= m_TitleBarDragRectMin.x && x <= m_TitleBarDragRectMax.x
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

			bool drewBoundingBox = false;
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
						drewBoundingBox = true;
					}
				}
			}

			if (!drewBoundingBox)
			{
				glm::vec4 color(1.0f, 0.5f, 0.0f, 1.0f);
				glm::vec4 corners[4] = {
					{-0.5f, -0.5f, 0.0f, 1.0f}, { 0.5f, -0.5f, 0.0f, 1.0f},
					{ 0.5f,  0.5f, 0.0f, 1.0f}, {-0.5f,  0.5f, 0.0f, 1.0f}
				};
				for (int i = 0; i < 4; i++)
				{
					glm::vec3 p0 = worldTransform * corners[i];
					glm::vec3 p1 = worldTransform * corners[(i + 1) % 4];
					m_Renderer2D->DrawLine(p0, p1, color);
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
		m_UseGizmoSnap = settings.GetInt("Editor.UseGizmoSnap", 0) != 0;
		m_TranslationSnapValue = std::max(settings.GetFloat("Editor.TranslationSnapValue", 0.5f), 0.05f);
		m_RotationSnapValue = std::max(settings.GetFloat("Editor.RotationSnapValue", 45.0f), 1.0f);
		m_ShowBoundingBoxes = settings.GetInt("Editor.ShowBoundingBoxes", 0) != 0;
		m_ShowEntityIcons = settings.GetInt("Editor.ShowEntityIcons", 1) != 0;
		m_ShowViewportPerformanceHUD = settings.GetInt("Editor.ShowViewportPerformanceHUD", 1) != 0;
		m_ShowPhysicsColliders = settings.GetInt("Editor.ShowPhysicsColliders", 0) != 0;

		ApplyEditorPreferences();
	}

	void EditorLayer::SaveEditorPreferences() const
	{
		auto& settings = Application::Get().GetSettings();
		settings.SetInt("Editor.VSync", m_VSync ? 1 : 0);
		settings.SetInt("Editor.UseGizmoSnap", m_UseGizmoSnap ? 1 : 0);
		settings.SetFloat("Editor.TranslationSnapValue", m_TranslationSnapValue);
		settings.SetFloat("Editor.RotationSnapValue", m_RotationSnapValue);
		settings.SetInt("Editor.ShowBoundingBoxes", m_ShowBoundingBoxes ? 1 : 0);
		settings.SetInt("Editor.ShowEntityIcons", m_ShowEntityIcons ? 1 : 0);
		settings.SetInt("Editor.ShowViewportPerformanceHUD", m_ShowViewportPerformanceHUD ? 1 : 0);
		settings.SetInt("Editor.ShowPhysicsColliders", m_ShowPhysicsColliders ? 1 : 0);
		settings.Serialize();
	}

	void EditorLayer::ApplyEditorPreferences()
	{
		Application::Get().GetWindow().SetVSync(m_VSync);

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

	void EditorLayer::NewProject(RenderingTechnique renderingTechnique)
	{
		Project::New();
		Project::GetActive()->GetConfig().RendererTechnique = renderingTechnique;
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

	void EditorLayer::ExportRuntime()
	{
		Ref<Project> project = Project::GetActive();
		if (!project)
		{
			LUX_CONSOLE_LOG_ERROR("No active project to export.");
			return;
		}

		if (m_SceneState != SceneState::Edit)
			OnSceneStop();

		SaveScene();
		SaveProject();

		const std::filesystem::path selectedFolder = FileSystem::OpenFolderDialog(project->GetProjectDirectory().string().c_str());
		if (selectedFolder.empty())
			return;

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
				return;
			runtimeExe = GetRuntimeExecutablePath(targetConfig);
		}

		const std::filesystem::path current = std::filesystem::current_path(ec);
		const std::filesystem::path resourcesSource = FindFirstExistingDirectory({
			current / "Resources",
			current / ".." / "Editor" / "Resources",
			repositoryRoot / "Editor" / "Resources",
			std::filesystem::path("Editor") / "Resources"
		});
		const std::filesystem::path monoSource = FindFirstExistingDirectory({
			current / "mono",
			current / ".." / "Editor" / "mono",
			repositoryRoot / "Editor" / "mono",
			std::filesystem::path("Editor") / "mono"
		});
		const std::filesystem::path scriptModule = Project::GetActiveScriptModuleFilePath();
		const bool hasStartupScene = project->GetConfig().StartSceneHandle != 0;
		const bool hasRuntimeExe = FileExists(runtimeExe);
		const bool hasResources = !resourcesSource.empty();
		const bool hasMono = !monoSource.empty();
		const bool hasScriptModule = project->GetConfig().ScriptModulePath.empty() || FileExists(scriptModule);

		LUX_CONSOLE_LOG_INFO("Runtime export preflight:");
		LUX_CONSOLE_LOG_INFO("  Startup Scene: {}", hasStartupScene ? "set" : "missing");
		LUX_CONSOLE_LOG_INFO("  Lux-Runtime.exe: {}", hasRuntimeExe ? runtimeExe.string() : "missing");
		LUX_CONSOLE_LOG_INFO("  Resources: {}", hasResources ? resourcesSource.string() : "missing");
		LUX_CONSOLE_LOG_INFO("  Script Module: {}", hasScriptModule ? (scriptModule.empty() ? "optional" : scriptModule.string()) : "missing");
		LUX_CONSOLE_LOG_INFO("  mono: {}", hasMono ? monoSource.string() : "missing");

		if (!hasStartupScene || !hasRuntimeExe || !hasResources)
		{
			LUX_CONSOLE_LOG_ERROR("Runtime export preflight failed. Set a Startup Scene and make sure Lux-Runtime and Resources are available.");
			return;
		}

		if (!hasScriptModule)
			LUX_CONSOLE_LOG_WARN("Script module is missing. Export will continue, but scripted behaviours will not load.");
		if (!hasMono)
			LUX_CONSOLE_LOG_WARN("Mono directory is missing. Export will continue, but scripting will not run.");

		const std::string buildName = SanitizeBuildName(runtimeSettings.GameName.empty() ? project->GetConfig().Name : runtimeSettings.GameName);
		const std::filesystem::path exportRoot = selectedFolder / (buildName + "-Windows-x86_64");
		const std::filesystem::path exportAssets = exportRoot / "Assets";

		std::filesystem::create_directories(exportAssets, ec);
		if (ec)
		{
			LUX_CONSOLE_LOG_ERROR("Failed to create export directory '{}': {}", exportRoot.string(), ec.message());
			return;
		}

		std::atomic<float> assetPackProgress = 0.0f;
		Ref<AssetPack> assetPack = AssetPack::CreateFromActiveProject(assetPackProgress);
		if (!assetPack)
		{
			LUX_CONSOLE_LOG_ERROR("Runtime export failed while building the asset pack.");
			return;
		}

		ProjectSerializer serializer(project);
		const std::filesystem::path runtimeProjectFile = exportAssets / s_RuntimeProjectFile;
		if (!serializer.SerializeRuntime(runtimeProjectFile))
		{
			LUX_CONSOLE_LOG_ERROR("Runtime export failed while writing '{}'.", runtimeProjectFile.string());
			return;
		}

		if (!CopyFileIfExists(Project::GetActiveAssetDirectory() / s_RuntimeAssetPackFile, exportAssets / s_RuntimeAssetPackFile, true))
			return;
		LUX_CONSOLE_LOG_INFO("  AssetPack.lap: created");

		const std::filesystem::path exportedExe = exportRoot / (buildName + ".exe");
		if (!CopyFileIfExists(runtimeExe, exportedExe, true))
			LUX_CONSOLE_LOG_WARN("Build the Lux-Runtime project once before exporting a standalone executable.");

		const std::filesystem::path runtimeDirectory = runtimeExe.parent_path();
		if (!runtimeDirectory.empty() && std::filesystem::exists(runtimeDirectory, ec))
		{
			for (const auto& entry : std::filesystem::directory_iterator(runtimeDirectory, ec))
			{
				if (!entry.is_regular_file(ec))
					continue;

				const std::filesystem::path extension = entry.path().extension();
				if (extension == ".dll" || (extension == ".pdb" && targetConfig != RuntimeExportTarget::Dist))
					CopyFileIfExists(entry.path(), exportRoot / entry.path().filename());
			}
		}

		if (!resourcesSource.empty())
			CopyDirectoryRecursive(resourcesSource, exportRoot / "Resources", targetConfig == RuntimeExportTarget::Dist);
		else
			LUX_CONSOLE_LOG_WARN("Runtime export could not find an editor Resources directory to copy.");

		if (!WriteRuntimeShaderPack(exportRoot / "Assets" / s_RuntimeShaderPackFile))
			return;

		if (!monoSource.empty())
			CopyDirectoryRecursive(monoSource, exportRoot / "mono", targetConfig == RuntimeExportTarget::Dist);

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
			return;

		LUX_CONSOLE_LOG_INFO("Runtime export complete: {}", exportRoot.string());
		FileSystem::OpenDirectoryInExplorer(exportRoot);
	}

	void EditorLayer::NewScene()
	{
		m_EditorScene = CreateRef<Scene>();
		m_ActiveScene = m_EditorScene;
		m_PanelManager->SetSceneContext(m_ActiveScene);
		m_EditorScenePath = std::filesystem::path();

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
		m_ActiveScene = m_EditorScene;
		m_PanelManager->SetSceneContext(m_EditorScene);
		m_EditorScenePath = Project::GetActive()->GetEditorAssetManager()->GetFilePath(handle);

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

	void EditorLayer::OnScenePlay()
	{
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
	}

	void EditorLayer::OnSceneSimulate()
	{
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
