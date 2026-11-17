// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "NodeCanvasPanel.h"

#include "Panels/ContentBrowserPanel.h"
#include "Panels/MaterialEditor/MaterialGraphDocument.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Asset/AssetReference.h"
#include "Lux/Core/Hash.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/ImGui/PanelControls.h"

#include <imgui/imgui.h>

namespace Lux
{
	namespace
	{
		struct DocumentType
		{
			AssetType Type;
			GraphDocument::OpenFn (*MakeFactory)();
		};

		constexpr DocumentType k_DocumentTypes[] = {
			{ AssetType::Material, MaterialGraphDocument::MakeFactory },
		};
	}

	NodeCanvasPanel::NodeCanvasPanel(const char* windowName)
		: m_WindowName(windowName), m_CloseDialogName(std::format("Unsaved graph###node_canvas_close_{}", Hash::GenerateFNVHash(windowName)))
	{
		for (const auto& type : k_DocumentTypes)
			m_Factories.emplace(type.Type, type.MakeFactory());
	}

	void NodeCanvasPanel::RegisterAssetActivation(ContentBrowserPanel& browser, const std::function<void(AssetHandle)>& open)
	{
		for (const auto& type : k_DocumentTypes)
		{
			browser.RegisterItemActivateCallbackForType(type.Type, [open](const AssetMetadata& metadata)
			{
				open(metadata.Handle);
			});
		}
	}

	bool NodeCanvasPanel::OpenAsset(AssetHandle handle)
	{
		return OpenAsset(handle, true);
	}

	bool NodeCanvasPanel::OpenAsset(AssetHandle handle, bool requestFocus)
	{
		if (!Project::GetActive() || !handle)
			return false;

		int index = FindDocument(handle);
		if (index >= 0 && !IsSourceCurrent(*m_Documents[index]))
		{
			CloseDocument(static_cast<size_t>(index));
			index = -1;
		}
		if (index < 0)
		{
			const auto metadata = Project::GetEditorAssetManager()->GetMetadata(handle);
			const auto factory = m_Factories.find(metadata.Type);
			if (!metadata.IsValid() || factory == m_Factories.end())
				return false;

			auto document = factory->second(handle, metadata.FilePath);
			if (!document)
			{
				LUX_CORE_ERROR_TAG("Editor", "Could not open graph asset '{}' in Node Canvas", Utils::PathToUTF8(metadata.FilePath));
				m_Notice = "Could not load the graph asset. Check its path and the Log panel.";
				return false;
			}
			document->LoadView(m_PresentationDefaults, false);
			index = static_cast<int>(m_Documents.size());
			m_Documents.push_back(std::move(document));
			m_TabsRestored = true;
		}
		SelectDocument(index, true);
		m_RequestFocus |= requestFocus;
		return true;
	}

	int NodeCanvasPanel::FindDocument(AssetHandle handle) const
	{
		const auto it = std::ranges::find_if(m_Documents, [handle](const auto& document) { return document->Handle == handle; });
		return it == m_Documents.end() ? -1 : static_cast<int>(it - m_Documents.begin());
	}

	bool NodeCanvasPanel::HasDocument(AssetHandle handle) const
	{
		return FindDocument(handle) >= 0;
	}

	GraphDocument* NodeCanvasPanel::GetActiveDocument() const
	{
		if (m_ActiveDocument < 0 || static_cast<size_t>(m_ActiveDocument) >= m_Documents.size())
			return nullptr;
		return m_Documents[m_ActiveDocument].get();
	}

	void NodeCanvasPanel::SelectDocument(int index, bool requestTab)
	{
		if (index != m_ActiveDocument)
		{
			if (GraphDocument* old = GetActiveDocument())
				old->OnDeactivate();
			m_ActiveDocument = index;
		}
		m_SelectActiveTab |= requestTab;
	}

	void NodeCanvasPanel::OnClose()
	{
		m_KeyboardFocus = false;
		m_PendingCloseIndex = -1;
		m_OpenCloseConfirm = false;
		ImGuiEx::ClosePopupByID(std::exchange(m_CloseDialogID, 0));
		if (GraphDocument* document = GetActiveDocument())
			document->OnDeactivate();
	}

	void NodeCanvasPanel::OnProjectChanged(const Ref<Project>& project)
	{
		OnClose();
		m_Documents.clear();
		m_ActiveDocument = m_PendingCloseIndex = -1;
		m_SelectActiveTab = m_OpenCloseConfirm = m_RequestFocus = m_TabsRestored = false;
		m_LastFocus = -1.0;
		m_Notice.clear();
		if (project)
			RestoreTabs();
	}

	bool NodeCanvasPanel::IsSourceCurrent(const GraphDocument& document) const
	{
		if (!Project::GetActive() || !AssetManager::IsAssetHandleValid(document.Handle))
			return false;
		const auto& loaded = AssetManager::GetLoadedAssets();
		const auto it = loaded.find(document.Handle);
		return it == loaded.end() || it->second == document.Source;
	}

	void NodeCanvasPanel::OnEvent(Event& event)
	{
		if (m_KeyboardFocus && event.IsInCategory(EventCategoryKeyboard))
			event.Handled = true;
	}

	void NodeCanvasPanel::HandleShortcuts(GraphDocument& document) const
	{
		const auto& io = ImGui::GetIO();
		if (!m_KeyboardFocus || io.WantTextInput || !io.KeyCtrl ||
			ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
			return;

		if (ImGui::IsKeyPressed(ImGuiKey_S, false))
			document.Save();
		if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
		{
			if (io.KeyShift)
				document.Redo();
			else
				document.Undo();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
			document.Redo();
	}

	void NodeCanvasPanel::SaveState(Yaml::Writer& out) const
	{
		const GraphDocument* active = GetActiveDocument();
		if (active)
			active->SaveView(out, false);

		if (!Project::GetActive())
			return;
		out << Yaml::Key << "Project" << Yaml::Value << Utils::PathToUTF8(Project::GetActive()->GetProjectFilePath());
		AssetReference::PathModeScope pathMode;
		if (active)
		{
			out << Yaml::Key << "ActiveTab" << Yaml::Value;
			AssetReference::WriteValue(out, active->Handle);
		}
		out << Yaml::Key << "OpenTabs" << Yaml::Value << Yaml::BeginSeq;
		for (const auto& document : m_Documents)
		{
			out << Yaml::BeginMap << Yaml::Key << "Asset" << Yaml::Value;
			AssetReference::WriteValue(out, document->Handle);
			document->SaveView(out, true);
			out << Yaml::EndMap;
		}
		out << Yaml::EndSeq;
	}

	void NodeCanvasPanel::LoadState(const Yaml::Node& state)
	{
		m_PresentationDefaults = state;
		if (GraphDocument* document = GetActiveDocument())
			document->LoadView(state, false);

		if (!m_TabsRestored && state["OpenTabs"].IsSequence())
		{
			m_PendingTabs = state;
			RestoreTabs();
		}
	}

	void NodeCanvasPanel::RestoreTabs()
	{
		if (m_TabsRestored || !Project::GetActive() || !m_PendingTabs)
			return;
		const auto state = std::exchange(m_PendingTabs, Yaml::Node{});
		if (state["Project"].Scalar() != Utils::PathToUTF8(Project::GetActive()->GetProjectFilePath()))
			return;
		m_TabsRestored = true;
		for (const auto& tab : state["OpenTabs"])
		{
			const AssetHandle handle = AssetReference::ReadValue(tab["Asset"]);
			if (OpenAsset(handle, false))
				GetActiveDocument()->LoadView(tab, true);
		}
		const int index = FindDocument(AssetReference::ReadValue(state["ActiveTab"]));
		if (index >= 0)
			SelectDocument(index, true);
	}

	void NodeCanvasPanel::OnImGuiRender(bool& isOpen)
	{
		m_KeyboardFocus = false;
		if (std::exchange(m_RequestFocus, false))
			ImGui::SetNextWindowFocus();
		ImGui::SetNextWindowSize({ 900.0f, 560.0f }, ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(m_WindowName.c_str(), &isOpen))
		{
			OnClose();
			ImGui::End();
			return;
		}

		m_KeyboardFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (m_KeyboardFocus)
			m_LastFocus = ImGui::GetTime();
		if (!Project::GetActive())
		{
			ImGuiEx::PanelEmptyState("canvas_no_project", "Open a project", "Graph documents belong to the active project.");
			ImGui::End();
			return;
		}

		for (size_t i = m_Documents.size(); i-- > 0;)
		{
			const auto& document = *m_Documents[i];
			if (IsSourceCurrent(document))
				continue;
			m_Notice = std::format("Closed {} because the source was removed or reloaded.", document.DisplayPath);
			CloseDocument(i);
		}
		if (!m_Notice.empty())
			ImGui::TextWrapped("%s", m_Notice.c_str());

		if (m_Documents.empty())
		{
			ImGuiEx::PanelEmptyState("canvas_empty", "No graph open", "Double-click a supported asset in the Content Browser to open it.");
			ImGui::End();
			return;
		}

		UI_Tabs();
		UI_CloseConfirm();
		if (GraphDocument* document = GetActiveDocument())
		{
			const ImGuiEx::ScopedID documentID(document->Handle);
			HandleShortcuts(*document);
			if (document->CanUndo() || document->CanRedo())
			{
				const ImGuiEx::PanelAction actions[] = {
					{ 1, "Undo", nullptr, "Ctrl+Z", document->CanUndo() },
					{ 2, "Redo", nullptr, "Ctrl+Shift+Z", document->CanRedo() },
				};
				const int action = ImGuiEx::PanelActionBar("document_history", actions);
				if (action == 1)
					document->Undo();
				else if (action == 2)
					document->Redo();
			}
			const AssetHandle openAfterDraw = document->Draw();
			document->DirtyTab = document->IsDirty();
			if (openAfterDraw)
				OpenAsset(openAfterDraw);
		}
		ImGui::End();
	}

	void NodeCanvasPanel::UI_Tabs()
	{
		int closeRequest = -1;
		if (ImGui::BeginTabBar("##node_canvas_tabs", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton))
		{
			const auto& registry = Project::GetEditorAssetManager()->GetAssetRegistry();
			for (size_t i = 0; i < m_Documents.size(); ++i)
			{
				GraphDocument& document = *m_Documents[i];
				document.RefreshLabel(registry.Get(document.Handle).FilePath);
				bool open = true;
				ImGuiTabItemFlags flags = document.DirtyTab ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
				if (m_SelectActiveTab && static_cast<int>(i) == m_ActiveDocument)
					flags |= ImGuiTabItemFlags_SetSelected;
				ImGui::SetNextItemWidth(std::min(ImGui::CalcTextSize(document.TabLabel.c_str(), nullptr, true).x +
					ImGui::GetFontSize() * 3.0f, ImGui::GetFontSize() * 18.0f));
				if (ImGui::BeginTabItem(document.TabLabel.c_str(), &open, flags))
				{
					if (!m_SelectActiveTab)
						SelectDocument(static_cast<int>(i), false);
					ImGui::EndTabItem();
				}
				ImGui::SetItemTooltip("%s", document.DisplayPath.c_str());
				if (!open)
					closeRequest = static_cast<int>(i);
			}
			ImGui::EndTabBar();
		}
		m_SelectActiveTab = false;
		if (closeRequest >= 0)
			RequestCloseDocument(closeRequest);
	}

	void NodeCanvasPanel::RequestCloseDocument(int index)
	{
		if (m_Documents[index]->IsDirty())
		{
			m_PendingCloseIndex = index;
			m_OpenCloseConfirm = true;
		}
		else
			CloseDocument(static_cast<size_t>(index));
	}

	void NodeCanvasPanel::CloseDocument(size_t index)
	{
		if (m_ActiveDocument == static_cast<int>(index))
			m_Documents[index]->OnDeactivate();
		if (m_PendingCloseIndex == static_cast<int>(index))
			m_PendingCloseIndex = -1;
		else if (m_PendingCloseIndex > static_cast<int>(index))
			--m_PendingCloseIndex;

		m_Documents.erase(m_Documents.begin() + static_cast<ptrdiff_t>(index));
		if (m_ActiveDocument > static_cast<int>(index))
			--m_ActiveDocument;
		if (m_ActiveDocument >= static_cast<int>(m_Documents.size()))
			m_ActiveDocument = static_cast<int>(m_Documents.size()) - 1;
		m_SelectActiveTab = true;
	}

	void NodeCanvasPanel::UI_CloseConfirm()
	{
		const char* title = m_CloseDialogName.c_str();
		if (std::exchange(m_OpenCloseConfirm, false))
		{
			m_CloseDialogID = ImGui::GetID(title);
			ImGui::OpenPopup(title);
		}
		if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		if (m_PendingCloseIndex >= 0)
		{
			GraphDocument& document = *m_Documents[m_PendingCloseIndex];
			ImGui::TextWrapped("Save changes to %s?", document.DisplayPath.c_str());
			if (!document.SaveError.empty())
				ImGuiEx::PanelMessage("Could not save", document.SaveError.c_str(), true);
			if (ImGui::Button("Save") && document.Save())
			{
				CloseDocument(static_cast<size_t>(m_PendingCloseIndex));
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Discard") && m_PendingCloseIndex >= 0)
			{
				document.Discard();
				CloseDocument(static_cast<size_t>(m_PendingCloseIndex));
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				m_PendingCloseIndex = -1;
				ImGui::CloseCurrentPopup();
			}
		}
		else
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}
}
