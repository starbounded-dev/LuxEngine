#include "lpch.h"
#include "TextEditorPanel.h"

#include <imgui/imgui.h>
#include "ImGui/ImGuiEx.h"
#include <fstream>
#include <sstream>

namespace Lux
{
	TextEditorPanel::TextEditorPanel()
	{
		m_Editor.SetReadOnlyEnabled(false);
		m_Editor.SetShowLineNumbersEnabled(true);
		m_Editor.SetShowMatchingBrackets(true);
		m_Editor.SetShowScrollbarMiniMapEnabled(true);
		m_Editor.SetLanguage(TextEditor::Language::Cpp());
		m_Editor.SetChangeCallback([this]()
			{
				m_Dirty = true;
			}, 150);

		m_DiffEditor.SetReadOnlyEnabled(true);
		m_DiffEditor.SetSideBySideMode(true);
		m_DiffEditor.SetLanguage(TextEditor::Language::Cpp());
	}

	std::string TextEditorPanel::ReadFileString(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::in | std::ios::binary);
		if (!stream)
			return {};

		std::stringstream ss;
		ss << stream.rdbuf();
		return ss.str();
	}

	const TextEditor::Language* TextEditorPanel::GetLanguageFromPath(const std::filesystem::path& path)
	{
		std::string ext = path.extension().string();

		if (ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".cxx" || ext == ".cc")
			return TextEditor::Language::Cpp();
		if (ext == ".c")
			return TextEditor::Language::C();
		if (ext == ".cs")
			return TextEditor::Language::Cs();
		if (ext == ".lua")
			return TextEditor::Language::Lua();
		if (ext == ".py")
			return TextEditor::Language::Python();
		if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".comp")
			return TextEditor::Language::Glsl();
		if (ext == ".hlsl")
			return TextEditor::Language::Hlsl();
		if (ext == ".json")
			return TextEditor::Language::Json();
		if (ext == ".md")
			return TextEditor::Language::Markdown();
		if (ext == ".sql")
			return TextEditor::Language::Sql();

		return TextEditor::Language::Cpp();
	}

	void TextEditorPanel::SetText(const std::string& text)
	{
		m_DiffMode = false;
		m_Editor.SetText(text);
		m_Dirty = false;
	}

	void TextEditorPanel::SetDiffText(const std::string& left, const std::string& right)
	{
		m_DiffMode = true;
		m_DiffEditor.SetText(left, right);
		m_DiffEditor.SetSideBySideMode(m_DiffSideBySide);
	}

	void TextEditorPanel::OpenFile(const std::filesystem::path& path)
	{
		m_CurrentPath = path;
		m_DiffMode = false;
		m_Editor.SetLanguage(GetLanguageFromPath(path));
		m_Editor.SetText(ReadFileString(path));
		m_Dirty = false;
	}

	void TextEditorPanel::Save()
	{
		if (m_CurrentPath.empty() || m_DiffMode)
			return;

		std::ofstream stream(m_CurrentPath, std::ios::out | std::ios::binary | std::ios::trunc);
		if (!stream)
			return;

		std::string text = m_Editor.GetText();
		stream.write(text.data(), static_cast<std::streamsize>(text.size()));
		stream.flush();
		m_Dirty = false;
	}

	void TextEditorPanel::SaveAs(const std::filesystem::path& path)
	{
		m_CurrentPath = path;
		Save();
	}

	void TextEditorPanel::OpenDiff(const std::filesystem::path& leftPath, const std::filesystem::path& rightPath)
	{
		m_LeftDiffPath = leftPath;
		m_RightDiffPath = rightPath;

		std::string left = ReadFileString(leftPath);
		std::string right = ReadFileString(rightPath);

		m_DiffEditor.SetLanguage(GetLanguageFromPath(rightPath.empty() ? leftPath : rightPath));
		m_DiffEditor.SetText(left, right);
		m_DiffEditor.SetSideBySideMode(m_DiffSideBySide);
		m_DiffMode = true;
	}

	void TextEditorPanel::OnImGuiRender(bool& isOpen)
	{
		if (!ImGui::Begin("Text Editor", &isOpen))
		{
			ImGui::End();
			return;
		}

		if (ImGui::Button("Editor"))
			m_DiffMode = false;

		ImGui::SameLine();

		if (ImGui::Button("Diff"))
			m_DiffMode = true;

		ImGui::SameLine();

		if (!m_DiffMode)
		{
			if (ImGui::Button("Save"))
				Save();

			ImGui::SameLine();
			if (ImGui::Button("Undo"))
				m_Editor.Undo();

			ImGui::SameLine();
			if (ImGui::Button("Redo"))
				m_Editor.Redo();

			ImGui::SameLine();
			if (ImGui::Button("Copy"))
				m_Editor.Copy();

			ImGui::SameLine();
			if (ImGui::Button("Paste"))
				m_Editor.Paste();
		}
		else
		{
			if (ImGuiEx::BeginPropertyGrid())
			{
				if (ImGuiEx::Property("Side By Side", m_DiffSideBySide))
				{
					m_DiffEditor.SetSideBySideMode(m_DiffSideBySide);
				}
				ImGuiEx::EndPropertyGrid();
			}
		}

		ImGui::Separator();

		if (!m_DiffMode)
		{
			std::string title = m_CurrentPath.empty() ? "Untitled" : m_CurrentPath.string();
			if (m_Dirty)
				title += " *";

			ImGui::TextUnformatted(title.c_str());

			int line = 0, column = 0;
			m_Editor.GetCurrentCursor(line, column);
			ImGui::Text("Line %d, Column %d | %s",
				line + 1,
				column + 1,
				m_Editor.GetLanguageName().c_str());

			m_Editor.Render("##LuxTextEditor", ImVec2(-1.0f, -1.0f), true);
		}
		else
		{
			std::string leftName = m_LeftDiffPath.empty() ? "Left" : m_LeftDiffPath.filename().string();
			std::string rightName = m_RightDiffPath.empty() ? "Right" : m_RightDiffPath.filename().string();
			std::string header = leftName + "  <->  " + rightName;

			ImGui::TextUnformatted(header.c_str());
			m_DiffEditor.Render("##LuxTextDiff", ImVec2(-1.0f, -1.0f), true);
		}

		ImGui::End();
	}
}
