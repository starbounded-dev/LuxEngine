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
	// its own editor kept at a stable heap address so its change callback can reference it safely.
	class TextEditorPanel : public EditorPanel
	{
	public:
		TextEditorPanel();
		virtual ~TextEditorPanel() = default;

		virtual void OnImGuiRender(bool& isOpen) override;

		void OpenFile(const std::filesystem::path& path);
		void Save();
		void SaveAs(const std::filesystem::path& path);

		void OpenDiff(const std::filesystem::path& leftPath, const std::filesystem::path& rightPath);
		void SetText(const std::string& text);
		void SetDiffText(const std::string& left, const std::string& right);

		const std::filesystem::path& GetCurrentPath() const;
		bool IsDirty() const;

	private:
		struct Document
		{
			TextEditor Editor;
			std::filesystem::path Path;   // empty => untitled
			bool Dirty = false;
		};

		static std::string ReadFileString(const std::filesystem::path& path);
		static const TextEditor::Language* GetLanguageFromPath(const std::filesystem::path& path);
		static std::string DocumentTitle(const Document& doc);

		void ConfigureEditor(Document& doc);
		Document& NewDocument();
		Document* ActiveDocument();
		int FindDocument(const std::filesystem::path& path) const;
		void SaveDocument(Document& doc);
		void CloseDocument(int index);
		void RequestCloseDocument(int index);   // prompts before discarding a dirty tab
		void HandleShortcuts();

		void UI_Toolbar();
		void UI_Tabs();
		void UI_CloseConfirm();
		void UI_StatusBar(Document& doc);
		void UI_GoToLinePopup(Document& doc);

		std::vector<Scope<Document>> m_Documents;
		int m_ActiveDocument = -1;
		int m_PendingCloseIndex = -1;       // tab awaiting a Save/Discard/Cancel decision
		bool m_OpenCloseConfirm = false;    // request to open the confirmation popup next frame
		bool m_OpenGoToLine = false;
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
