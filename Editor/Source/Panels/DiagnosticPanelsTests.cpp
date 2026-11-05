// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AssetManagerPanel.h"
#include "UndoHistoryPanel.h"

#ifndef LUX_DIST
#include "Lux/Asset/AssetManager.h"
#include "Lux/ImGui/ImGuiScaling.h"
#include "Lux/Project/Project.h"
#include "Lux/Utilities/StringUtils.h"
#include <imgui/imgui_internal.h>
#include <stdexcept>

namespace Lux
{
	namespace
	{
		struct DiagnosticTestContext
		{
			ImGuiContext* Previous = ImGui::GetCurrentContext();
			ImGuiContext* Context = ImGui::CreateContext();
			DiagnosticTestContext()
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
			~DiagnosticTestContext()
			{
				ImGui::SetCurrentContext(Context);
				ImGui::DestroyContext(Context);
				ImGui::SetCurrentContext(Previous);
			}
		};
		void Require(bool value, const char* reason)
		{
			if (!value)
				throw std::runtime_error(reason);
		}
	} // namespace

	bool AssetManagerPanel::RunSelfTests(std::vector<std::string>& failures)
	{
		DiagnosticTestContext context;
		try
		{
			AssetManagerPanel panel;
			for (int frame = 0; frame < 3; ++frame)
			{
				ImGui::NewFrame();
				bool open = true;
				panel.OnImGuiRender(open);
				ImGui::Render();
			}
			Require(ImGui::FindWindowByName("Asset Manager")->Size.x >= 200.0f,
				"Asset Manager first floating open collapsed to unusable width");
			auto manager = Project::GetEditorAssetManager();
			Require(manager != nullptr, "Asset diagnostics require the private runner project");
			const AssetHandle first = AssetManager::ImportAsset("One/Duplicate.cs");
			const AssetHandle second = AssetManager::ImportAsset("Two/Duplicate.cs");
			Require(first && second && first != second, "Asset diagnostic fixtures lost distinct handles");
			auto frame = [&](float width)
			{
				ImGui::SetWindowSize("Asset Manager", {width, 650.0f});
				ImGui::NewFrame();
				ImGui::SetNextWindowPos({0.0f, 0.0f});
				ImGui::SetNextWindowSize({width, 650.0f});
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
				for (float width : {1300.0f, 280.0f, 190.0f, 1300.0f})
				{
					frame(width);
					frame(width);
					const auto* window = ImGui::FindWindowByName("Asset Manager");
					Require(window && window->ContentSize.x <= window->Size.x,
							"Asset diagnostics overflow at narrow/scaled width");
					Require(panel.m_Rows.size() == manager->GetAssetRegistry().Count(),
							"Registry snapshot omitted metadata");
				}
			}
			panel.m_FocusSearch = true;
			frame(1300.0f);
			frame(1300.0f);
			ImGui::GetIO().AddInputCharactersUTF8("Two/");
			frame(1300.0f);
			frame(1300.0f);
			Require(panel.m_VisibleRows.size() == 1 && panel.m_Rows[panel.m_VisibleRows[0]].ID == second,
					"Registry path search confused duplicate basenames");
			ImGui::ClearActiveID();
			const auto original = manager->GetMetadata(second);
			auto changed = original;
			changed.FilePath = std::filesystem::path(u8"Two/\u00e9##Renamed.cs");
			changed.Status = AssetStatus::Missing;
			manager->SetMetadata(second, changed);
			std::strcpy(panel.m_SearchBuffer, "missing");
			panel.m_Query = "missing";
			frame(280.0f);
			frame(280.0f);
			Require(panel.m_VisibleRows.size() == 1 &&
						panel.m_Rows[panel.m_VisibleRows[0]].Path == Utils::PathToUTF8(changed.FilePath),
					"Metadata revision did not refresh Unicode path/status search");
			auto* root = ImGui::FindWindowByName("Asset Manager");
			auto* table = GImGui->Tables.GetByKey(root->GetID("##asset_registry_compact"));
			Require(table && table->InnerWindow, "Compact asset region is missing");
			auto& io = ImGui::GetIO();
			io.AddMousePosEvent(table->InnerWindow->InnerRect.GetCenter().x,
								(table->RowPosY1 + table->RowPosY2) * 0.5f);
			frame(280.0f);
			io.AddMouseButtonEvent(0, true);
			frame(280.0f);
			io.AddMouseButtonEvent(0, false);
			frame(280.0f);
			Require(panel.m_Inspecting == second && !GImGui->OpenPopupStack.empty(),
					"Asset row did not open complete metadata details");
			panel.OnClose();
			Require(GImGui->OpenPopupStack.empty(), "Closing asset diagnostics retained its popup");
			panel.m_ShowOnlyLoaded = true;
			frame(280.0f);
			for (size_t index : panel.m_VisibleRows)
				Require(AssetManager::IsAssetLoaded(panel.m_Rows[index].ID),
						"Only loaded included an unloaded asset");
			manager->SetMetadata(second, original);
			panel.OnProjectChanged(nullptr);
			Require(panel.m_Rows.empty() && !panel.m_RegistryOwner && !panel.m_Inspecting &&
						!panel.m_SearchBuffer[0],
					"Project switch retained asset diagnostic context");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		return failures.empty();
	}

	bool UndoHistoryPanel::RunSelfTests(std::vector<std::string>& failures)
	{
		DiagnosticTestContext context;
		try
		{
			std::vector<std::string> undo(5, std::string(200, 'R') + " ## literal 100%");
			std::vector<std::string> redo(2, "Future state");
			bool available = true;
			int undoSteps = 0, redoSteps = 0;
			Bindings bindings;
			bindings.UndoLabels = [&]() { return undo; };
			bindings.RedoLabels = [&]() { return redo; };
			bindings.IsAvailable = [&]() { return available; };
			bindings.Undo = [&](int steps)
			{
				Require(std::strcmp(ImGui::GetCurrentWindow()->Name, "History") != 0,
						"Undo ran inside History's draw scope");
				undoSteps = steps;
			};
			bindings.Redo = [&](int steps) { redoSteps = steps; };
			UndoHistoryPanel panel(std::move(bindings));
			auto frame = [&](float width)
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowPos({0.0f, 0.0f});
				ImGui::SetNextWindowSize({width, 650.0f});
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
				for (float width : {900.0f, 280.0f, 190.0f, 900.0f})
				{
					frame(width);
					frame(width);
					const auto* window = ImGui::FindWindowByName("History");
					Require(window && window->ContentSize.x <= window->Size.x,
							"History labels/controls overflow at narrow width");
				}
			}
			auto* tree = ImGui::FindWindowByName("History");
			ImGuiWindow* states = nullptr;
			for (auto* window : GImGui->Windows)
				if (window->Active && window->ParentWindow == tree &&
					std::strstr(window->Name, "##history_states"))
					states = window;
			Require(states, "History states region is missing");
			auto& io = ImGui::GetIO();
			const float rowHeight = states->FontRefSize + ImGui::GetStyle().FramePadding.y * 2.0f +
									ImGui::GetStyle().ItemSpacing.y;
			auto click = [&](int row)
			{
				io.AddMousePosEvent(states->WorkRect.Min.x + 30.0f,
									states->WorkRect.Min.y + rowHeight * static_cast<float>(row) +
										(rowHeight - ImGui::GetStyle().ItemSpacing.y) * 0.5f);
				frame(900.0f);
				io.AddMouseButtonEvent(0, true);
				frame(900.0f);
				io.AddMouseButtonEvent(0, false);
				frame(900.0f);
			};
			click(0);
			Require(undoSteps == 5 && redoSteps == 0,
					"History's first row did not reach the state before all edits");
			io.AddMousePosEvent(states->InnerRect.GetCenter().x, states->InnerRect.GetCenter().y);
			frame(900.0f);
			io.AddMouseWheelEvent(0.0f, -3.0f);
			frame(900.0f);
			frame(900.0f);
			click(7);
			if (redoSteps != 2)
				throw std::runtime_error(
					std::format("History future click: redo {} undo {} scroll {} min {} rowheight {}",
								redoSteps, undoSteps, states->Scroll.y, states->WorkRect.Min.y, rowHeight));
			available = false;
			undoSteps = redoSteps = 0;
			frame(190.0f);
			Require(!undoSteps && !redoSteps, "Unavailable history invoked an action");
			available = true;
			undo.clear();
			redo.clear();
			frame(190.0f);
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		return failures.empty();
	}
} // namespace Lux
#endif
