// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "Lux/Utilities/FileSystem.h"
#include "TextEditorPanel.h"

#include "Lux/Editor/FontAwesome.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/ImGuiFonts.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Project/Project.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <fstream>
#include <sstream>
#include <chrono>
#include <cstdio>

namespace Lux
{
	namespace
	{
		constexpr int k_MinTextZoom = 50;
		constexpr int k_MaxTextZoom = 250;
		constexpr int k_TextZoomStep = 10;

		enum class BeamAction
		{
			New, Save, Find, Editor, Diff, SaveAs, GoToLine, Close, Open, Documents, ZoomIn, ZoomOut,
			ZoomReset, LineNumbers, Minimap, Build, Problems, FindInFiles
		};

		constexpr double k_BackupInterval = 3.0;      // seconds between a dirty buffer's backups
		constexpr double k_SessionInterval = 2.0;
		constexpr double k_DiskCheckInterval = 1.0;

		// Wall time, usable with no ImGui context (the panel's destructor saves the session at shutdown).
		double Now()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}
	}

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

	TextEditorPanel::~TextEditorPanel()
	{
		// A quit with unsaved tabs comes back as it was: a last copy of every unsaved buffer, and the open tabs.
		m_Build.Cancel();
		for (auto& document : m_Documents)
			RefreshDirty(*document);
		UpdateBackups(true);
		SaveSession();
	}

	bool TextEditorPanel::ReadFileString(const std::filesystem::path& path, std::string& text)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
			return false;
		std::stringstream contents;
		contents << stream.rdbuf();
		if (stream.bad())
			return false;
		text = contents.str();
		return true;
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

	void TextEditorPanel::UpdateDocumentLabels()
	{
		// Only filenames/lifetimes change these labels; no path conversion or label allocation per frame.
		for (auto& doc : m_Documents)
		{
			const auto filename = doc->Path.filename().u8string();
			doc->DisplayName = doc->Path.empty() ? std::format("Untitled {}", doc->UntitledNumber)
				: std::string(reinterpret_cast<const char*>(filename.data()), filename.size());
			const auto path = doc->Path.generic_u8string();
			doc->DisplayPath.assign(reinterpret_cast<const char*>(path.data()), path.size());
		}
		for (auto& doc : m_Documents)
		{
			const bool duplicate = !doc->Path.empty() && std::any_of(m_Documents.begin(), m_Documents.end(),
				[&](const auto& other) { return other.get() != doc.get() && other->Path.filename() == doc->Path.filename(); });
			if (duplicate)
			{
				auto parentSuffix = [](std::filesystem::path parent, size_t depth)
				{
					std::filesystem::path suffix;
					for (size_t i = 0; i < depth && !parent.empty(); ++i)
					{
						suffix = suffix.empty() ? parent.filename() : parent.filename() / suffix;
						const auto next = parent.parent_path();
						if (next == parent)
							break;
						parent = next;
					}
					return suffix;
				};
				size_t depth = 1;
				auto suffix = parentSuffix(doc->Path.parent_path(), depth);
				while (std::any_of(m_Documents.begin(), m_Documents.end(), [&](const auto& other)
				{
					return other.get() != doc.get() && other->Path.filename() == doc->Path.filename() &&
						parentSuffix(other->Path.parent_path(), depth) == suffix;
				}))
				{
					const auto longer = parentSuffix(doc->Path.parent_path(), ++depth);
					if (longer == suffix)
						break;
					suffix = longer;
				}
				const auto parent = suffix.generic_u8string();
				doc->DisplayName = std::string(reinterpret_cast<const char*>(parent.data()), parent.size()) + " / " + doc->DisplayName;
			}
			doc->TabLabel = doc->DisplayName + std::format("###beam_tab_{}", static_cast<const void*>(doc.get()));
		}
	}

	void TextEditorPanel::SelectDocument(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_Documents.size()))
			return;
		m_ActiveDocument = index;
		m_SelectActiveTab = true;
		m_DiffMode = false;
		m_SessionDirty = true;
		m_Documents[index]->Editor.SetFocus();
	}

	void TextEditorPanel::CycleDocument(int direction)
	{
		if (m_Documents.empty())
			return;
		const int count = static_cast<int>(m_Documents.size());
		int next = (m_ActiveDocument + direction + count) % count;
		if (m_TabOrder.size() == m_Documents.size())
		{
			const auto active = std::find(m_TabOrder.begin(), m_TabOrder.end(), ActiveDocument());
			if (active != m_TabOrder.end())
			{
				Document* target = m_TabOrder[(std::distance(m_TabOrder.begin(), active) + direction + count) % count];
				for (int i = 0; i < count; ++i)
					if (m_Documents[i].get() == target)
						next = i;
			}
		}
		SelectDocument(next);
	}

	void TextEditorPanel::ConfigureEditor(Document& doc)
	{
		doc.Editor.SetReadOnlyEnabled(false);
		doc.Editor.SetShowLineNumbersEnabled(m_ShowLineNumbers);
		doc.Editor.SetShowMatchingBrackets(true);
		doc.Editor.SetShowScrollbarMiniMapEnabled(m_ShowMinimap);
		doc.Editor.SetAutoIndentEnabled(true);
		doc.Editor.SetLanguage(TextEditor::Language::Cpp());
		auto palette = doc.Editor.GetPalette();
		palette[static_cast<size_t>(TextEditor::Color::lineNumber)] = Colors::Theme::textCaption;
		palette[static_cast<size_t>(TextEditor::Color::currentLineNumber)] = Colors::Theme::textBrighter;
		doc.Editor.SetPalette(palette);
	}

	void TextEditorPanel::RefreshDirty(Document& doc)
	{
		// The editor's summary callback is delayed until a later Render, even with zero
		// delay. Inspect the committed change version before Save/Close instead, so a quick
		// edit cannot be lost or a queued callback mark freshly saved text dirty again.
		// Dirty means "different from the file", so undoing back to the saved text clears it. The text is only compared
		// when it changed, never once per frame.
		const size_t version = doc.Editor.GetChangeVersion();
		if (version != doc.ObservedChangeVersion)
		{
			doc.ObservedChangeVersion = version;
			doc.Dirty = HashText(doc.Editor.GetText()) != doc.SavedHash;
		}
	}

	TextEditorPanel::Document& TextEditorPanel::NewDocument()
	{
		m_Documents.push_back(CreateScope<Document>());
		Document& doc = *m_Documents.back();
		doc.UntitledNumber = m_NextUntitledNumber++;
		ConfigureEditor(doc);
		doc.SavedHash = HashText(doc.Editor.GetText());
		doc.ObservedChangeVersion = doc.Editor.GetChangeVersion();
		doc.Editor.SetFocus();
		m_SelectActiveTab = true;
		m_SessionDirty = true;
		m_ActiveDocument = (int)m_Documents.size() - 1;
		m_DiffMode = false;
		UpdateDocumentLabels();
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
		std::error_code error;
		const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
		for (int i = 0; i < static_cast<int>(m_Documents.size()); ++i)
		{
			const auto& existing = m_Documents[i]->Path;
			if (existing.empty())
				continue;
			if ((!error && existing == absolute) || existing == path)
				return i;
			std::error_code equivalentError;
			if (std::filesystem::equivalent(existing, path, equivalentError) && !equivalentError)
				return i;
		}
		return -1;
	}

	void TextEditorPanel::OpenFile(const std::filesystem::path& path)
	{
		m_RequestFocus = true;
		const int existing = FindDocument(path);
		if (existing >= 0)
		{
			SelectDocument(existing);
			m_OpenError.clear();
			return;
		}
		std::string text;
		if (!ReadFileString(path, text))
		{
			const auto utf8 = path.u8string();
			m_OpenError = "The file could not be read. Your open documents are unchanged.\n" +
				std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
			LUX_CORE_ERROR_TAG("Editor", "Beam: {}", m_OpenError);
			return;
		}
		std::error_code error;
		const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
		Document& doc = NewDocument();
		doc.Path = error ? path : absolute;
		doc.Editor.SetLanguage(GetLanguageFromPath(path));
		doc.Editor.SetText(text);
		MarkSaved(doc);
		ApplyDiagnosticMarkers(doc);
		m_OpenError.clear();
		UpdateDocumentLabels();
	}

	void TextEditorPanel::OpenFileAt(const std::filesystem::path& path, int line, int column)
	{
		OpenFile(path);
		const int index = FindDocument(path);
		if (index < 0)
			return;
		SelectDocument(index);
		Document& doc = *m_Documents[index];
		const int target = std::clamp(line - 1, 0, std::max(0, doc.Editor.GetLineCount() - 1));
		doc.Editor.SetCursor(target, std::max(column - 1, 0));
		doc.Editor.ScrollToLine(target, TextEditor::Scroll::alignMiddle);
		m_RequestFocus = true;
	}

	size_t TextEditorPanel::HashText(const std::string& text)
	{
		return std::hash<std::string>{}(text);
	}

	// The buffer and the file are the same text now: a load, a save or a reload.
	void TextEditorPanel::MarkSaved(Document& doc)
	{
		doc.SavedHash = HashText(doc.Editor.GetText());
		doc.ObservedChangeVersion = doc.Editor.GetChangeVersion();
		doc.Dirty = false;
		doc.DiskChanged = false;
		doc.DiskDeleted = false;
		doc.DiskTime = doc.Path.empty() ? 0 : FileSystem::GetLastWriteTime(doc.Path);
		if (!doc.BackupName.empty())
		{
			if (const auto directory = SessionDirectory(); !directory.empty())
				BeamSession::DeleteBackup(directory, doc.BackupName);
			doc.BackupName.clear();
		}
		doc.BackedUpVersion = ~size_t(0);
		m_SessionDirty = true;
	}

	void TextEditorPanel::ReloadDocument(Document& doc)
	{
		std::string text;
		if (doc.Path.empty() || !ReadFileString(doc.Path, text))
			return;
		int line = 0, column = 0;
		doc.Editor.GetCurrentCursor(line, column);
		doc.Editor.SetText(text);
		doc.Editor.SetCursor(std::min(line, std::max(0, doc.Editor.GetLineCount() - 1)), column);
		MarkSaved(doc);
		ApplyDiagnosticMarkers(doc);
	}

	// Files change under Beam: a build tool, a git checkout, another editor. An unedited buffer follows the file; an edited one asks.
	void TextEditorPanel::CheckExternalChanges()
	{
		for (auto& entry : m_Documents)
		{
			Document& doc = *entry;
			if (doc.Path.empty())
				continue;
			std::error_code error;
			if (!std::filesystem::exists(doc.Path, error))
			{
				doc.DiskDeleted = true;
				continue;
			}
			doc.DiskDeleted = false;
			const uint64_t time = FileSystem::GetLastWriteTime(doc.Path);
			if (time == doc.DiskTime)
				continue;
			RefreshDirty(doc);
			if (!doc.Dirty)
			{
				ReloadDocument(doc);
				LUX_CORE_INFO_TAG("Editor", "Beam: {} changed on disk and was reloaded.", doc.DisplayName);
			}
			else
			{
				doc.DiskChanged = true;
			}
		}
	}

	// A dirty buffer's copy, written a few seconds after its last change, outside the project, so a crash loses seconds, not work.
	void TextEditorPanel::UpdateBackups(bool force)
	{
		const std::filesystem::path directory = SessionDirectory();
		if (directory.empty())
			return;
		const double now = Now();
		for (auto& entry : m_Documents)
		{
			Document& doc = *entry;
			if (!doc.Dirty)
				continue;
			const size_t version = doc.Editor.GetChangeVersion();
			if (version == doc.BackedUpVersion || (!force && now - doc.LastBackupTime < k_BackupInterval))
				continue;
			if (doc.BackupName.empty())
				doc.BackupName = BeamSession::NewBackupName();
			if (BeamSession::WriteBackup(directory, doc.BackupName, doc.Editor.GetText()))
			{
				doc.BackedUpVersion = version;
				doc.LastBackupTime = now;
				m_SessionDirty = true;
			}
			else
			{
				LUX_CORE_ERROR_TAG("Editor", "Beam: could not write a backup of {} to {}.", doc.DisplayName, directory.string());
			}
		}
	}

	std::filesystem::path TextEditorPanel::SessionDirectory() const
	{
		return Project::GetActive() ? BeamSession::DirectoryFor(Project::GetActive()->GetProjectDirectory()) : std::filesystem::path();
	}

	void TextEditorPanel::SaveSession()
	{
		const std::filesystem::path directory = SessionDirectory();
		// Until the last session is restored, saving would overwrite it with an empty one.
		if (directory.empty() || !m_SessionRestored)
			return;

		BeamSessionData data;
		for (int i = 0; i < static_cast<int>(m_Documents.size()); ++i)
		{
			const Document& doc = *m_Documents[i];
			if (doc.Path.empty() && !doc.Dirty)
				continue; // an empty untitled tab is not worth restoring
			BeamSessionDocument saved;
			saved.Path = doc.Path;
			saved.BackupFile = doc.Dirty ? doc.BackupName : std::string();
			saved.Untitled = doc.UntitledNumber;
			doc.Editor.GetCurrentCursor(saved.Line, saved.Column);
			if (i == m_ActiveDocument)
				data.Active = static_cast<int>(data.Documents.size());
			data.Documents.push_back(std::move(saved));
		}
		if (!BeamSession::Save(directory, data))
			LUX_CORE_ERROR_TAG("Editor", "Beam: could not save the session to {}.", directory.string());
		BeamSession::PruneBackups(directory, data);
		m_SessionDirty = false;
		m_LastSessionSave = Now();
	}

	// The tabs of the last session, each unsaved buffer as it was at its last backup.
	void TextEditorPanel::RestoreSession()
	{
		if (!Project::GetActive())
			return;
		m_SessionRestored = true;
		if (!m_Documents.empty())
			return; // something opened Beam before its first frame

		const std::filesystem::path directory = SessionDirectory();
		BeamSessionData data;
		if (directory.empty() || !BeamSession::Load(directory, data))
			return;

		int active = -1;
		for (int i = 0; i < static_cast<int>(data.Documents.size()); ++i)
		{
			const BeamSessionDocument& saved = data.Documents[i];
			std::string backup;
			const bool hasBackup = !saved.BackupFile.empty() && BeamSession::ReadBackup(directory, saved.BackupFile, backup);
			if (!saved.Path.empty() && !hasBackup)
			{
				std::error_code error;
				if (!std::filesystem::exists(saved.Path, error))
					continue; // deleted since: nothing to bring back
				OpenFile(saved.Path);
			}
			else if (hasBackup)
			{
				Document& doc = NewDocument();
				doc.Path = saved.Path;
				if (saved.Untitled > 0)
					doc.UntitledNumber = saved.Untitled;
				m_NextUntitledNumber = std::max(m_NextUntitledNumber, doc.UntitledNumber + 1);
				// The file's text decides what "dirty" means: the restored buffer is dirty exactly when it differs from it.
				std::string disk;
				if (!doc.Path.empty() && ReadFileString(doc.Path, disk))
				{
					doc.Editor.SetText(disk);
					doc.SavedHash = HashText(doc.Editor.GetText());
					doc.DiskTime = FileSystem::GetLastWriteTime(doc.Path);
				}
				if (!doc.Path.empty())
					doc.Editor.SetLanguage(GetLanguageFromPath(doc.Path));
				doc.Editor.SetText(backup);
				doc.ObservedChangeVersion = doc.Editor.GetChangeVersion() - 1; // force the comparison on the next frame
				doc.BackupName = saved.BackupFile;
				UpdateDocumentLabels();
			}
			else
			{
				continue;
			}
			Document& doc = *m_Documents.back();
			doc.Editor.SetCursor(std::min(saved.Line, std::max(0, doc.Editor.GetLineCount() - 1)), saved.Column);
			if (i == data.Active)
				active = static_cast<int>(m_Documents.size()) - 1;
		}
		if (active >= 0)
		{
			m_ActiveDocument = active;
			m_SelectActiveTab = true;
		}
		m_RequestFocus = false; // restoring tabs must not take the keyboard from whatever the user was doing
		for (auto& doc : m_Documents)
			RefreshDirty(*doc);
		if (!m_Documents.empty())
			LUX_CORE_INFO_TAG("Editor", "Beam: restored {} open file{}.", m_Documents.size(), m_Documents.size() == 1 ? "" : "s");
	}

	// -- Building C# ---------------------------------------------------------------------------

	void TextEditorPanel::StartBuild()
	{
		if (m_Build.IsRunning())
			return;

		const std::filesystem::path project = Project::GetActive() ? Project::GetActiveScriptProjectPath() : std::filesystem::path();
		std::error_code error;
		if (project.empty() || !std::filesystem::exists(project, error))
		{
			m_Problems.clear();
			m_BuildStatus = "This project has no C# script project to build.";
			m_BuildFailed = true;
			return;
		}

		// The compiler reads the files on disk, so unsaved scripts are saved first.
		for (auto& entry : m_Documents)
		{
			Document& doc = *entry;
			RefreshDirty(doc);
			if (doc.Dirty && !doc.Path.empty() && doc.Path.extension() == ".cs" && !SaveDocument(doc))
			{
				m_BuildStatus = "Build not started: " + doc.DisplayName + " could not be saved.";
				m_BuildFailed = true;
				return;
			}
		}

		m_Problems.clear();
		for (auto& entry : m_Documents)
			entry->Editor.ClearMarkers();
		m_BuildFailed = false;
		m_BuildStatus = "Building...";
		m_Build.Start(project, "Debug");
	}

	void TextEditorPanel::HandleBuildResult(ScriptBuildResult& result)
	{
		m_Problems = std::move(result.Diagnostics);
		std::stable_sort(m_Problems.begin(), m_Problems.end(), [](const BuildDiagnostic& a, const BuildDiagnostic& b)
		{
			return a.Severity != b.Severity ? a.Severity > b.Severity : false; // errors first, then the compiler's own order
		});

		size_t errors = 0, warnings = 0;
		for (const BuildDiagnostic& diagnostic : m_Problems)
		{
			errors += diagnostic.Severity == DiagnosticSeverity::Error;
			warnings += diagnostic.Severity == DiagnosticSeverity::Warning;
		}

		if (!result.Error.empty())
			m_BuildStatus = result.Error;
		else if (result.Cancelled)
			m_BuildStatus = "Build cancelled.";
		else if (result.Success)
			m_BuildStatus = warnings ? std::format("Build succeeded, {} warning{}.", warnings, warnings == 1 ? "" : "s") : "Build succeeded.";
		else if (errors)
			m_BuildStatus = std::format("Build failed: {} error{}{}.", errors, errors == 1 ? "" : "s", warnings ? std::format(", {} warning{}", warnings, warnings == 1 ? "" : "s") : std::string());
		else
			m_BuildStatus = "Build failed. The console has the compiler's output.";
		m_BuildFailed = !result.Success;

		// A failure with nothing to point at (a missing SDK, a restore error): put what the compiler said where it can be read.
		if (!result.Success && !result.Cancelled && errors == 0)
		{
			const size_t first = result.Output.size() > 20 ? result.Output.size() - 20 : 0;
			for (size_t i = first; i < result.Output.size(); ++i)
				LUX_CORE_ERROR_TAG("ScriptBuilder", "{}", result.Output[i]);
		}

		for (auto& entry : m_Documents)
			ApplyDiagnosticMarkers(*entry);
		if (result.Success && m_OnBuildSucceeded)
			m_OnBuildSucceeded();
	}

	// A mark on the line number and a tint on the line, with the message as its tooltip.
	void TextEditorPanel::ApplyDiagnosticMarkers(Document& doc)
	{
		doc.Editor.ClearMarkers();
		if (doc.Path.empty())
			return;
		for (const BuildDiagnostic& diagnostic : m_Problems)
		{
			std::error_code error;
			if (!std::filesystem::equivalent(doc.Path, diagnostic.Location.File, error) || error)
				continue;
			const bool isError = diagnostic.Severity == DiagnosticSeverity::Error;
			const std::string tip = (diagnostic.Code.empty() ? std::string() : diagnostic.Code + ": ") + diagnostic.Message;
			doc.Editor.AddMarker(diagnostic.Location.Line - 1, isError ? IM_COL32(220, 60, 60, 255) : IM_COL32(220, 170, 40, 255),
				isError ? IM_COL32(220, 60, 60, 48) : IM_COL32(220, 170, 40, 36), tip, tip);
		}
	}

	float TextEditorPanel::ProblemsHeight() const
	{
		if (!m_ShowSearch && (!m_ShowProblems || (m_Problems.empty() && m_BuildStatus.empty())))
			return 0.0f;
		return std::min(ImGui::GetFontSize() * 11.0f, ImGui::GetContentRegionAvail().y * 0.4f);
	}

	void TextEditorPanel::UI_Problems(float height)
	{
		if (height <= 0.0f)
			return;
		if (ImGui::BeginChild("beam_problems", {0.0f, height}, ImGuiChildFlags_Borders))
		{
			const bool building = m_Build.IsRunning();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(building ? Colors::Theme::textCaption : (m_BuildFailed ? Colors::Theme::titlebarRed : Colors::Theme::titlebarGreen)),
				"%s %s", building ? LUX_ICON_COGS : (m_BuildFailed ? LUX_ICON_TIMES_CIRCLE : LUX_ICON_CHECK), m_BuildStatus.c_str());
			ImGui::SameLine();
			if (building)
			{
				if (ImGui::SmallButton("Cancel##beam_build_cancel"))
					m_Build.Cancel();
			}
			else if (ImGui::SmallButton("Build again##beam_build_again"))
			{
				StartBuild();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Hide##beam_problems_hide"))
				m_ShowProblems = false;

			int jump = -1;
			for (int i = 0; i < static_cast<int>(m_Problems.size()); ++i)
			{
				const BuildDiagnostic& diagnostic = m_Problems[i];
				const auto file = diagnostic.Location.File.filename().u8string();
				const std::string text = std::format("{} {}({},{})  {}{}{}", diagnostic.Severity == DiagnosticSeverity::Error ? LUX_ICON_TIMES_CIRCLE : LUX_ICON_EXCLAMATION_TRIANGLE,
					std::string(reinterpret_cast<const char*>(file.data()), file.size()), diagnostic.Location.Line, diagnostic.Location.Column,
					diagnostic.Code, diagnostic.Code.empty() ? "" : "  ", diagnostic.Message);
				ImGuiEx::ScopedID id(i);
				if (ImGui::Selectable(text.c_str()))
					jump = i;
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s\n%s", diagnostic.Location.File.string().c_str(), diagnostic.Message.c_str());
			}
			if (m_Problems.empty() && !building && !m_BuildFailed)
				ImGui::TextUnformatted("No problems.");
			if (jump >= 0)
			{
				const SourceLocation location = m_Problems[jump].Location;
				OpenFileAt(location.File, location.Line, location.Column);
			}
		}
		ImGui::EndChild();
	}

	void TextEditorPanel::OpenSearch()
	{
		m_ShowSearch = true;
		m_SearchFocus = true;
		m_RequestFocus = true;
	}

	void TextEditorPanel::StartSearch()
	{
		m_SearchResults.clear();
		m_SearchFiles = 0;
		m_SearchTruncated = false;
		if (!Project::GetActive())
			return;
		BeamSearchOptions options;
		options.Query = m_SearchQuery;
		options.CaseSensitive = m_SearchCase;
		options.Root = Project::GetActiveAssetDirectory();
		m_Search.Start(options);
	}

	void TextEditorPanel::UI_BottomPanel(float height)
	{
		if (m_ShowSearch)
			UI_Search(height);
		else
			UI_Problems(height);
	}

	void TextEditorPanel::UI_Search(float height)
	{
		if (height <= 0.0f)
			return;
		if (ImGui::BeginChild("beam_search", {0.0f, height}, ImGuiChildFlags_Borders))
		{
			if (m_SearchFocus)
			{
				ImGui::SetKeyboardFocusHere();
				m_SearchFocus = false;
			}
			ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 6.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 11.0f));
			bool search = ImGui::InputTextWithHint("##beam_search_query", "Find in files (Enter)", m_SearchQuery, sizeof(m_SearchQuery), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			search |= ImGui::Checkbox("Aa##beam_search_case", &m_SearchCase);
			ImGui::SetItemTooltip("Match case");
			ImGui::SameLine();
			if (ImGui::SmallButton("Close##beam_search_close"))
				m_ShowSearch = false;
			if (search)
				StartSearch();

			const bool running = m_Search.IsRunning();
			if (running)
				ImGui::TextUnformatted("Searching...");
			else if (m_SearchQuery[0])
				ImGui::Text("%zu match%s in %zu file%s searched%s", m_SearchResults.size(), m_SearchResults.size() == 1 ? "" : "es", m_SearchFiles, m_SearchFiles == 1 ? "" : "s",
					m_SearchTruncated ? " (the first matches only)" : "");
			else
				ImGui::TextUnformatted("Searches the project's assets: scripts, shaders, scenes and other text files.");

			if (ImGui::BeginChild("beam_search_results", {0.0f, 0.0f}))
			{
				const std::filesystem::path root = Project::GetActive() ? Project::GetActiveAssetDirectory() : std::filesystem::path();
				int jump = -1;
				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(m_SearchResults.size()));
				while (clipper.Step())
				{
					for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
					{
						const BeamSearchMatch& match = m_SearchResults[i];
						std::error_code error;
						std::filesystem::path shown = std::filesystem::relative(match.File, root, error);
						if (error || shown.empty())
							shown = match.File.filename();
						const auto shownText = shown.generic_u8string();
						const std::string label = std::format("{}:{}  {}", std::string(reinterpret_cast<const char*>(shownText.data()), shownText.size()), match.Line, match.Text);
						ImGuiEx::ScopedID id(i);
						if (ImGui::Selectable(label.c_str()))
							jump = i;
					}
				}
				if (jump >= 0)
				{
					const BeamSearchMatch match = m_SearchResults[jump];
					OpenFileAt(match.File, match.Line, match.Column);
				}
			}
			ImGui::EndChild();
		}
		ImGui::EndChild();
	}

	// "Changed on disk" and "deleted" bars above the editor.
	void TextEditorPanel::UI_FileBars(Document& doc)
	{
		if (doc.DiskDeleted)
		{
			ImGuiEx::PanelMessage("This file is gone from the disk", "Your text is kept here. Save to write the file again, or close the tab.", true);
			if (ImGui::SmallButton("Save##beam_deleted_save"))
				SaveDocument(doc);
		}
		else if (doc.DiskChanged)
		{
			ImGuiEx::PanelMessage("This file changed on the disk", "You have unsaved changes here too. Keep yours, load the disk's version, or compare them first.", true);
			if (ImGui::SmallButton("Load the disk version##beam_disk_reload"))
				ReloadDocument(doc);
			ImGui::SameLine();
			if (ImGui::SmallButton("Keep my version##beam_disk_keep"))
			{
				doc.DiskTime = FileSystem::GetLastWriteTime(doc.Path);
				doc.DiskChanged = false;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Compare##beam_disk_compare"))
			{
				std::string disk;
				if (ReadFileString(doc.Path, disk))
					SetDiffText(disk, doc.Editor.GetText());
			}
		}
	}

	bool TextEditorPanel::SaveDocument(Document& doc)
	{
		if (!doc.Path.empty())
			return SaveDocumentAs(doc, doc.Path);
		const auto path = FileSystem::SaveFileDialog({{"All files", "*"}});
		return !path.empty() && SaveDocumentAs(doc, path);
	}

	bool TextEditorPanel::SaveDocumentAs(Document& doc, const std::filesystem::path& path)
	{
		if (path.empty())
			return false; // Cancelling a native dialog does not alter document state.
		RefreshDirty(doc);
		const int existing = FindDocument(path);
		if (existing >= 0 && m_Documents[existing].get() != &doc)
		{
			doc.SaveError = "This path is already open in another tab. Choose another name with Save As.";
			LUX_CORE_ERROR_TAG("Editor", "Beam: {}", doc.SaveError);
			return false;
		}
		std::error_code error;
		const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
		const auto destination = error ? path : absolute;
		if (!FileSystem::WriteFileAtomically(destination, doc.Editor.GetText(), true))
		{
			const auto utf8 = destination.u8string();
			doc.SaveError = "Could not write this destination. Your text and original file are retained. "
				"Retry Save, or choose another destination with Save As.\n" +
				std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
			doc.Dirty = true;
			return false;
		}
		// Commit a rename/language change only after the destination is safely written.
		const bool renamed = doc.Path != destination;
		doc.Path = destination;
		doc.Editor.SetLanguage(GetLanguageFromPath(destination));
		MarkSaved(doc);
		doc.SaveError.clear();
		if (renamed)
			UpdateDocumentLabels();
		return true;
	}

	void TextEditorPanel::Save()
	{
		if (Document* doc = ActiveDocument())
			SaveDocument(*doc);
	}

	void TextEditorPanel::SaveAs(const std::filesystem::path& path)
	{
		if (Document* doc = ActiveDocument())
			SaveDocumentAs(*doc, path);
	}

	void TextEditorPanel::CloseDocument(int index)
	{
		if (index < 0 || index >= (int)m_Documents.size())
			return;

		if (!m_Documents[index]->BackupName.empty())
		{
			if (const auto directory = SessionDirectory(); !directory.empty())
				BeamSession::DeleteBackup(directory, m_Documents[index]->BackupName);
		}
		m_SessionDirty = true;
		if (m_PendingCloseIndex == index)
			m_PendingCloseIndex = -1;
		else if (m_PendingCloseIndex > index)
			--m_PendingCloseIndex;
		m_TabOrder.clear();
		m_Documents.erase(m_Documents.begin() + index);
		UpdateDocumentLabels();
		if (m_ActiveDocument > index)
			--m_ActiveDocument;
		m_SelectActiveTab = true;

		if (m_Documents.empty())
			m_ActiveDocument = -1;
		else
			m_ActiveDocument = std::min(m_ActiveDocument, (int)m_Documents.size() - 1);
	}

	void TextEditorPanel::RequestCloseDocument(int index)
	{
		if (index < 0 || index >= (int)m_Documents.size())
			return;

		// A dirty tab routes through a Save / Discard / Cancel prompt so edits aren't lost silently.
		RefreshDirty(*m_Documents[index]);
		if (m_Documents[index]->Dirty)
		{
			SelectDocument(index);
			m_Documents[index]->Editor.CloseFindReplaceWindow(false);
			m_PendingCloseIndex = index;
			m_OpenCloseConfirm = true;
			return;
		}

		CloseDocument(index);
	}

	bool TextEditorPanel::ResolveClose(bool save, bool discard)
	{
		const int index = m_PendingCloseIndex;
		if (index < 0 || index >= static_cast<int>(m_Documents.size()))
			return true;
		if (save && !SaveDocument(*m_Documents[index]))
			return false;
		m_PendingCloseIndex = -1;
		m_OpenCloseConfirm = false;
		if (save || discard)
			CloseDocument(index);
		if (Document* doc = ActiveDocument())
			doc->Editor.SetFocus();
		return true;
	}

	void TextEditorPanel::UI_CloseConfirm()
	{
		if (m_OpenCloseConfirm)
		{
			ImGui::OpenPopup("Unsaved changes##beam_close");
			m_OpenCloseConfirm = false;
		}
		const ImGuiViewport* viewport = ImGui::GetWindowViewport();
		ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
		const float width = std::min(ImGui::GetFontSize() * 34.0f, viewport->WorkSize.x * 0.9f);
		ImGui::SetNextWindowSize({width, 0.0f}, ImGuiCond_Appearing);
		if (!ImGui::BeginPopupModal("Unsaved changes##beam_close", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;
		const bool valid = m_PendingCloseIndex >= 0 && m_PendingCloseIndex < static_cast<int>(m_Documents.size());
		if (!valid)
			ImGui::CloseCurrentPopup();
		else
		{
			Document& doc = *m_Documents[m_PendingCloseIndex];
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - ImGui::GetStyle().WindowPadding.x * 2.0f);
			ImGui::TextUnformatted("Save changes before closing?");
			ImGui::TextWrapped("%s", doc.DisplayPath.empty() ? doc.DisplayName.c_str() : doc.DisplayPath.c_str());
			if (!doc.SaveError.empty())
				ImGuiEx::PanelMessage("Could not save", doc.SaveError.c_str(), true);
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
			const bool save = ImGui::Button("Save");
			ImGui::SameLine();
			const bool saveAs = ImGui::Button("Save As...");
			ImGui::SameLine();
			const bool discard = ImGui::Button("Discard");
			ImGui::SameLine();
			const bool cancel = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);
			// Resolve only one decision, after drawing; a successful save destroys doc.
			bool close = false;
			if (cancel)
			{
				ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("beam_close_cancel"), ImGuiInputFlags_LockThisFrame);
				close = ResolveClose(false);
			}
			else if (save)
				close = ResolveClose(true);
			else if (saveAs)
			{
				const auto path = FileSystem::SaveFileDialog({{"All files", "*"}});
				if (!path.empty() && SaveDocumentAs(doc, path))
					close = ResolveClose(false, true);
			}
			else if (discard)
				close = ResolveClose(false, true);
			if (close)
				ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void TextEditorPanel::SetText(const std::string& text)
	{
		Document& doc = NewDocument();
		doc.Editor.SetText(text);
		doc.ObservedChangeVersion = doc.Editor.GetChangeVersion();
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
			return m_Documents[m_ActiveDocument]->Dirty ||
				m_Documents[m_ActiveDocument]->Editor.GetChangeVersion() != m_Documents[m_ActiveDocument]->ObservedChangeVersion;
		return false;
	}

	// -- Diff ----------------------------------------------------------------------------------

	void TextEditorPanel::SetDiffText(const std::string& left, const std::string& right)
	{
		m_LeftDiffPath.clear();
		m_RightDiffPath.clear();
		m_DiffMode = true;
		m_DiffEditor.SetText(left, right);
		m_DiffEditor.SetSideBySideMode(m_DiffSideBySide);
	}

	void TextEditorPanel::OpenDiff(const std::filesystem::path& leftPath, const std::filesystem::path& rightPath)
	{
		std::string left, right;
		if (!ReadFileString(leftPath, left) || !ReadFileString(rightPath, right))
		{
			m_OpenError = "Could not read both comparison files. The previous comparison is retained.";
			LUX_CORE_ERROR_TAG("Editor", "Beam: {}", m_OpenError);
			return;
		}
		m_LeftDiffPath = leftPath;
		m_RightDiffPath = rightPath;
		m_OpenError.clear();
		m_RequestFocus = true;

		m_DiffEditor.SetLanguage(GetLanguageFromPath(rightPath.empty() ? leftPath : rightPath));
		m_DiffEditor.SetText(left, right);
		m_DiffEditor.SetSideBySideMode(m_DiffSideBySide);
		m_DiffMode = true;
	}

	// -- Shortcuts -----------------------------------------------------------------------------

	void TextEditorPanel::OnEvent(Event& event)
	{
		// ImGui receives native input independently; only the scene shortcut path is consumed here.
		if (m_KeyboardFocus && event.IsInCategory(EventCategoryKeyboard))
			event.Handled = true;
	}

	void TextEditorPanel::HandleShortcuts()
	{
		if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			return;
		const ImGuiIO& io = ImGui::GetIO();
		if (!io.KeyCtrl || io.KeyAlt || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
			return;
		auto pressed = [](ImGuiKey key)
		{
			if (!ImGui::IsKeyPressed(key, false))
				return false;
			// In particular, Ctrl+G is Find Next in the vendored widget. A shell command
			// must not also execute that editing command later in this same frame.
			ImGui::SetKeyOwner(key, ImGui::GetID("beam_shortcuts"), ImGuiInputFlags_LockThisFrame);
			return true;
		};
		if (io.KeyShift && pressed(ImGuiKey_B))
			StartBuild();
		else if (io.KeyShift && pressed(ImGuiKey_F))
			OpenSearch();
		else if (pressed(ImGuiKey_N))
			NewDocument();
		else if (pressed(ImGuiKey_O))
			RunAction(static_cast<int>(BeamAction::Open));
		else if (pressed(ImGuiKey_P))
			m_OpenDocuments = true;
		else if (pressed(ImGuiKey_Equal) || pressed(ImGuiKey_KeypadAdd))
			RunAction(static_cast<int>(BeamAction::ZoomIn));
		else if (pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract))
			RunAction(static_cast<int>(BeamAction::ZoomOut));
		else if (pressed(ImGuiKey_0))
			RunAction(static_cast<int>(BeamAction::ZoomReset));
		else if (!m_Documents.empty() && (pressed(ImGuiKey_PageDown) || pressed(ImGuiKey_PageUp)))
		{
			CycleDocument(ImGui::IsKeyDown(ImGuiKey_PageDown) ? 1 : -1);
		}
		else if (!m_DiffMode && ActiveDocument())
		{
			if (pressed(ImGuiKey_S))
				RunAction(static_cast<int>(io.KeyShift ? BeamAction::SaveAs : BeamAction::Save));
			else if (pressed(ImGuiKey_W))
				RunAction(static_cast<int>(BeamAction::Close));
			else if (pressed(ImGuiKey_F))
				RunAction(static_cast<int>(BeamAction::Find));
			else if (pressed(ImGuiKey_G))
				RunAction(static_cast<int>(BeamAction::GoToLine));
		}
	}

	// -- UI ------------------------------------------------------------------------------------

	void TextEditorPanel::RunAction(int action)
	{
		if (action < 0)
			return;
		Document* doc = ActiveDocument();
		switch (static_cast<BeamAction>(action))
		{
		case BeamAction::New:
			NewDocument();
			break;
		case BeamAction::Save:
			Save();
			break;
		case BeamAction::Find:
			if (doc)
				doc->Editor.OpenFindReplaceWindow();
			break;
		case BeamAction::Editor:
			m_DiffMode = false;
			if (doc)
				doc->Editor.SetFocus();
			break;
		case BeamAction::Diff:
			m_DiffMode = true;
			break;
		case BeamAction::SaveAs:
		{
			const auto path = FileSystem::SaveFileDialog({{"All files", "*"}});
			if (!path.empty())
				SaveAs(path);
			break;
		}
		case BeamAction::GoToLine:
			m_OpenGoToLine = true;
			break;
		case BeamAction::Close:
			RequestCloseDocument(m_ActiveDocument);
			break;
		case BeamAction::Open:
		{
			const auto path = FileSystem::OpenFileDialog({{"Text files", "cs,cpp,h,hpp,c,glsl,json,lua,py,txt,md"}, {"All files", "*"}});
			if (!path.empty())
				OpenFile(path);
			break;
		}
		case BeamAction::Documents:
			m_OpenDocuments = true;
			break;
		case BeamAction::ZoomIn:
			m_TextZoom = std::min(k_MaxTextZoom, m_TextZoom + k_TextZoomStep);
			break;
		case BeamAction::ZoomOut:
			m_TextZoom = std::max(k_MinTextZoom, m_TextZoom - k_TextZoomStep);
			break;
		case BeamAction::ZoomReset:
			m_TextZoom = 100;
			break;
		case BeamAction::LineNumbers:
			m_ShowLineNumbers = !m_ShowLineNumbers;
			for (auto& document : m_Documents)
				document->Editor.SetShowLineNumbersEnabled(m_ShowLineNumbers);
			break;
		case BeamAction::Build:
			StartBuild();
			break;
		case BeamAction::Problems:
			m_ShowProblems = !m_ShowProblems;
			m_ShowSearch = false;
			break;
		case BeamAction::FindInFiles:
			OpenSearch();
			break;
		case BeamAction::Minimap:
			m_ShowMinimap = !m_ShowMinimap;
			for (auto& document : m_Documents)
				document->Editor.SetShowScrollbarMiniMapEnabled(m_ShowMinimap);
			break;
		}
	}

	void TextEditorPanel::UI_Toolbar()
	{
		const bool editing = ActiveDocument() && !m_DiffMode;
		using ImGuiEx::PanelAction;
		const PanelAction actions[] = {
			{static_cast<int>(BeamAction::New), "New", LUX_ICON_FILE_O, "Ctrl+N", true, false, true},
			{static_cast<int>(BeamAction::Save), "Save", LUX_ICON_FLOPPY_O, "Ctrl+S", editing, false, true},
			{static_cast<int>(BeamAction::Find), "Find", LUX_ICON_SEARCH, "Ctrl+F", editing},
			{static_cast<int>(BeamAction::Build), "Build C#", LUX_ICON_WRENCH, "Ctrl+Shift+B", !m_Build.IsRunning(), false, true},
			{static_cast<int>(BeamAction::Editor), "Editor", LUX_ICON_CODE, nullptr, true, !m_DiffMode},
			{static_cast<int>(BeamAction::Diff), "Diff", LUX_ICON_COLUMNS, nullptr, true, m_DiffMode},
			{static_cast<int>(BeamAction::SaveAs), "Save As...", nullptr, "Ctrl+Shift+S", editing, false, false, true},
			{static_cast<int>(BeamAction::GoToLine), "Go to Line...", nullptr, "Ctrl+G", editing, false, false, true},
			{static_cast<int>(BeamAction::Close), "Close File", nullptr, "Ctrl+W", editing, false, false, true},
			{static_cast<int>(BeamAction::Open), "Open File...", nullptr, "Ctrl+O", true, false, false, true},
			{static_cast<int>(BeamAction::Documents), "Open Documents...", nullptr, "Ctrl+P", !m_Documents.empty(), false, false, true},
			{static_cast<int>(BeamAction::ZoomIn), "Zoom In", nullptr, "Ctrl++", m_TextZoom < k_MaxTextZoom, false, false, true},
			{static_cast<int>(BeamAction::ZoomOut), "Zoom Out", nullptr, "Ctrl+-", m_TextZoom > k_MinTextZoom, false, false, true},
			{static_cast<int>(BeamAction::ZoomReset), "Reset Zoom", nullptr, "Ctrl+0", true, false, false, true},
			{static_cast<int>(BeamAction::LineNumbers), "Line Numbers", nullptr, nullptr, true, m_ShowLineNumbers, false, true},
			{static_cast<int>(BeamAction::Minimap), "Minimap", nullptr, nullptr, true, m_ShowMinimap, false, true},
			{static_cast<int>(BeamAction::FindInFiles), "Find in Files...", nullptr, "Ctrl+Shift+F", Project::GetActive() != nullptr, false, false, true},
			{static_cast<int>(BeamAction::Problems), "Problems", nullptr, nullptr, true, m_ShowProblems, false, true}
		};
		RunAction(ImGuiEx::PanelActionBar("beam_actions", actions));
	}

	void TextEditorPanel::UI_Tabs()
	{
		const ImGuiTabBarFlags flags = ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll
			| ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_TabListPopupButton;

		ImGuiEx::ScopedColourStack tabColours(
			ImGuiCol_Tab, Colors::Theme::backgroundDark,
			ImGuiCol_TabHovered, Colors::Theme::backgroundPopup,
			ImGuiCol_TabSelected, Colors::Theme::groupHeader,
			ImGuiCol_TabDimmed, Colors::Theme::backgroundDark,
			ImGuiCol_TabDimmedSelected, Colors::Theme::groupHeader);

		if (!ImGui::BeginTabBar("##beam_tabs", flags))
			return;

		int closeRequest = -1;
		for (int i = 0; i < (int)m_Documents.size(); i++)
		{
			Document& d = *m_Documents[i];

			ImGuiTabItemFlags itemFlags = d.Dirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
			if (m_SelectActiveTab && i == m_ActiveDocument)
				itemFlags |= ImGuiTabItemFlags_SetSelected;
			ImGui::SetNextItemWidth(std::min(ImGui::CalcTextSize(d.DisplayName.c_str()).x + ImGui::GetFontSize() * 3.0f,
				ImGui::GetFontSize() * 18.0f));

			d.TabID = ImGui::GetID(d.TabLabel.c_str());
			bool open = true;
			if (ImGui::BeginTabItem(d.TabLabel.c_str(), &open, itemFlags))
			{
				if (!m_SelectActiveTab && m_ActiveDocument != i)
				{
					m_ActiveDocument = i;
					d.Editor.SetFocus();
				}
				ImGui::EndTabItem();
			}

			if (ImGui::IsItemHovered() && !d.Path.empty())
				ImGui::SetTooltip("%s%s", d.DisplayPath.c_str(), d.Dirty ? "\nUnsaved changes" : "");
			ImGuiEx::ScopedID tabID(static_cast<const void*>(&d));
			if (ImGui::BeginPopupContextItem("##beam_tab_actions"))
			{
				if (ImGui::MenuItem("Copy file path", nullptr, false, !d.Path.empty()))
					ImGui::SetClipboardText(d.DisplayPath.c_str());
				if (ImGui::MenuItem("Close", "Ctrl+W"))
					open = false;
				ImGui::EndPopup();
			}
			if (!open)
				closeRequest = i;
		}

		m_SelectActiveTab = false;

		// A separate section keeps the button in one place as documents open and reorder.
		if (ImGui::TabItemButton(LUX_ICON_PLUS "##beam_new_tab", ImGuiTabItemFlags_Leading | ImGuiTabItemFlags_NoTooltip))
			NewDocument();
		ImGui::SetItemTooltip("New file (Ctrl+N)");

		const auto& tabs = ImGui::GetCurrentTabBar()->Tabs;
		bool reordered = m_TabOrder.size() != m_Documents.size();
		size_t position = 0;
		for (const ImGuiTabItem& tab : tabs)
		{
			if (tab.Flags & ImGuiTabItemFlags_Button)
				continue;
			reordered |= position >= m_TabOrder.size() || m_TabOrder[position]->TabID != tab.ID;
			++position;
		}
		if (reordered)
		{
			m_TabOrder.clear();
			m_TabOrder.reserve(m_Documents.size());
			for (const ImGuiTabItem& tab : tabs)
				for (auto& document : m_Documents)
					if (document->TabID == tab.ID)
						m_TabOrder.push_back(document.get());
		}
		ImGui::EndTabBar();

		if (closeRequest >= 0)
			RequestCloseDocument(closeRequest);
	}

	void TextEditorPanel::UI_DocumentsMenu()
	{
		if (m_OpenDocuments)
		{
			if (Document* doc = ActiveDocument())
				doc->Editor.CloseFindReplaceWindow(false);
			m_DocumentFilter.Clear();
			ImGui::OpenPopup("Open documents##beam");
			m_OpenDocuments = false;
		}
		const ImGuiViewport* viewport = ImGui::GetWindowViewport();
		ImGui::SetNextWindowSize({std::min(ImGui::GetFontSize() * 42.0f, viewport->WorkSize.x * 0.9f), 0.0f}, ImGuiCond_Appearing);
		if (!ImGui::BeginPopup("Open documents##beam"))
			return;
		ImGui::TextUnformatted("Open documents");
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		m_DocumentFilter.Draw("##beam_document_filter", -1.0f);
		ImGui::SetItemTooltip("Filter by filename or full path. Enter selects the first match.");
		const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
		int selected = -1;
		bool matched = false;
		if (ImGui::BeginChild("documents", {0.0f, ImGui::GetTextLineHeightWithSpacing() * 10.0f}))
		{
			for (int i = 0; i < static_cast<int>(m_Documents.size()); ++i)
			{
				const Document& doc = *m_Documents[i];
				if (!m_DocumentFilter.PassFilter(doc.DisplayName.c_str()) && !m_DocumentFilter.PassFilter(doc.DisplayPath.c_str()))
					continue;
				matched = true;
				ImGuiEx::ScopedID id(static_cast<const void*>(&doc));
				if (ImGui::Selectable(doc.DisplayName.c_str(), m_ActiveDocument == i) || (enter && selected < 0))
					selected = i;
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s%s", doc.DisplayPath.c_str(), doc.Dirty ? "\nUnsaved changes" : "");
			}
			if (!matched)
				ImGui::TextUnformatted("No matching open files.");
		}
		ImGui::EndChild();
		if (selected >= 0)
		{
			if (enter)
			{
				ImGui::SetKeyOwner(ImGuiKey_Enter, ImGui::GetID("beam_document_select"), ImGuiInputFlags_LockThisFrame);
				ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, ImGui::GetID("beam_document_select"), ImGuiInputFlags_LockThisFrame);
			}
			SelectDocument(selected);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void TextEditorPanel::UI_StatusBar(Document& doc)
	{
		int line = 0, column = 0;
		doc.Editor.GetCurrentCursor(line, column);
		ImGuiEx::ScopedColour color(ImGuiCol_Text, doc.Dirty ? Colors::Theme::accent : Colors::Theme::textCaption);
		char position[64];
		std::snprintf(position, sizeof(position), "Ln %d, Col %d", line + 1, column + 1);
		char details[128];
		std::snprintf(details, sizeof(details), "%s  |  %s  |  %d lines  |  %d%%", doc.Dirty ? "Modified" : (doc.Path.empty() ? "Untitled" : "Saved"),
			doc.Editor.GetLanguageName().c_str(), doc.Editor.GetLineCount(), m_TextZoom);
		const float available = ImGui::GetContentRegionAvail().x;
		const float height = ImGui::GetTextLineHeight();
		ImGui::BeginChild("beam_status", {0.0f, height}, ImGuiChildFlags_None,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		char full[256];
		std::snprintf(full, sizeof(full), "%s  |  %s", position, details);
		if (ImGui::CalcTextSize(full).x <= available)
			ImGui::TextUnformatted(full);
		else
			ImGui::Text("%s%s", doc.Dirty ? "* " : "", position);
		ImGui::EndChild();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s\n%s\n%s", position, details, doc.DisplayPath.empty() ? "Untitled file" : doc.DisplayPath.c_str());
	}

	void TextEditorPanel::UI_GoToLinePopup(Document& doc)
	{
		if (m_OpenGoToLine)
		{
			doc.Editor.CloseFindReplaceWindow(false);
			int column = 0;
			doc.Editor.GetCurrentCursor(m_TargetLine, column);
			++m_TargetLine;
			ImGui::OpenPopup("Go to line##beam");
			m_OpenGoToLine = false;
		}
		if (ImGui::BeginPopup("Go to line##beam"))
		{
			ImGui::Text("Go to line (1 - %d)", doc.Editor.GetLineCount());
			if (ImGui::IsWindowAppearing())
				ImGui::SetKeyboardFocusHere();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
			ImGui::InputInt("##beam_goto", &m_TargetLine, 0, 0, ImGuiInputTextFlags_AutoSelectAll);
			const bool enter = (ImGui::IsItemActive() || ImGui::IsItemDeactivated()) &&
				(ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
			if (enter || ImGui::Button("Go"))
			{
				if (enter)
				{
					ImGui::SetKeyOwner(ImGuiKey_Enter, ImGui::GetID("beam_goto_submit"), ImGuiInputFlags_LockThisFrame);
					ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, ImGui::GetID("beam_goto_submit"), ImGuiInputFlags_LockThisFrame);
				}
				doc.Editor.SetCursor(std::clamp(m_TargetLine, 1, std::max(1, doc.Editor.GetLineCount())) - 1, 0);
				doc.Editor.SetFocus();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				ImGui::SetKeyOwner(ImGuiKey_Escape, ImGui::GetID("beam_goto_cancel"), ImGuiInputFlags_LockThisFrame);
				doc.Editor.SetFocus();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void TextEditorPanel::OnImGuiRender(bool& isOpen)
	{
		if (m_RequestFocus)
		{
			ImGui::SetNextWindowFocus();
			m_RequestFocus = false;
		}
		const bool visible = ImGui::Begin("Beam", &isOpen);
		m_KeyboardFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (!visible)
		{
			ImGui::End();
			return;
		}

		if (!m_SessionRestored)
			RestoreSession();
		for (auto& document : m_Documents)
			RefreshDirty(*document);
		{
			const double now = Now();
			if ((m_KeyboardFocus && !m_WasFocused) || now - m_LastDiskCheck >= k_DiskCheckInterval)
			{
				CheckExternalChanges();
				m_LastDiskCheck = now;
			}
			m_WasFocused = m_KeyboardFocus;
			ScriptBuildResult buildResult;
			if (m_Build.Poll(buildResult))
				HandleBuildResult(buildResult);
			if (m_Build.IsRunning())
				m_BuildStatus = "Building... " + m_Build.GetLatestLine();
			m_Search.Poll(m_SearchResults, m_SearchFiles, m_SearchTruncated);
			UpdateBackups();
			if (m_SessionDirty && now - m_LastSessionSave >= k_SessionInterval)
				SaveSession();
		}
		HandleShortcuts();
		UI_Toolbar();
		UI_CloseConfirm();
		UI_DocumentsMenu();
		ImGui::Spacing();
		if (!m_OpenError.empty())
		{
			ImGuiEx::PanelMessage("Could not open file", m_OpenError.c_str(), true);
			if (ImGui::SmallButton("Dismiss##beam_open_error"))
				m_OpenError.clear();
		}

		if (m_DiffMode)
		{
			const auto leftUtf8 = m_LeftDiffPath.filename().u8string();
			const std::string leftName = m_LeftDiffPath.empty() ? "Left" : std::string(reinterpret_cast<const char*>(leftUtf8.data()), leftUtf8.size());
			const auto rightUtf8 = m_RightDiffPath.filename().u8string();
			const std::string rightName = m_RightDiffPath.empty() ? "Right" : std::string(reinterpret_cast<const char*>(rightUtf8.data()), rightUtf8.size());

			if (ImGuiEx::Property("Side By Side", m_DiffSideBySide))
				m_DiffEditor.SetSideBySideMode(m_DiffSideBySide);

			ImGui::TextWrapped("Read-only comparison: %s  <->  %s", leftName.c_str(), rightName.c_str());
			// The diff view lays glyphs out on a fixed-width grid, so it must draw with the mono face.
			{
				ImFont* mono = ImGuiEx::Fonts::Get("Mono");
				ImGuiEx::ScopedFont font(mono, mono->LegacySize * static_cast<float>(m_TextZoom) / 100.0f);
				m_DiffEditor.Render("##BeamDiff", {-1.0f, std::max(ImGui::GetTextLineHeight(), ImGui::GetContentRegionAvail().y)}, true);
			}
			ImGui::End();
			return;
		}

		// No tab bar (and no lone "+") when nothing is open — the toolbar's New button and Ctrl+N
		// already cover creating the first file.
		if (m_Documents.empty())
		{
			const ImGuiEx::PanelAction create[] = {
				{static_cast<int>(BeamAction::New), "New file", LUX_ICON_FILE_O, "Ctrl+N", true, false, true}
			};
			RunAction(ImGuiEx::PanelEmptyState("beam_empty", "No file open",
				"Create a text file here, or open an existing file from the Content Browser. "
				"Your open files stay with you when you change workspaces.", create));
			UI_BottomPanel(ProblemsHeight());
			ImGui::End();
			return;
		}

		UI_Tabs();

		Document* doc = ActiveDocument();
		if (!doc)
		{
			ImGui::End();
			return;
		}

		if (!doc->SaveError.empty())
			ImGuiEx::PanelMessage("Could not save this file", doc->SaveError.c_str(), true);
		UI_FileBars(*doc);
		UI_GoToLinePopup(*doc);

		// Editor fills the space above a one-line status bar pinned to the bottom.
		const float statusBarHeight = ImGui::GetTextLineHeightWithSpacing();
		const float problemsHeight = ProblemsHeight();
		const std::string editorId = std::format("##beam_editor_{}", static_cast<const void*>(doc));
		// TextEditor places every glyph on a grid one "#" wide, so a proportional font renders with
		// gaps around narrow characters. Push the mono face for the editor body.
		{
			ImFont* mono = ImGuiEx::Fonts::Get("Mono");
			ImGuiEx::ScopedFont font(mono, mono->LegacySize * static_cast<float>(m_TextZoom) / 100.0f);
			const float reserved = problemsHeight > 0.0f ? problemsHeight + ImGui::GetStyle().ItemSpacing.y : 0.0f;
			const float height = std::max(ImGui::GetTextLineHeight(), ImGui::GetContentRegionAvail().y - statusBarHeight - ImGui::GetStyle().ItemSpacing.y - reserved);
			doc->Editor.Render(editorId.c_str(), {-1.0f, height}, true);
		}
		RefreshDirty(*doc);
		UI_BottomPanel(problemsHeight);

		UI_StatusBar(*doc);

		ImGui::End();
	}
}
