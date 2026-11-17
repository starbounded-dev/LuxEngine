// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "MaterialGraphDocument.h"

#include "Lux/Asset/Asset.h"
#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/MaterialAsset.h"
#include "Lux/Serialization/Yaml.h"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Lux
{
	class MaterialPreview;

	// Edits material assets: one tab per open material, a live preview, undo through the editor's
	// undo stack, and explicit Save / Revert. Main thread only.
	class MaterialEditorPanel : public EditorPanel
	{
	public:
		using UndoPushFn = std::function<void(const std::string& label, std::function<void()> undo, std::function<void()> redo)>;

		MaterialEditorPanel();
		~MaterialEditorPanel() override; // out of line: MaterialPreview is incomplete here

		void OnImGuiRender(bool& isOpen) override;
		void OnProjectChanged(const Ref<Project>& project) override;
		void OnEvent(Event& event) override;
		void OnClose() override;
		void SaveState(Yaml::Writer& out) const override;
		void LoadState(const Yaml::Node& state) override;

		void OpenMaterial(AssetHandle handle);
		void SetUndoCallback(UndoPushFn callback) { m_PushUndo = std::move(callback); }
#ifndef LUX_DIST
		static bool RunSelfTests(const std::filesystem::path& directory, std::vector<std::string>& failures);
#endif

	private:
		using Document = MaterialGraphDocument;
		using MaterialState = MaterialGraphDocument::MaterialState;

		Document* GetActiveDocument() const;
		void StopPreview(AssetHandle next = 0);
		void RestoreTabs();
		void CloseDocument(size_t index);
		void RevertDocument(Document& document, Ref<MaterialAsset> material);

		void UI_Tabs();
		void UI_Toolbar(Document& document, Ref<MaterialAsset> material);
		void UI_Preview(float width);
		void UI_Properties(Document& document, Ref<MaterialAsset> material);
		void UI_Instance(Ref<MaterialAsset> material);
		void UI_GraphParameters(Ref<MaterialAsset> material);
		void UI_CloseConfirm();
		void TrackEdit(Document& document, Ref<MaterialAsset> material);

	private:
		std::vector<Scope<Document>> m_Documents;
		int m_ActiveDocument = -1;
		bool m_SelectActiveTab = false;

		int m_PendingCloseIndex = -1;
		bool m_OpenCloseConfirm = false;

		Ref<MaterialPreview> m_Preview;
		Yaml::Node m_GraphDefaults;
		Yaml::Node m_PendingTabs;
		bool m_TabsRestored = false;
		AssetHandle m_PreviewDocument = 0;
		bool m_ShowGraph = false;
		bool m_ShowPreview = true;
		bool m_NarrowPreview = false;
		bool m_Compact = false;
		bool m_KeyboardFocus = false;
		float m_PreviewFraction = 0.3f;
		AssetHandle m_OpenAfterFrame = 0;
		UndoPushFn m_PushUndo;
	};
}
