// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ApplicationSettingsPanel.h"
#include "ContentBrowserPanel.h"
#include "HelpPanel.h"
#include "LightSettingsPanel.h"
#include "ProjectSettingsWindow.h"
#include "SceneRendererPanel.h"
#include "GameViewPanel.h"
#include "Viewport/Viewport.h"
#include "RuntimeExportUtils.h"

#ifndef LUX_DIST
#include "EditorLayer.h"
#include "Lux/Editor/SceneHierarchyPanel.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/SceneSerializer.h"
#include "Lux/ImGui/ImGuiScaling.h"
#include "Lux/ImGui/ImGuiWidgets.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/StringUtils.h"
#include "Lux/Utilities/FileSystem.h"
#include <imgui/imgui_internal.h>
#include <stdexcept>

namespace Lux
{
	namespace
	{
		struct SettingsTestContext
		{
			ImGuiContext* Previous = ImGui::GetCurrentContext();
			ImGuiContext* Context = ImGui::CreateContext();
			SettingsTestContext()
			{
				ImGui::SetCurrentContext(Context);
				auto& io = ImGui::GetIO();
				io.IniFilename = nullptr;
				io.DisplaySize = {1920.0f, 1080.0f};
				io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard;
				io.ConfigDebugHighlightIdConflicts = true;
				io.Fonts->AddFontDefault();
				unsigned char* pixels = nullptr;
				int width = 0, height = 0;
				io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
			}
			~SettingsTestContext()
			{
				ImGui::DestroyContext(Context);
				ImGui::SetCurrentContext(Previous);
			}
		};
		void Require(bool result, const char* reason)
		{
			if (!result)
				throw std::runtime_error(reason);
		}
		void Frame(EditorPanel& panel, float width, float height = 720.0f, const char* name = "Help")
		{
			// A FirstUseEver default overrides pending NextWindowData even on later frames.
			// Resize the existing window as the OS/dock splitter would, then render its settled frame.
			ImGui::SetWindowSize(name, {width, height});
			ImGui::NewFrame();
			ImGui::SetNextWindowPos({0.0f, 0.0f});
			ImGui::SetNextWindowSize({width, height});
			bool open = true;
			panel.OnImGuiRender(open);
			ImGui::Render();
		}
		void CheckWidth(const char* rootName)
		{
			auto* root = ImGui::FindWindowByName(rootName);
			if (!root || root->ContentSize.x > root->Size.x)
				throw std::runtime_error(std::format("{} width {} content {}", rootName,
													 root ? root->Size.x : 0.0f,
													 root ? root->ContentSize.x : 0.0f));
		}
		Yaml::Node StateOf(const EditorPanel& panel)
		{
			Yaml::Writer out;
			out << Yaml::BeginMap;
			panel.SaveState(out);
			out << Yaml::EndMap;
			return Yaml::Load(out.str());
		}
	} // namespace

	bool EditorLayer::RunViewportOverlaySelfTests(std::vector<std::string>& failures)
	{
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* context = ImGui::CreateContext(previous->IO.Fonts);
		ImGui::SetCurrentContext(context);
		const auto originalViewport = m_EditorViewport;
		const auto originalScene = m_ActiveScene;
		const auto originalHierarchy = m_SceneHierarchyPanel;
		const auto originalSelection = SelectionManager::GetSelections(SelectionContext::PrefabEditor);
		const std::string originalBadgeName = m_SelectionBadgeName;
		const std::string originalBadgeLabel = m_SelectionBadgeLabel;
		const bool originalHUD = m_ShowViewportPerformanceHUD;
		const bool originalCategories = m_SceneRenderer->GetOptions().ShowDebugCategories;
		try
		{
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = {1920.0f, 1080.0f};
			io.BackendFlags = previous->IO.BackendFlags & ImGuiBackendFlags_RendererHasTextures;
			m_EditorViewport = Ref<Viewport>::Create("Scene View");
			m_ActiveScene = Ref<Scene>::Create();
			m_SceneHierarchyPanel =
				Ref<SceneHierarchyPanel>::Create(m_ActiveScene, SelectionContext::PrefabEditor);
			Entity selected = m_ActiveScene->CreateEntity(std::string(300, 'N') + " ## 100%");
			m_SceneHierarchyPanel->SetSelectedEntity(selected);
			selected.AddComponent<MeshColliderComponent>();
			m_ShowViewportPerformanceHUD = true;
			m_SceneRenderer->GetOptions().ShowDebugCategories = true;
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (const ImVec2 size : {ImVec2{900.0f, 600.0f}, ImVec2{180.0f, 140.0f}})
				{
					for (int frame = 0; frame < 3; ++frame)
					{
						ImGui::NewFrame();
						ImGui::SetNextWindowPos({50.0f, 50.0f});
						ImGui::SetNextWindowSize({size.x * scale, size.y * scale});
						m_EditorViewport->BeginImGui();
						UI_ViewportPerformanceHUD();
						UI_AcousticMaterialLegend();
						m_EditorViewport->EndImGui();
						UI_ViewportSettings();
						UI_ViewportOrientationGizmo();
						UI_ViewportSelectionBadge();
						ImGui::Render();
					}
					const auto* bounds = m_EditorViewport->GetBounds();
					for (const char* name : {"##viewport_performance_hud", "##acoustic_material_legend",
											 "##viewport_settings_toolbar", "##viewport_orientation_gizmo",
											 "##viewport_selection_badge"})
					{
						const auto* window = ImGui::FindWindowByName(name);
						Require(window && window->Active, "A Scene View overlay was not submitted");
						Require(window->Viewport->ID == m_EditorViewport->GetPlatformViewportID(),
								"A Scene View overlay targeted a different platform viewport");
						if (window->Pos.x < bounds[0].x - 1.0f || window->Pos.y < bounds[0].y - 1.0f ||
							window->Pos.x + window->Size.x > bounds[1].x + 1.0f ||
							window->Pos.y + window->Size.y > bounds[1].y + 1.0f)
							throw std::runtime_error(std::format("Scene overlay {} escaped {}x{} at scale {}",
																 name, size.x, size.y, scale));
					}
					const auto* hud = ImGui::FindWindowByName("##viewport_performance_hud");
					const auto* legend = ImGui::FindWindowByName("##acoustic_material_legend");
					if (size.x >= 900.0f)
						Require(hud->InnerRect.GetWidth() >= ImGui::CalcTextSize("FPS 60  16.67 ms").x,
								"Wide Scene View collapsed the HUD to a vertical text column");
					Require(legend->Pos.x + legend->Size.x <= hud->Pos.x,
							"Scene View diagnostic overlays overlapped");
				}
			}
			m_EditorViewport->Hide();
			Require(m_EditorViewport->GetPlatformViewportID() == 0 &&
						!m_EditorViewport->PrepareOverlay({0.0f, 0.0f}),
					"Hidden Scene View retained overlay submission state");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		m_EditorViewport = originalViewport;
		m_ActiveScene = originalScene;
		m_SceneHierarchyPanel = originalHierarchy;
		m_SelectionBadgeName = originalBadgeName;
		m_SelectionBadgeLabel = originalBadgeLabel;
		SelectionManager::DeselectAll(SelectionContext::PrefabEditor);
		for (UUID id : originalSelection)
			SelectionManager::Select(SelectionContext::PrefabEditor, id);
		m_ShowViewportPerformanceHUD = originalHUD;
		m_SceneRenderer->GetOptions().ShowDebugCategories = originalCategories;
		ImGui::DestroyContext(context);
		ImGui::SetCurrentContext(previous);
		return failures.empty();
	}

	bool SceneRendererPanel::RunPanelSelfTests(Ref<SceneRenderer> renderer,
											   std::vector<std::string>& failures)
	{
		SettingsTestContext context;
		auto project = Project::GetActive();
		if (!renderer || !project)
		{
			failures.push_back("Scene Renderer tests need the runner's renderer and private project");
			return false;
		}
		const auto originalConfig = project->GetConfig().SceneRenderer;
		const auto originalOptions = renderer->GetOptions();
		try
		{
			SceneRendererPanel panel;
			panel.SetContext(renderer);
			panel.SetSceneContext(Ref<Scene>::Create());
			const std::string sceneBefore = SceneSerializer(panel.m_Scene).SerializeToString();
			ProjectSceneRendererSettings before;
			renderer->WriteProjectSettings(before);
			panel.LoadState(Yaml::Load("ShadowsOpen: true\nPostFXOpen: true\nColorGradingOpen: true\n"));
			const auto state = StateOf(panel);
			Require(state["ShadowsOpen"].as<bool>() && state["PostFXOpen"].as<bool>(),
					"Scene Renderer lost existing card layout state");
			ImGui::NewFrame();
			bool open = true;
			panel.OnImGuiRender(open);
			ImGui::Render();
			Require(ImGui::FindWindowByName("Scene Renderer")->Size.x >= 300.0f,
					"Scene Renderer first open collapsed to wrapped-content auto-fit");
			const auto frame = [&](float width, float height = 720.0f)
			{
				ImGui::SetWindowSize("Scene Renderer", {width, height});
				Frame(panel, width, height);
			};
			const auto check = [&]()
			{
				CheckWidth("Scene Renderer");
				for (ImGuiWindow* window : GImGui->Windows)
				{
					if (!window->Active || window->SkipItems ||
						window->RootWindow != ImGui::FindWindowByName("Scene Renderer"))
						continue;
					if (window->DC.CursorMaxPos.x > window->InnerRect.Max.x + 1.0f)
						throw std::runtime_error(std::format(
							"Renderer child {} width {} content {} query {} scale {} mode {} submitted {} "
							"skip {}",
							window->Name, window->Size.x, window->ContentSize.x, panel.m_SearchBuffer,
							ImGui::GetStyle().FontScaleMain,
							static_cast<int>(renderer->GetOptions().ResolutionScaleMode),
							window->DC.CursorMaxPos.x - window->DC.CursorStartPos.x, window->SkipItems));
				}
			};
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (auto mode : {SceneRendererOptions::RenderResolutionScaleMode::Native,
								  SceneRendererOptions::RenderResolutionScaleMode::FixedResolution,
								  SceneRendererOptions::RenderResolutionScaleMode::Dynamic})
				{
					renderer->GetOptions().ResolutionScaleMode = mode;
					panel.m_CardDebugViews =
						mode != SceneRendererOptions::RenderResolutionScaleMode::FixedResolution;
					for (float width : {1400.0f, 650.0f, 280.0f, 190.0f, 1400.0f})
					{
						frame(width);
						frame(width);
						check();
					}
					for (const char* query :
						 {"Show", "Shadows", "Bloom", "GTAO", "Tonemap", "Saturation", "Render"})
					{
						std::snprintf(panel.m_SearchBuffer, sizeof(panel.m_SearchBuffer), "%s", query);
						frame(190.0f);
						frame(190.0f);
						check();
					}
					panel.m_SearchBuffer[0] = '\0';
				}
			}
			renderer->GetOptions().ResolutionScaleMode = originalOptions.ResolutionScaleMode;
			ProjectSceneRendererSettings after;
			renderer->WriteProjectSettings(after);
			Require(before == after && !panel.m_ProjectRendererSettingsDirty &&
						sceneBefore == SceneSerializer(panel.m_Scene).SerializeToString(),
					"Viewing renderer cards changed authored renderer or scene data");
			std::snprintf(panel.m_SearchBuffer, sizeof(panel.m_SearchBuffer), "%s", "shadows");
			panel.m_CardShadows = false;
			frame(190.0f);
			frame(190.0f);
			check();
			Require(!panel.m_CardShadows, "Searching changed the saved card collapse preference");
			panel.m_SearchBuffer[0] = '\0';
			panel.SetDebugViewsRuntimeSuspended(true);
			frame(190.0f);
			frame(190.0f);
			check();
			frame(190.0f, 40.0f);
			frame(1400.0f);
			panel.m_IsFocused = true;
			ImGui::GetIO().WantTextInput = true;
			KeyPressedEvent typing(Key::D, 0);
			panel.OnEvent(typing);
			Require(typing.Handled, "Renderer numeric/text editing leaked a scene shortcut");
			panel.OnClose();
			Require(!panel.m_IsFocused, "Hidden renderer controls retained keyboard focus");
			const bool authoredCulling = !renderer->GetOptions().EnableFrustumCulling;
			renderer->GetOptions().EnableFrustumCulling = authoredCulling;
			Require(panel.SaveProjectRendererSettings(), "Renderer settings save failed in private project");
			Require(!panel.m_ProjectRendererSettingsDirty && panel.m_SaveError.empty(),
					"Renderer save left stale dirty/error status");
			const auto path = project->GetProjectFilePath();
			Require(Yaml::LoadFile(path)["Project"]["SceneRenderer"]["Rendering"]["FrustumCulling"].as<bool>() ==
						authoredCulling,
					"Renderer settings save did not persist the authored value");
			auto backup = path;
			backup += ".renderer-save-test";
			Require(FileSystem::Rename(path, backup), "Cannot preserve private renderer save fixture");
			const bool madeDirectory = FileSystem::CreateDirectory(path);
			const bool failedSave = madeDirectory && !panel.SaveProjectRendererSettings();
			const bool retained = panel.m_ProjectRendererSettingsDirty && !panel.m_SaveError.empty();
			std::error_code removeError;
			if (madeDirectory)
				std::filesystem::remove(path, removeError);
			Require(FileSystem::Rename(backup, path), "Cannot restore private renderer save fixture");
			Require(failedSave && retained, "Failed renderer save lost dirty/error indication");
			frame(190.0f);
			frame(190.0f);
			check();
			Require(panel.SaveProjectRendererSettings() && panel.m_SaveError.empty(),
					"Renderer save did not recover after destination repair");
			panel.OnProjectChanged(nullptr);
			Require(!panel.m_IsFocused && !panel.m_ProjectRendererSettingsDirty && panel.m_SaveError.empty(),
					"Renderer project change retained old transient state");
			panel.SetContext(nullptr);
			frame(190.0f);
			frame(190.0f);
			check();
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		renderer->GetOptions() = originalOptions;
		project->GetConfig().SceneRenderer = originalConfig;
		return failures.empty();
	}

	bool GameViewPanel::RunPanelSelfTests(std::vector<std::string>& failures)
	{
		SettingsTestContext context;
		try
		{
			GameViewPanel panel;
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (float width : {1400.0f, 650.0f, 280.0f, 190.0f, 1400.0f})
				{
					ImGui::SetWindowSize("Game View", {width, 600.0f});
					Frame(panel, width, 600.0f);
					ImGui::SetWindowSize("Game View", {width, 600.0f});
					Frame(panel, width, 600.0f);
					const auto* window = ImGui::FindWindowByName("Game View");
					const auto* bounds = panel.m_Viewport->GetBounds();
					bool captionFound = false;
					for (const ImDrawVert& vertex : window->DrawList->VtxBuffer)
					{
						if (vertex.col != Colors::Theme::textCaption)
							continue;
						captionFound = true;
						Require(vertex.pos.x >= bounds[0].x && vertex.pos.x <= bounds[1].x,
								"No Camera guidance is clipped at narrow Game View widths");
					}
					Require(captionFound && !panel.m_RendererCreated,
							"Empty Game View lost its guidance or allocated a renderer while drawing");
				}
			}
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		return failures.empty();
	}

	bool ApplicationSettingsPanel::RunSelfTests(std::vector<std::string>& failures)
	{
		SettingsTestContext context;
		try
		{
			bool vsync = false;
			int rate = 120;
			SceneRenderer::GridSettings grid;
			EditorPreferencesBindings bindings;
			bindings.VSync = &vsync;
			bindings.TargetFrameRate = &rate;
			bindings.Grid = &grid;
			ApplicationSettingsPanel panel(nullptr, bindings, nullptr);
			panel.LoadState(Yaml::Load("Page: Viewport\nPageFraction: 0.37\n"));
			const auto state = StateOf(panel);
			Require(state["Page"].as<std::string>() == "Viewport", "Application page did not round-trip");
			const float fraction = panel.m_PageFraction;
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (uint32_t page = 0; page < panel.m_Pages.size(); ++page)
				{
					panel.m_CurrentPage = page;
					for (float width : {1200.0f, 500.0f, 280.0f, 190.0f, 1200.0f})
					{
						Frame(panel, width);
						Frame(panel, width);
						CheckWidth("Application Settings");
						Require(panel.m_PageFraction == fraction,
								"Reflow overwrote application sidebar preference");
					}
				}
			}
			panel.LoadState(Yaml::Load("Page: Editor\n"));
			Frame(panel, 190.0f);
			Frame(panel, 190.0f);
			auto* settings = ImGui::FindWindowByName("Application Settings");
			auto& io = ImGui::GetIO();
			io.AddMousePosEvent(settings->WorkRect.Min.x + 20.0f,
								settings->WorkRect.Min.y + settings->FontRefSize * 0.5f);
			Frame(panel, 190.0f);
			io.AddMouseButtonEvent(0, true);
			Frame(panel, 190.0f);
			io.AddMouseButtonEvent(0, false);
			Frame(panel, 190.0f);
			Require(!GImGui->OpenPopupStack.empty(), "Narrow settings page selector did not open");
			io.AddKeyEvent(ImGuiKey_DownArrow, true);
			Frame(panel, 190.0f);
			io.AddKeyEvent(ImGuiKey_DownArrow, false);
			Frame(panel, 190.0f);
			Frame(panel, 190.0f);
			io.AddKeyEvent(ImGuiKey_Enter, true);
			Frame(panel, 190.0f);
			io.AddKeyEvent(ImGuiKey_Enter, false);
			Frame(panel, 190.0f);
			Require(panel.m_CurrentPage == 1,
					"Narrow settings selector did not switch pages with the keyboard");
			// A completely clipped child must still close before the next panel/frame.
			Frame(panel, 190.0f, 40.0f);
			Frame(panel, 1200.0f);
			panel.m_IsFocused = true;
			ImGui::GetIO().WantTextInput = true;
			KeyPressedEvent typing(Key::D, 0);
			panel.OnEvent(typing);
			Require(typing.Handled, "Settings typing leaked a scene shortcut");
			panel.OnClose();
			Require(!panel.m_IsFocused, "Hidden settings retained keyboard focus");
			Require(!vsync && rate == 120 && grid.CellSize == SceneRenderer::GridSettings{}.CellSize,
					"Viewing settings changed preferences");
			panel.LoadState(Yaml::Load("Page: obsolete\nPageFraction: .nan\n"));
			Require(panel.m_PageFraction == fraction, "Invalid layout state poisoned sidebar sizing");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		return failures.empty();
	}

	bool HelpPanel::RunSelfTests(std::vector<std::string>& failures)
	{
		SettingsTestContext context;
		try
		{
			HelpPanel panel;
			Require(!panel.m_RootMissing && panel.FindTopic("editor/interface"),
					"Bundled manual fixture missing");
			panel.LoadState(Yaml::Load("Page: editor/interface\nTopicFraction: 0.35\nShowTopics: false\n"));
			Require(panel.m_CurrentTopic == "editor/interface" && !panel.m_FocusRequested,
					"Help restore lost its page or stole focus");
			Require(panel.ResolveLink("../components/transform.md#position") == "components/transform",
					"Manual relative links lost their destination");
			const auto state = StateOf(panel);
			Require(state["Page"].as<std::string>() == "editor/interface" && !state["ShowTopics"].as<bool>(),
					"Help presentation state did not round-trip");
			const float fraction = panel.m_TopicFraction;
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (float width : {1200.0f, 500.0f, 280.0f, 190.0f, 1200.0f})
				{
					Frame(panel, width);
					Frame(panel, width);
					CheckWidth("Help");
					Require(panel.m_TopicFraction == fraction && panel.m_CurrentTopic == "editor/interface",
							"Help reflow lost page or sidebar proportion");
				}
			}
			panel.m_FocusSearch = true;
			Frame(panel, 280.0f);
			Frame(panel, 280.0f);
			ImGui::GetIO().AddInputCharactersUTF8("transform");
			Frame(panel, 280.0f);
			Frame(panel, 280.0f);
			if (panel.m_Search != "transform" || !panel.m_NarrowTopics)
				throw std::runtime_error(std::format(
					"Help search '{}' topics {} split {} focused {} active {}", panel.m_Search,
					panel.m_NarrowTopics, panel.m_SideBySide, panel.m_IsFocused, GImGui->ActiveId));
			ImGui::ClearActiveID();
			auto* root = ImGui::FindWindowByName("Help");
			ImGuiWindow* topics = nullptr;
			for (auto* window : GImGui->Windows)
				if (window->Active && window->ParentWindow == root && std::strstr(window->Name, "##topics"))
					topics = window;
			Require(topics, "Help search results child missing");
			auto& io = ImGui::GetIO();
			io.AddMousePosEvent(topics->WorkRect.Min.x + 20.0f,
								topics->WorkRect.Min.y + topics->FontRefSize * 0.5f);
			Frame(panel, 280.0f);
			io.AddMouseButtonEvent(0, true);
			Frame(panel, 280.0f);
			io.AddMouseButtonEvent(0, false);
			Frame(panel, 280.0f);
			Frame(panel, 280.0f);
			Require(!panel.m_NarrowTopics, "Selecting a narrow Help result did not return to its page");
			panel.m_IsFocused = true;
			io.WantTextInput = true;
			KeyPressedEvent typing(Key::F, 0);
			panel.OnEvent(typing);
			Require(typing.Handled, "Manual search leaked a scene shortcut");
			panel.OnClose();
			Require(!panel.m_IsFocused && !panel.m_FocusSearch, "Closing Help retained focus");
			panel.LoadState(Yaml::Load("TopicFraction: .nan\n"));
			Require(panel.m_TopicFraction == fraction, "Invalid Help ratio poisoned layout state");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		return failures.empty();
	}

	bool ProjectSettingsWindow::RunPanelSelfTests(std::vector<std::string>& failures)
	{
		SettingsTestContext context;
		try
		{
			RuntimeExport::RuntimeBuildJob job;
			ProjectSettingsWindow panel(job);
			auto project = Project::GetActive();
			Require(project != nullptr, "Project settings require the disposable runner project");
			panel.OnProjectChanged(project);
			panel.LoadState(Yaml::Load("Page: Physics\n"));
			Require(StateOf(panel)["Page"].as<std::string>() == "Physics",
					"Project section did not round-trip");
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (uint32_t page = 0; page < 7; ++page)
				{
					panel.m_CurrentPage = page;
					panel.m_OpenPage = true;
					for (float width : {1200.0f, 500.0f, 280.0f, 190.0f, 1200.0f})
					{
						Frame(panel, width);
						Frame(panel, width);
						CheckWidth("Project Settings");
					}
				}
			}
			Require(!panel.m_Dirty && !job.IsRunning(),
					"Viewing settings changed project data or launched a build");
			const std::string name = project->GetConfig().Name;
			project->GetConfig().Name = "SettingsSaved";
			panel.m_Dirty = true;
			panel.SaveProject();
			Require(!panel.m_Dirty &&
						Yaml::LoadFile(project->GetProjectFilePath())["Project"]["Name"].as<std::string>() ==
							"SettingsSaved",
					"Project settings save did not persist authored data");
			project->GetConfig().Name = name;
			panel.m_Dirty = true;
			panel.SaveProject();
			const auto path = project->GetProjectFilePath();
			auto backup = path;
			backup += ".before-save-test";
			Require(FileSystem::Rename(path, backup), "Could not preserve the private project save fixture");
			Require(FileSystem::CreateDirectory(path), "Could not construct the private failed-save fixture");
			panel.m_Dirty = true;
			panel.SaveProject();
			const bool retainedDirty = panel.m_Dirty;
			std::error_code removeError;
			Require(std::filesystem::remove(path, removeError) && FileSystem::Rename(backup, path),
					"Could not restore the private project save fixture");
			Require(retainedDirty, "A failed project save cleared the unsaved indication");
			panel.SaveProject();
			Require(!panel.m_Dirty, "Retrying a project save did not clear the dirty state");
			panel.m_IsFocused = true;
			ImGui::GetIO().WantTextInput = true;
			KeyPressedEvent typing(Key::D, 0);
			panel.OnEvent(typing);
			Require(typing.Handled, "Project text editing leaked a scene shortcut");
			panel.OnClose();
			Require(!panel.m_IsFocused, "Closing project settings retained focus");
			panel.m_LegacyErrors.push_back("old project cleanup report");
			panel.OnProjectChanged(nullptr);
			Require(!panel.m_Project && !panel.m_Dirty && !panel.m_DialoguePreview,
					"Project change retained settings context/audition");
			Require(panel.m_LegacyErrors.empty() && !panel.m_LegacyScan,
					"Project change retained old audio cleanup context");

			LightSettingsPanel lights;
			for (int frame = 0; frame < 3; ++frame)
			{
				ImGui::NewFrame();
				bool open = true;
				lights.OnImGuiRender(open);
				ImGui::Render();
			}
			Require(ImGui::FindWindowByName("Light Settings")->Size.x >= 200.0f,
				"Light Settings first floating open collapsed to unusable width");
			auto scene = Ref<Scene>::Create();
			Entity entity = scene->CreateEntity(std::string(200, 'L') + " ## literal 100%");
			entity.AddComponent<DirectionalLightComponent>();
			entity.AddComponent<PointLightComponent>();
			entity.AddComponent<SpotLightComponent>();
			entity.AddComponent<SkyLightComponent>();
			lights.SetSceneContext(scene);
			lights.OnProjectChanged(project);
			Require(lights.GetSceneContext() == scene,
					"Project notification discarded the newly installed light scene");
			for (float width : {1200.0f, 280.0f, 190.0f, 1200.0f})
			{
				Frame(lights, width, 720.0f, "Light Settings");
				Frame(lights, width, 720.0f, "Light Settings");
				CheckWidth("Light Settings");
			}
			lights.OnProjectChanged(nullptr);
			Require(!lights.GetSceneContext(), "Light settings retained the old project's scene");
			Frame(lights, 190.0f, 720.0f, "Light Settings");

			glm::vec3 values(1.0f, 2.0f, 3.0f);
			bool manuallyEdited = false;
			auto vectorFrame = [&]()
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowPos({0.0f, 0.0f});
				ImGui::SetNextWindowSize({190.0f, 350.0f});
				ImGui::Begin("Vector controls");
				ImGuiEx::Widgets::EditVec3("##value", {ImGui::GetContentRegionAvail().x, 0.0f}, 0.0f,
										   manuallyEdited, values);
				ImGui::End();
				ImGui::Render();
			};
			vectorFrame();
			vectorFrame();
			CheckWidth("Vector controls");
			auto* vectorWindow = ImGui::FindWindowByName("Vector controls");
			auto& io = ImGui::GetIO();
			io.AddMousePosEvent(vectorWindow->WorkRect.Min.x + 10.0f, vectorWindow->WorkRect.Min.y + 12.0f);
			vectorFrame();
			io.AddMouseButtonEvent(0, true);
			vectorFrame();
			io.AddMouseButtonEvent(0, false);
			vectorFrame();
			Require(values == glm::vec3(0.0f, 2.0f, 3.0f),
					"Stacked vector axis reset changed the wrong values");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		return failures.empty();
	}
} // namespace Lux
#endif
