// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Editor/EditorPanel.h"

#include "Lux/Vendor/TextEditor.h"
#include "Lux/Vendor/TextDiff.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Lux
{
	// "Beam" — the Lux code editor. A tabbed, keyboard-driven shell over the vendored TextEditor
	// (syntax highlighting, multi-cursor, find/replace, minimap). Each open file is a Document with
	// its own editor kept at a stable heap address across document-tab changes.
	class TextEditorPanel : public EditorPanel
	{
	public:
		TextEditorPanel();
		virtual ~TextEditorPanel() = default;

		virtual void OnImGuiRender(bool& isOpen) override;
		void OnEvent(Event& event) override;
		void OnClose() override { m_KeyboardFocus = false; }

		void OpenFile(const std::filesystem::path& path);
		void Save();
		void SaveAs(const std::filesystem::path& path);

		void OpenDiff(const std::filesystem::path& leftPath, const std::filesystem::path& rightPath);
		void SetText(const std::string& text);
		void SetDiffText(const std::string& left, const std::string& right);

		const std::filesystem::path& GetCurrentPath() const;
		bool IsDirty() const;
#ifndef LUX_DIST
		static bool RunPanelControlsSelfTests(const std::filesystem::path& directory, std::vector<std::string>& failures);
		static bool RunWorkspaceSelfTests(const std::filesystem::path& directory,
										  std::vector<std::string>& failures);
#endif

	private:
		struct Document
		{
			TextEditor Editor;
			std::filesystem::path Path;   // empty => untitled
			bool Dirty = false;
			std::string SaveError;
			size_t ObservedChangeVersion = 0;
			uint32_t UntitledNumber = 0;
			std::string DisplayName;
			std::string DisplayPath;
			std::string TabLabel;
			ImGuiID TabID = 0;
		};

		static bool ReadFileString(const std::filesystem::path& path, std::string& text);
		static const TextEditor::Language* GetLanguageFromPath(const std::filesystem::path& path);
		void UpdateDocumentLabels();
		void SelectDocument(int index);
		void CycleDocument(int direction);

		void ConfigureEditor(Document& doc);
		static void RefreshDirty(Document& doc);
		Document& NewDocument();
		Document* ActiveDocument();
		int FindDocument(const std::filesystem::path& path) const;
		bool SaveDocument(Document& doc);
		bool SaveDocumentAs(Document& doc, const std::filesystem::path& path);
		bool ResolveClose(bool save, bool discard = false);
		void CloseDocument(int index);
		void RequestCloseDocument(int index);   // prompts before discarding a dirty tab
		void HandleShortcuts();
		void RunAction(int action);

		void UI_Toolbar();
		void UI_Tabs();
		void UI_DocumentsMenu();
		void UI_CloseConfirm();
		void UI_StatusBar(Document& doc);
		void UI_GoToLinePopup(Document& doc);

		std::vector<Scope<Document>> m_Documents;
		std::vector<Document*> m_TabOrder; // Non-owning visual order, cleared before any document is destroyed.
		int m_ActiveDocument = -1;
		int m_PendingCloseIndex = -1;       // tab awaiting a Save/Discard/Cancel decision
		bool m_OpenCloseConfirm = false;    // request to open the confirmation popup next frame
		bool m_OpenGoToLine = false;
		int m_TargetLine = 1;
		bool m_OpenDocuments = false;
		ImGuiTextFilter m_DocumentFilter;
		int m_TextZoom = 100;
		bool m_ShowLineNumbers = true;
		bool m_ShowMinimap = true;
		std::string m_OpenError;
		bool m_KeyboardFocus = false;
		bool m_RequestFocus = false;
		uint32_t m_NextUntitledNumber = 1;
		bool m_SelectActiveTab = false;   // sync the tab bar after a shortcut-driven switch

		// Diff view (read-only, side-by-side) — kept from the original panel.
		TextDiff m_DiffEditor;
		std::filesystem::path m_LeftDiffPath;
		std::filesystem::path m_RightDiffPath;
		bool m_DiffMode = false;
		bool m_DiffSideBySide = true;

		std::filesystem::path m_EmptyPath;   // stable empty return for GetCurrentPath()
	};
}
