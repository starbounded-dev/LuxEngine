// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "GraphDocument.h"

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Serialization/Yaml.h"

#include <unordered_map>

namespace Lux
{
	class ContentBrowserPanel;

	class NodeCanvasPanel : public EditorPanel
	{
	public:
		static constexpr const char* PrimaryName = "Node Canvas###Material Editor";
		static constexpr const char* SecondaryName = "Node Canvas 2";

		explicit NodeCanvasPanel(const char* windowName = PrimaryName);
		const char* GetWindowName() const override { return m_WindowName.c_str(); }
		void OnImGuiRender(bool& isOpen) override;
		void OnClose() override;
		void OnProjectChanged(const Ref<Project>& project) override;
		void OnEvent(Event& event) override;
		void SaveState(Yaml::Writer& out) const override;
		void LoadState(const Yaml::Node& state) override;

		bool OpenAsset(AssetHandle handle);
		bool HasDocument(AssetHandle handle) const;
		double GetLastFocus() const { return m_LastFocus; }
		static void RegisterAssetActivation(ContentBrowserPanel& browser, const std::function<void(AssetHandle)>& open);

#ifndef LUX_DIST
		static bool RunSelfTests(const std::filesystem::path& directory, std::vector<std::string>& failures);
#endif

	private:
		bool OpenAsset(AssetHandle handle, bool requestFocus);
		GraphDocument* GetActiveDocument() const;
		int FindDocument(AssetHandle handle) const;
		void SelectDocument(int index, bool requestTab);
		void RestoreTabs();
		void RequestCloseDocument(int index);
		void CloseDocument(size_t index);
		void HandleShortcuts(GraphDocument& document) const;
		void UI_Tabs();
		void UI_CloseConfirm();
		bool IsSourceCurrent(const GraphDocument& document) const;

		std::string m_WindowName;
		std::string m_CloseDialogName;
		uint32_t m_CloseDialogID = 0;
		std::unordered_map<AssetType, GraphDocument::OpenFn> m_Factories;
		std::vector<Scope<GraphDocument>> m_Documents;
		int m_ActiveDocument = -1;
		int m_PendingCloseIndex = -1;
		bool m_SelectActiveTab = false;
		bool m_OpenCloseConfirm = false;
		bool m_RequestFocus = false;
		bool m_KeyboardFocus = false;
		bool m_TabsRestored = false;
		double m_LastFocus = -1.0;
		Yaml::Node m_PresentationDefaults;
		Yaml::Node m_PendingTabs;
		std::string m_Notice;
	};
}
