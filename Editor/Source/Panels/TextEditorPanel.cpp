#include "lpch.h"
#include "TextEditorPanel.h"

#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiFonts.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <fstream>
#include <sstream>

namespace Lux
{
	// -- Beam: the Lux code editor -------------------------------------------------------------
	// A tabbed, keyboard-driven shell over the vendored TextEditor. Keybinds (when focused):
	//   Ctrl+N new   Ctrl+S save   Ctrl+W close tab   Ctrl+F find/replace   Ctrl+G go to line
	//   Ctrl+PageDown/PageUp next/previous tab
	// Editing keys (undo/redo, copy/paste, multi-cursor, comment) are handled by the editor itself.

	TextEditorPanel::TextEditorPanel()
	{
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

	std::string TextEditorPanel::DocumentTitle(const Document& doc)
	{
		std::string name = doc.Path.empty() ? "untitled" : doc.Path.filename().string();
		if (doc.Dirty)
			name += " *";
		return name;
	}

	void TextEditorPanel::ConfigureEditor(Document& doc)
	{
		doc.Editor.SetReadOnlyEnabled(false);
		doc.Editor.SetShowLineNumbersEnabled(true);
		doc.Editor.SetShowMatchingBrackets(true);
		doc.Editor.SetShowScrollbarMiniMapEnabled(true);
		doc.Editor.SetAutoIndentEnabled(true);
		doc.Editor.SetLanguage(TextEditor::Language::Cpp());

		// The Document lives at a stable heap address (Scope in a vector of Scopes), so capturing
		// its pointer is safe across reallocations of the vector.
		Document* d = &doc;
		doc.Editor.SetChangeCallback([d]() { d->Dirty = true; }, 150);
	}

	TextEditorPanel::Document& TextEditorPanel::NewDocument()
	{
		m_Documents.push_back(CreateScope<Document>());
		Document& doc = *m_Documents.back();
		ConfigureEditor(doc);
		m_ActiveDocument = (int)m_Documents.size() - 1;
		m_DiffMode = false;
		return doc;
	}

	TextEditorPanel::Document* TextEditorPanel::ActiveDocument()
	{
		if (m_ActiveDocument < 0 || m_ActiveDocument >= (int)m_Documents.size())
			return nullptr;
		return m_Documents[m_ActiveDocument].get();
	}

	int TextEditorPanel::FindDocument(const std::filesystem::path& path) const
	{
		for (int i = 0; i < (int)m_Documents.size(); i++)
		{
			if (!m_Documents[i]->Path.empty() && m_Documents[i]->Path == path)
				return i;
		}
		return -1;
	}

	void TextEditorPanel::OpenFile(const std::filesystem::path& path)
	{
		const int existing = FindDocument(path);
		if (existing >= 0)
		{
			m_ActiveDocument = existing;
			m_DiffMode = false;
			return;
		}

		Document& doc = NewDocument();
		doc.Path = path;
		doc.Editor.SetLanguage(GetLanguageFromPath(path));
		doc.Editor.SetText(ReadFileString(path));
		doc.Dirty = false;
	}

	void TextEditorPanel::SaveDocument(Document& doc)
	{
		if (doc.Path.empty())
			return; // untitled: needs a Save As path (wired via the content browser today)

		std::ofstream stream(doc.Path, std::ios::out | std::ios::binary | std::ios::trunc);
		if (!stream)
			return;

		const std::string text = doc.Editor.GetText();
		stream.write(text.data(), static_cast<std::streamsize>(text.size()));
		stream.flush();
		doc.Dirty = false;
	}

	void TextEditorPanel::Save()
	{
		if (Document* doc = ActiveDocument())
			SaveDocument(*doc);
	}

	void TextEditorPanel::SaveAs(const std::filesystem::path& path)
	{
		if (Document* doc = ActiveDocument())
		{
			doc->Path = path;
			doc->Editor.SetLanguage(GetLanguageFromPath(path));
			SaveDocument(*doc);
		}
	}

	void TextEditorPanel::CloseDocument(int index)
	{
		if (index < 0 || index >= (int)m_Documents.size())
			return;

		m_Documents.erase(m_Documents.begin() + index);

		if (m_Documents.empty())
			m_ActiveDocument = -1;
		else
			m_ActiveDocument = std::min(m_ActiveDocument, (int)m_Documents.size() - 1);
	}

	void TextEditorPanel::SetText(const std::string& text)
	{
		Document& doc = NewDocument();
		doc.Editor.SetText(text);
		doc.Dirty = false;
	}

	const std::filesystem::path& TextEditorPanel::GetCurrentPath() const
	{
		if (m_ActiveDocument >= 0 && m_ActiveDocument < (int)m_Documents.size())
			return m_Documents[m_ActiveDocument]->Path;
		return m_EmptyPath;
	}

	bool TextEditorPanel::IsDirty() const
	{
		if (m_ActiveDocument >= 0 && m_ActiveDocument < (int)m_Documents.size())
			return m_Documents[m_ActiveDocument]->Dirty;
		return false;
	}

	// -- Diff ----------------------------------------------------------------------------------

	void TextEditorPanel::SetDiffText(const std::string& left, const std::string& right)
	{
		m_DiffMode = true;
		m_DiffEditor.SetText(left, right);
		m_DiffEditor.SetSideBySideMode(m_DiffSideBySide);
	}

	void TextEditorPanel::OpenDiff(const std::filesystem::path& leftPath, const std::filesystem::path& rightPath)
	{
		m_LeftDiffPath = leftPath;
		m_RightDiffPath = rightPath;

		const std::string left = ReadFileString(leftPath);
		const std::string right = ReadFileString(rightPath);

		m_DiffEditor.SetLanguage(GetLanguageFromPath(rightPath.empty() ? leftPath : rightPath));
		m_DiffEditor.SetText(left, right);
		m_DiffEditor.SetSideBySideMode(m_DiffSideBySide);
		m_DiffMode = true;
	}

	// -- Shortcuts -----------------------------------------------------------------------------

	void TextEditorPanel::HandleShortcuts()
	{
		if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			return;

		const ImGuiIO& io = ImGui::GetIO();
		if (!io.KeyCtrl)
			return;

		if (ImGui::IsKeyPressed(ImGuiKey_N, false))
			NewDocument();

		Document* doc = ActiveDocument();

		if (ImGui::IsKeyPressed(ImGuiKey_S, false))
			Save();
		if (ImGui::IsKeyPressed(ImGuiKey_W, false) && m_ActiveDocument >= 0)
			CloseDocument(m_ActiveDocument);
		if (doc && ImGui::IsKeyPressed(ImGuiKey_F, false))
			doc->Editor.OpenFindReplaceWindow();
		if (doc && ImGui::IsKeyPressed(ImGuiKey_G, false))
			m_OpenGoToLine = true;

		// Ctrl+PageDown / PageUp cycle tabs (Ctrl+Tab is reserved by ImGui's window nav).
		if (!m_Documents.empty())
		{
			if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false))
			{
				m_ActiveDocument = (m_ActiveDocument + 1) % (int)m_Documents.size();
				m_SelectActiveTab = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_PageUp, false))
			{
				m_ActiveDocument = (m_ActiveDocument + (int)m_Documents.size() - 1) % (int)m_Documents.size();
				m_SelectActiveTab = true;
			}
		}
	}

	// -- UI ------------------------------------------------------------------------------------

	namespace
	{
		bool ToolbarIconButton(const char* id, const char* icon, const char* tooltip, bool active = false)
		{
			if (active)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colors::Theme::accent));
			}
			ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 16));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 26));
			ImGui::PushID(id);
			const bool pressed = ImGui::Button(icon, ImVec2(26.0f, 24.0f));
			ImGui::PopID();
			ImGui::PopStyleColor(active ? 4 : 3);
			if (tooltip && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", tooltip);
			return pressed;
		}
	}

	void TextEditorPanel::UI_Toolbar()
	{
		// Wordmark.
		ImGui::AlignTextToFramePadding();
		if (ImFont* display = ImGuiEx::Fonts::Get("Display"))
			ImGui::PushFont(display);
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colors::Theme::accent), "BEAM");
		if (ImGuiEx::Fonts::Get("Display"))
			ImGui::PopFont();

		ImGui::SameLine(0.0f, 12.0f);

		Document* doc = ActiveDocument();

		if (ToolbarIconButton("new", LUX_ICON_FILE_O, "New file  (Ctrl+N)"))
			NewDocument();
		ImGui::SameLine(0.0f, 2.0f);
		if (ToolbarIconButton("save", LUX_ICON_FLOPPY_O, "Save  (Ctrl+S)") && doc)
			Save();
		ImGui::SameLine(0.0f, 2.0f);
		if (ToolbarIconButton("find", LUX_ICON_SEARCH, "Find / Replace  (Ctrl+F)") && doc)
			doc->Editor.OpenFindReplaceWindow();

		// Editor / Diff toggle on the right.
		const float toggleWidth = 120.0f;
		ImGui::SameLine(ImGui::GetContentRegionMax().x - toggleWidth);
		if (ToolbarIconButton("mode_editor", LUX_ICON_CODE, "Editor", !m_DiffMode))
			m_DiffMode = false;
		ImGui::SameLine(0.0f, 2.0f);
		if (ToolbarIconButton("mode_diff", LUX_ICON_COLUMNS, "Diff", m_DiffMode))
			m_DiffMode = true;
	}

	void TextEditorPanel::UI_Tabs()
	{
		const ImGuiTabBarFlags flags = ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll
			| ImGuiTabBarFlags_AutoSelectNewTabs;

		if (!ImGui::BeginTabBar("##beam_tabs", flags))
			return;

		if (ImGui::TabItemButton(LUX_ICON_PLUS "##beam_new_tab", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
			NewDocument();

		int closeRequest = -1;
		for (int i = 0; i < (int)m_Documents.size(); i++)
		{
			Document& d = *m_Documents[i];

			// Stable, unique tab id from the document's address (after ###), so the visible title
			// can change (dirty marker, rename) without resetting the tab.
			const std::string label = DocumentTitle(d) + std::format("###beam_tab_{}", static_cast<const void*>(&d));

			ImGuiTabItemFlags itemFlags = ImGuiTabItemFlags_None;
			if (m_SelectActiveTab && i == m_ActiveDocument)
				itemFlags |= ImGuiTabItemFlags_SetSelected;

			bool open = true;
			if (ImGui::BeginTabItem(label.c_str(), &open, itemFlags))
			{
				m_ActiveDocument = i;
				ImGui::EndTabItem();
			}

			if (!open)
				closeRequest = i;
		}

		m_SelectActiveTab = false;

		ImGui::EndTabBar();

		if (closeRequest >= 0)
			CloseDocument(closeRequest);
	}

	void TextEditorPanel::UI_StatusBar(Document& doc)
	{
		int line = 0, column = 0;
		doc.Editor.GetCurrentCursor(line, column);

		if (ImFont* mono = ImGuiEx::Fonts::Get("Mono"))
			ImGui::PushFont(mono);

		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker));
		ImGui::Text("Ln %d, Col %d   %s   %d lines%s",
			line + 1,
			column + 1,
			doc.Editor.GetLanguageName().c_str(),
			doc.Editor.GetLineCount(),
			doc.Dirty ? "   \xE2\x97\x8f modified" : "");
		ImGui::PopStyleColor();

		if (ImGuiEx::Fonts::Get("Mono"))
			ImGui::PopFont();
	}

	void TextEditorPanel::UI_GoToLinePopup(Document& doc)
	{
		if (m_OpenGoToLine)
		{
			ImGui::OpenPopup("Go to line##beam");
			m_OpenGoToLine = false;
		}

		if (ImGui::BeginPopup("Go to line##beam"))
		{
			static int s_TargetLine = 1;
			ImGui::TextUnformatted("Go to line");
			ImGui::SetNextItemWidth(120.0f);
			const bool enter = ImGui::InputInt("##beam_goto", &s_TargetLine, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
			if (enter || ImGui::Button("Go"))
			{
				const int target = std::clamp(s_TargetLine - 1, 0, std::max(0, doc.Editor.GetLineCount() - 1));
				doc.Editor.SetCursor(target, 0);
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void TextEditorPanel::OnImGuiRender(bool& isOpen)
	{
		if (!ImGui::Begin("Beam", &isOpen))
		{
			ImGui::End();
			return;
		}

		HandleShortcuts();
		UI_Toolbar();
		ImGui::Separator();

		if (m_DiffMode)
		{
			const std::string leftName = m_LeftDiffPath.empty() ? "Left" : m_LeftDiffPath.filename().string();
			const std::string rightName = m_RightDiffPath.empty() ? "Right" : m_RightDiffPath.filename().string();

			if (ImGuiEx::Property("Side By Side", m_DiffSideBySide))
				m_DiffEditor.SetSideBySideMode(m_DiffSideBySide);

			ImGui::Text("%s  <->  %s", leftName.c_str(), rightName.c_str());
			m_DiffEditor.Render("##BeamDiff", ImVec2(-1.0f, -1.0f), true);
			ImGui::End();
			return;
		}

		UI_Tabs();

		Document* doc = ActiveDocument();
		if (!doc)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("No file open.  Press Ctrl+N for a new file, or open one from the Content Browser.");
			ImGui::End();
			return;
		}

		UI_StatusBar(*doc);
		UI_GoToLinePopup(*doc);

		const std::string editorId = std::format("##beam_editor_{}", static_cast<const void*>(doc));
		doc->Editor.Render(editorId.c_str(), ImVec2(-1.0f, -1.0f), true);

		ImGui::End();
	}
}
