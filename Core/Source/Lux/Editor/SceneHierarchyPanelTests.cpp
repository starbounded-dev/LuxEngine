// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SceneHierarchyPanel.h"

#ifndef LUX_DIST
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Editor/EditorStack.h"
#include "Lux/ImGui/ImGuiScaling.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Scene/SceneSerializer.h"

#include <imgui/imgui_internal.h>
#include <stdexcept>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace Lux
{
	bool SceneHierarchyPanel::RunSelfTests(const std::filesystem::path& directory,
										   std::vector<std::string>& failures)
	{
#ifdef _MSC_VER
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* context = ImGui::CreateContext();
		const auto previousSelectionContext = s_ActiveSelectionContext;
		try
		{
			auto require = [](bool condition, const char* message)
			{
				if (!condition)
					throw std::runtime_error(message);
			};
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = {1920.0f, 1400.0f};
			io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigDebugHighlightIdConflicts = true;
			io.Fonts->AddFontDefault();
			unsigned char* pixels = nullptr;
			int width = 0, height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
			auto scene = Ref<Scene>::Create();
			auto first = scene->CreateEntity(std::string(350, 'A'));
			auto second = scene->CreateChildEntity(first, "Child");
			const UUID firstID = first.GetUUID(), secondID = second.GetUUID();
			SceneHierarchyPanel panel(scene, SelectionContext::PrefabEditor);
			panel.SetSelectedEntity(first);
			SelectionManager::Select(SelectionContext::PrefabEditor, secondID);
			auto frame = [&](float panelWidth, bool openPicker = false, bool focusName = false,
							 bool resetTranslation = false)
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowPos({0.0f, 0.0f});
				ImGui::SetNextWindowSize({panelWidth, 1150.0f});
				ImGui::Begin("Properties");
				if (openPicker)
					ImGui::OpenPopup("AddComponentPanel");
				if (focusName)
				{
					GImGui->NavActivateId = ImGui::GetID("##Tag");
					GImGui->NavActivateFlags = ImGuiActivateFlags_PreferInput;
				}
				if (resetTranslation)
				{
					ImGui::PushID(reinterpret_cast<const void*>(typeid(TransformComponent).hash_code()));
					ImGui::PushID("##section");
					ImGui::PushID("Translation");
					ImGui::PushID(0);
					const ImGuiID reset = ImGui::GetID("X");
					ImGui::PopID();
					ImGui::PopID();
					ImGui::PopID();
					ImGui::PopID();
					GImGui->NavActivateId = reset;
					GImGui->NavActivateDownId = reset;
					GImGui->NavActivatePressedId = reset;
				}
				panel.DrawComponents(SelectionManager::GetSelections(SelectionContext::PrefabEditor));
				ImGuiEx::BeginPropertyGrid();
				float value = 1.0f;
				ImGuiEx::Property("A long property label containing 100% literal text", value, 0.1f, 0.0f,
								  0.0f, "Help remains reachable");
				ImGuiEx::EndPropertyGrid();
				ImGui::End();
				ImGui::SetNextWindowPos({1000.0f, 0.0f});
				ImGui::SetNextWindowSize({panelWidth, 700.0f});
				bool open = true;
				panel.OnImGuiRender(open);
				ImGui::Render();
			};
			ImGuiEx::DpiStyle dpi;
			dpi.Capture(ImGui::GetStyle());
			for (float scale : {1.0f, 1.5f})
			{
				dpi.Apply(ImGui::GetStyle(), scale);
				ImGui::GetStyle().FontScaleMain = scale;
				for (float panelWidth : {800.0f, 280.0f, 190.0f, 800.0f})
				{
					frame(panelWidth);
					frame(panelWidth);
					const auto* properties = ImGui::FindWindowByName("Properties");
					if (properties->ContentSize.x > properties->Size.x)
						throw std::runtime_error(
							std::format("Inspector overflow width {} scale {}: content {} window {}",
										panelWidth, scale, properties->ContentSize.x, properties->Size.x));
					require(SelectionManager::GetSelectionCount(SelectionContext::PrefabEditor) == 2,
							"Reflow discarded the multi-selection");
				}
			}
			SelectionManager::DeselectAll(SelectionContext::PrefabEditor);
			ImGuiWindow* tree = nullptr;
			for (auto* window : GImGui->Windows)
				if (window->Active && std::strstr(window->Name, "##hierarchy_entities"))
					tree = window;
			require(tree, "Hierarchy tree region is missing");
			io.AddMousePosEvent(tree->WorkRect.Min.x + 40.0f,
								tree->WorkRect.Min.y + ImGui::GetFontSize() * 0.5f);
			for (int hoverFrame = 0; hoverFrame < 40; ++hoverFrame)
				frame(800.0f);
			io.AddMouseButtonEvent(0, true);
			frame(800.0f);
			io.AddMouseButtonEvent(0, false);
			frame(800.0f);
			require(panel.GetSelectedEntity() == first, "Entity tooltip stole the row's selection target");
			SelectionManager::Select(SelectionContext::PrefabEditor, secondID);
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
			first.GetComponent<TransformComponent>().Translation = {2.0f, 3.0f, 4.0f};
			second.GetComponent<TransformComponent>().Translation = {5.0f, 6.0f, 7.0f};
			frame(600.0f, false, false, true);
			require(first.GetComponent<TransformComponent>().Translation == glm::vec3(0.0f, 3.0f, 4.0f) &&
						second.GetComponent<TransformComponent>().Translation == glm::vec3(0.0f, 6.0f, 7.0f),
					"Axis reset changed other axes in a multi-selection");
			panel.m_IsPropertiesFocused = true;
			io.WantTextInput = true;
			KeyPressedEvent typingF(Key::F, 0), typingEscape(Key::Escape, 0);
			panel.OnPropertiesEvent(typingF);
			panel.OnPropertiesEvent(typingEscape);
			require(typingF.Handled && typingEscape.Handled && !panel.m_ActivateSearchWidget &&
						SelectionManager::GetSelectionCount(SelectionContext::PrefabEditor) == 2,
					"Text input leaked search/deselect commands");
			io.WantTextInput = false;
			panel.m_IsHierarchyFocused = true;
			io.KeyCtrl = true;
			KeyPressedEvent find(Key::F, 0);
			panel.OnPropertiesEvent(find);
			require(find.Handled && panel.m_ActivateSearchWidget, "Ctrl+F did not request hierarchy search");
			io.KeyCtrl = false;
			panel.m_ActivateSearchWidget = false;
			frame(600.0f, false, true);
			io.AddKeyEvent(ImGuiMod_Ctrl, true);
			io.AddKeyEvent(ImGuiKey_A, true);
			frame(600.0f);
			io.AddKeyEvent(ImGuiKey_A, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false);
			frame(600.0f);
			frame(600.0f);
			const std::string longName(400, 'B');
			io.AddInputCharactersUTF8(longName.c_str());
			frame(600.0f);
			frame(600.0f);
			if (first.GetName() != longName || second.GetName() != longName)
				throw std::runtime_error(std::format(
					"Inspector name mismatch: lengths {}/{} active {} input {}", first.GetName().size(),
					second.GetName().size(), GImGui->ActiveId, GImGui->InputTextState.ID));
			require(EditorStack::Get().HasPendingSceneEdit(), "Inspector edit did not signal undo");
			ImGui::ClearActiveID();
			frame(600.0f, true);
			frame(600.0f);
			io.AddInputCharactersUTF8("Camera");
			frame(600.0f);
			ImGuiWindow* choices = nullptr;
			for (auto* window : GImGui->Windows)
				if (window->Active && std::strstr(window->Name, "##component_choices"))
					choices = window;
			require(choices && panel.m_ComponentSearch == "Camera", "Component search lost input/focus");
			io.AddMousePosEvent(choices->WorkRect.Min.x + 60.0f,
								choices->WorkRect.Min.y + ImGui::GetFontSize() * 0.5f);
			frame(600.0f);
			io.AddMouseButtonEvent(0, true);
			frame(600.0f);
			io.AddMouseButtonEvent(0, false);
			frame(600.0f);
			require(first.HasComponent<CameraComponent>() && second.HasComponent<CameraComponent>(),
					"Filtered component addition did not apply to all selected entities");
			const std::string snapshot = SceneSerializer(scene).SerializeToString();
			auto restored = Ref<Scene>::Create();
			require(SceneSerializer(restored).DeserializeFromYAML(snapshot) &&
						restored->GetEntityByUUID(firstID).GetName() == longName &&
						restored->GetEntityByUUID(secondID).HasComponent<CameraComponent>(),
					"Inspector edits did not survive scene snapshot persistence");
			require(SceneSerializer(scene).Serialize(directory / "Assets/Scenes/Authored.luxscene"),
					"Cannot save the disposable authored scene for native verification");
			panel.m_SearchString = "BBBB";
			frame(280.0f);
			require(panel.m_VisibleEntityCount == 2, "Hierarchy search did not reveal a matching child");
			panel.SetSelectedEntity(first);
			first.GetComponent<TagComponent>().Locked = true;
			frame(190.0f);
			require(first.GetName() == longName, "Locked inspector rendering changed the entity");
			first.GetComponent<TagComponent>().Locked = false;
			auto folder = scene->CreateEntity("Folder");
			folder.AddComponent<FolderComponent>();
			panel.SetSelectedEntity(folder);
			frame(190.0f);
			panel.SetSelectedEntity(first);
			panel.m_SearchString = "unmatched";
			frame(280.0f);
			require(panel.m_VisibleEntityCount == 0, "Empty hierarchy search retained visible entities");
			auto parent = second;
			for (int depth = 0; depth < 12; ++depth)
				parent = scene->CreateChildEntity(parent, depth == 11 ? "Deep search result" : "Ancestor");
			panel.m_SearchString = "Deep search result";
			frame(280.0f);
			require(panel.m_VisibleEntityCount == 14, "Hierarchy search hid a result beyond ten ancestors");
			panel.m_QueuedEntityDeletions.push_back(firstID);
			panel.SetContext(restored);
			require(panel.m_QueuedEntityDeletions.empty() && panel.m_SearchString.empty() &&
						panel.m_ComponentSearch.empty() && !panel.m_IsPropertiesFocused,
					"Scene switch retained deferred actions or search/focus");
			panel.SetContext(nullptr);
			frame(190.0f);
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		SelectionManager::DeselectAll(SelectionContext::PrefabEditor);
		s_ActiveSelectionContext = previousSelectionContext;
		EditorStack::Get().ConsumeSceneEdit();
		EditorStack::Get().ConsumeLabel();
		EditorStack::Get().ClearEditScope();
		ImGui::DestroyContext(context);
		ImGui::SetCurrentContext(previous);
		return failures.empty();
	}
} // namespace Lux
#endif
