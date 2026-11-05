// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "TextEditorPanel.h"

#ifndef LUX_DIST
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Utilities/FileSystem.h"

#include <imgui/imgui_internal.h>

#include <fstream>
#include <stdexcept>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace Lux
{
	namespace
	{
		void Require(bool condition, const char* message)
		{
			if (!condition)
				throw std::runtime_error(message);
		}

		std::string Read(const std::filesystem::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			Require(static_cast<bool>(file), "Cannot read saved Beam document");
			return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		}
	} // namespace

	bool TextEditorPanel::RunPanelControlsSelfTests(const std::filesystem::path& directory,
													std::vector<std::string>& failures)
	{
		ImGuiContext* previous = ImGui::GetCurrentContext();
#ifdef _MSC_VER
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
		ImGuiContext* context = ImGui::CreateContext();
		ImGui::SetCurrentContext(context);
		try
		{
			Require(!FileSystem::Exists(directory), "Panel-control fixture directory must be new");
			Require(FileSystem::CreateDirectory(directory), "Cannot create panel-control fixtures");
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = {1920.0f, 1080.0f};
			io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard;
			io.Fonts->AddFontDefault();
			unsigned char* pixels = nullptr;
			int fontWidth = 0, fontHeight = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &fontWidth, &fontHeight);
			const ImGuiEx::PanelAction actions[] = {
				{11, "New", "+", "Ctrl+N", true, false, true},
				{12, "Save", "S", "Ctrl+S", false, false, true},
				{13, "Find", "F", "Ctrl+F"},
				{14, "Graph", "G", nullptr, true, true},
				{15, "Revert", nullptr, nullptr, true, false, false, true}};
			ImGuiWindow* toolbar = nullptr;
			auto controlsFrame = [&](float width)
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowPos({0.0f, 0.0f});
				ImGui::SetNextWindowSize({width, 400.0f});
				ImGui::Begin("Panel control test", nullptr,
							 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
				const int activated = ImGuiEx::PanelActionBar("test_actions", actions);
				for (ImGuiWindow* window : GImGui->Windows)
					if (window->ParentWindow == ImGui::GetCurrentWindow() &&
						window->ChildId == ImGui::GetID("test_actions"))
						toolbar = window;
				ImGui::End();
				ImGui::Render();
				return activated;
			};
			for (float factor : {1.0f, 1.5f, 2.0f})
			{
				ImGui::GetStyle() = ImGuiStyle{};
				ImGui::GetStyle().ScaleAllSizes(factor);
				ImGui::GetStyle().FontScaleMain = factor;
				for (float width : {900.0f, 340.0f, 190.0f, 95.0f, 48.0f, 900.0f})
				{
					controlsFrame(width * factor);
					controlsFrame(width * factor);
					Require(toolbar, "Action bar child was not submitted");
					if (toolbar->DC.CursorMaxPos.x > toolbar->InnerRect.Max.x + 1.0f)
						throw std::runtime_error(
							std::format("Action bar overflow at width {} scale {}: {} > {}", width, factor,
										toolbar->DC.CursorMaxPos.x, toolbar->InnerRect.Max.x));
				}
			}
			ImGui::GetStyle() = ImGuiStyle{};
			controlsFrame(80.0f);
			controlsFrame(80.0f);
			const ImVec2 more((toolbar->InnerRect.Min.x + toolbar->InnerRect.Max.x) * 0.5f,
							  (toolbar->InnerRect.Min.y + toolbar->InnerRect.Max.y) * 0.5f);
			io.AddMousePosEvent(more.x, more.y);
			controlsFrame(80.0f);
			io.AddMouseButtonEvent(0, true);
			controlsFrame(80.0f);
			io.AddMouseButtonEvent(0, false);
			controlsFrame(80.0f);
			controlsFrame(80.0f);
			Require(!GImGui->OpenPopupStack.empty(), "Narrow action bar did not expose its overflow menu");
			ImGuiWindow* popup = GImGui->OpenPopupStack.back().Window;
			Require(popup, "Overflow menu did not render");
			io.AddMousePosEvent(popup->WorkRect.Min.x + 10.0f,
								popup->WorkRect.Min.y + ImGui::GetTextLineHeight() * 0.5f);
			controlsFrame(80.0f);
			io.AddMouseButtonEvent(0, true);
			controlsFrame(80.0f);
			io.AddMouseButtonEvent(0, false);
			Require(controlsFrame(80.0f) == 11, "Overflow menu did not return its hidden primary action");

			TextEditorPanel panel;
			panel.SetText("// Beam phase one\n");
			Document* document = panel.ActiveDocument();
			document->Dirty = true;
			panel.SaveAs(directory / "saved.cs");
			Require(!panel.IsDirty() && Read(directory / "saved.cs") == "// Beam phase one\n",
					"Beam did not save the active document");
			document->Editor.ReplaceSectionText(0, 0, 0, 0, "// Quick edit\n");
			Require(panel.IsDirty(), "An immediate edit was not dirty before the next UI frame");
			panel.Save();
			Require(!panel.IsDirty() && Read(directory / "saved.cs") == document->Editor.GetText(),
					"Immediate save did not preserve the latest edit");
			const size_t savedUndoIndex = document->Editor.GetUndoIndex();
			document->Editor.Undo();
			document->Editor.ReplaceSectionText(0, 0, 0, 0, "// Replacement edit\n");
			Require(document->Editor.GetUndoIndex() == savedUndoIndex && panel.IsDirty(),
					"Replacing a redo branch at the saved undo index lost its dirty state");
			panel.Save();
			document->Editor.Undo();
			Require(panel.IsDirty(), "Undo after saving was not reported as a modification");
			panel.RequestCloseDocument(panel.m_ActiveDocument);
			Require(panel.m_OpenCloseConfirm, "Quick close discarded a pending edit");
			panel.m_OpenCloseConfirm = false;
			panel.m_PendingCloseIndex = -1;
			document->Dirty = true;
			const std::string text = document->Editor.GetText();
			panel.OnClose();
			Require(panel.ActiveDocument() == document && panel.IsDirty() &&
						document->Editor.GetText() == text,
					"Hiding Beam changed its dirty document");
			panel.SaveAs(directory / "missing" / "failed.cs");
			Require(panel.IsDirty() && !document->SaveError.empty(),
					"Failed save did not retain work and feedback");
			panel.SaveAs(directory / "recovered.cs");
			Require(!panel.IsDirty() && document->SaveError.empty() &&
						Read(directory / "recovered.cs") == text,
					"Successful retry did not clear the save failure");
			panel.SetDiffText("before", "after");
			panel.m_DiffMode = false;
			Require(panel.ActiveDocument() == document, "Diff mode replaced the document");
			const auto originalPath = document->Path;
			panel.SaveAs({});
			Require(document->Path == originalPath && !panel.IsDirty(),
					"Cancelled Save As changed document state");
			panel.SaveAs(directory / "missing-again" / "failure.py");
			Require(document->Path == originalPath && panel.IsDirty(),
					"Failed Save As renamed the original document");
			panel.RequestCloseDocument(panel.m_ActiveDocument);
			Require(panel.ResolveClose(false) && panel.ActiveDocument() == document && panel.IsDirty(),
					"Cancelling close lost the dirty document");
			panel.RequestCloseDocument(panel.m_ActiveDocument);
			Require(panel.ResolveClose(true) && panel.m_Documents.empty(),
					"Save-and-close did not save the original destination");
			Require(Read(originalPath) == text, "Save after failed Save As wrote to the wrong destination");

			Require(FileSystem::CreateDirectory(directory / "left") &&
						FileSystem::CreateDirectory(directory / "right"),
					"Cannot create duplicate-name fixtures");
			Require(
				FileSystem::WriteFileAtomically(directory / "left" / "Shared.cs", "// left\n", true) &&
					FileSystem::WriteFileAtomically(directory / "right" / "Shared.cs", "// right\n", true),
				"Cannot write duplicate-name fixtures");
			panel.OpenFile(directory / "left" / "Shared.cs");
			Document* leftDocument = panel.ActiveDocument();
			panel.OpenFile(directory / "right" / "Shared.cs");
			Require(panel.m_Documents.size() == 2 &&
						leftDocument->DisplayName != panel.ActiveDocument()->DisplayName,
					"Duplicate filenames cannot be distinguished");
			panel.SaveAs(leftDocument->Path);
			Require(!panel.ActiveDocument()->SaveError.empty() && Read(leftDocument->Path) == "// left\n",
					"Save As overwrote another open document");
			panel.OpenFile(directory / "left" / ".." / "left" / "Shared.cs");
			Require(panel.m_Documents.size() == 2 && panel.ActiveDocument() == leftDocument,
					"A path alias opened a second writer");
			panel.OpenFile(directory / "not-present.cs");
			Require(panel.m_Documents.size() == 2 && panel.ActiveDocument() == leftDocument &&
						!panel.m_OpenError.empty(),
					"An unreadable file created an empty document");
			const auto unicodePath = directory / std::filesystem::path(u8"caf\u00e9-\u65e5\u672c.cs");
			Require(FileSystem::WriteFileAtomically(unicodePath, "// UTF-8 path\n", true),
					"Cannot create Unicode fixture");
			panel.OpenFile(unicodePath);
			Require(panel.ActiveDocument()->DisplayPath.find("caf\xc3\xa9") != std::string::npos &&
						panel.ActiveDocument()->Editor.GetText() == "// UTF-8 path\n",
					"Unicode filename/content failed to open");
			Document* unicodeDocument = panel.ActiveDocument();
			unicodeDocument->Editor.ReplaceSectionText(0, 0, 0, 0, "// unsaved\n");
			panel.RequestCloseDocument(panel.m_ActiveDocument);
			panel.CloseDocument(0);
			Require(panel.m_Documents[panel.m_PendingCloseIndex].get() == unicodeDocument,
					"Closing an earlier tab redirected a pending close");
			Require(panel.ResolveClose(false, true) && Read(unicodePath) == "// UTF-8 path\n",
					"Discard wrote unsaved changes");

#ifdef LUX_PLATFORM_WINDOWS
			const auto readOnlyPath = directory / std::filesystem::path(u8"read-only-\u65e5\u672c.cs");
			Require(FileSystem::WriteFileAtomically(readOnlyPath, "// protected\n", true),
					"Cannot create read-only fixture");
			std::filesystem::permissions(readOnlyPath, std::filesystem::perms::owner_read,
										 std::filesystem::perm_options::replace);
			TextEditorPanel readOnlyPanel;
			readOnlyPanel.OpenFile(readOnlyPath);
			readOnlyPanel.ActiveDocument()->Editor.ReplaceSectionText(0, 0, 0, 0, "// edit\n");
			readOnlyPanel.Save();
			Require(readOnlyPanel.IsDirty() && !readOnlyPanel.ActiveDocument()->SaveError.empty() &&
						Read(readOnlyPath) == "// protected\n",
					"A read-only destination lost text or overwrote its protected contents");
			std::filesystem::permissions(readOnlyPath, std::filesystem::perms::owner_all,
										 std::filesystem::perm_options::replace);
			readOnlyPanel.Save();
			Require(!readOnlyPanel.IsDirty() &&
						Read(readOnlyPath) == readOnlyPanel.ActiveDocument()->Editor.GetText(),
					"Retry after removing read-only did not preserve the edit");
#endif

			TextEditorPanel closeFailure;
			Require(FileSystem::CreateDirectory(directory / "close-original"),
					"Cannot create close-save fixture");
			closeFailure.SetText("// original\n");
			closeFailure.SaveAs(directory / "close-original" / "file.cs");
			closeFailure.ActiveDocument()->Editor.ReplaceSectionText(0, 0, 0, 0, "// retained edit\n");
			std::filesystem::rename(directory / "close-original", directory / "close-moved");
			closeFailure.RequestCloseDocument(closeFailure.m_ActiveDocument);
			Require(!closeFailure.ResolveClose(true) && closeFailure.IsDirty() &&
						closeFailure.m_PendingCloseIndex == 0,
					"A failed close-save lost the document or dismissed its pending decision");
			Require(Read(directory / "close-moved" / "file.cs") == "// original\n",
					"Failed close-save changed the old file");
			Require(closeFailure.ResolveClose(false) && closeFailure.IsDirty(),
					"Cancel after failed close-save lost text");
			for (int i = 0; i < 24; ++i)
				panel.SetText("// many-tab navigation\n");

			// Exercise the real vendor find UI and Beam's shell controls with the isolated atlas.
			TextEditor editor;
			editor.SetText("first match\nsecond match\n" + std::string(500, 'x'));
			editor.OpenFindReplaceWindow();
			auto shellFrame = [&](float width)
			{
				ImGui::NewFrame();
				ImGui::SetNextWindowPos({0.0f, 0.0f});
				ImGui::SetNextWindowSize({width, 600.0f});
				ImGui::Begin("Beam shell regression", nullptr, ImGuiWindowFlags_NoTitleBar);
				panel.UI_Tabs();
				panel.UI_CloseConfirm();
				panel.UI_DocumentsMenu();
				if (Document* active = panel.ActiveDocument())
				{
					panel.UI_GoToLinePopup(*active);
					active->Editor.Render("active_beam_document", {0.0f, 90.0f});
				}
				editor.Render("vendor_editor", {0.0f, 350.0f});
				for (ImGuiWindow* window : GImGui->Windows)
				{
					if (window->Name &&
						std::string_view(window->Name).find("find-replace") != std::string_view::npos &&
						window->Active)
					{
						Require(window->Pos.x >= window->ParentWindow->Pos.x - 1.0f &&
									window->Pos.x + window->Size.x <=
										window->ParentWindow->Pos.x + window->ParentWindow->Size.x + 1.0f,
								"Find/Replace extends outside its editor");
						if (width >= 900.0f)
							if (window->ScrollMax.y > 1.0f)
								throw std::runtime_error(std::format("Wide Find/Replace scroll {} at width "
																	 "{}, height {}, content {}, inner {}",
																	 window->ScrollMax.y, width,
																	 window->Size.y, window->ContentSize.y,
																	 window->InnerRect.GetHeight()));
					}
				}
				if (Document* active = panel.ActiveDocument())
					panel.UI_StatusBar(*active);
				ImGui::End();
				ImGui::Render();
			};
			for (float factor : {1.0f, 1.5f})
			{
				ImGui::GetStyle() = ImGuiStyle{};
				ImGui::GetStyle().ScaleAllSizes(factor);
				ImGui::GetStyle().FontScaleMain = factor;
				for (float width : {900.0f, 280.0f, 180.0f, 900.0f})
				{
					shellFrame(width * factor);
					shellFrame(width * factor);
				}
			}

			Require(panel.m_TabOrder.size() == panel.m_Documents.size(), "Visual tab order lost a document");
			panel.SelectDocument(0);
			Document* expectedNext = panel.m_TabOrder[1];
			panel.CycleDocument(1);
			Require(panel.ActiveDocument() == expectedNext,
					"Keyboard cycling does not follow visual tab order");
			panel.ActiveDocument()->Editor.ReplaceSectionText(0, 0, 0, 0, "// pending close\n");
			panel.RequestCloseDocument(panel.m_ActiveDocument);
			shellFrame(900.0f);
			shellFrame(900.0f);
			Require(!GImGui->OpenPopupStack.empty(), "Editor focus hid the unsaved-changes modal");
			io.AddKeyEvent(ImGuiKey_Escape, true);
			shellFrame(900.0f);
			io.AddKeyEvent(ImGuiKey_Escape, false);
			shellFrame(900.0f);
			panel.ActiveDocument()->Dirty = false;
			panel.ActiveDocument()->ObservedChangeVersion = panel.ActiveDocument()->Editor.GetChangeVersion();
			panel.m_OpenGoToLine = true;
			shellFrame(900.0f);
			shellFrame(900.0f);
			Require(!GImGui->OpenPopupStack.empty(), "Go to Line popup was not submitted");
			const std::string beforeNavigation = panel.ActiveDocument()->Editor.GetText();
			io.AddKeyEvent(ImGuiKey_Enter, true);
			shellFrame(900.0f);
			io.AddKeyEvent(ImGuiKey_Enter, false);
			shellFrame(900.0f);
			Require(panel.ActiveDocument()->Editor.GetText() == beforeNavigation && !panel.IsDirty(),
					"Submitting Go to Line also inserted an editor newline");
			panel.m_OpenDocuments = true;
			shellFrame(900.0f);
			shellFrame(900.0f);
			Require(!GImGui->OpenPopupStack.empty(), "Open Documents popup was not submitted");
		}
		catch (const std::exception& error)
		{
			failures.push_back(error.what());
		}
		ImGui::DestroyContext(context);
		ImGui::SetCurrentContext(previous);
		return failures.empty();
	}
} // namespace Lux
#endif
