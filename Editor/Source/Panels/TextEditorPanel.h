#pragma once

#include "Lux/Editor/EditorPanel.h"

#include "Lux/Vendor/TextEditor.h"
#include "Lux/Vendor/TextDiff.h"

#include <filesystem>
#include <string>

namespace Lux
{
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

		const std::filesystem::path& GetCurrentPath() const { return m_CurrentPath; }
		bool IsDirty() const { return m_Dirty; }

	private:
		static std::string ReadFileString(const std::filesystem::path& path);
		static const TextEditor::Language* GetLanguageFromPath(const std::filesystem::path& path);

	private:
		TextEditor m_Editor;
		TextDiff m_DiffEditor;

		std::filesystem::path m_CurrentPath;
		std::filesystem::path m_LeftDiffPath;
		std::filesystem::path m_RightDiffPath;

		bool m_Dirty = false;
		bool m_DiffMode = false;
		bool m_DiffSideBySide = true;
	};
}
