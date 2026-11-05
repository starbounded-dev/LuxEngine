// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ApplicationSettingsPanel.h"
#include "ContentBrowserPanel.h"
#include "HelpPanel.h"
#include "LightSettingsPanel.h"
#include "ProjectSettingsWindow.h"
#include "RuntimeExportUtils.h"

#ifndef LUX_DIST
#include "Lux/Core/Events/KeyEvent.h"
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
		void Frame(EditorPanel& panel, float width, float height = 720.0f)
		{
			// Help's FirstUseEver default overrides pending NextWindowData even on later frames.
			// Resize the existing window as the OS/dock splitter would, then render its settled frame.
			ImGui::SetWindowSize("Help", {width, height});
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
				Frame(lights, width);
				Frame(lights, width);
				CheckWidth("Light Settings");
			}
			lights.OnProjectChanged(nullptr);
			Require(!lights.GetSceneContext(), "Light settings retained the old project's scene");
			Frame(lights, 190.0f);

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
