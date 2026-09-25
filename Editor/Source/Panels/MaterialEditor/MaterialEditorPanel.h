// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Editor/EditorPanel.h"
#include "Lux/Renderer/MaterialAsset.h"

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

		void OpenMaterial(AssetHandle handle);
		void SetUndoCallback(UndoPushFn callback) { m_PushUndo = std::move(callback); }

	private:
		// Every property the editor can change, so a whole edit can be captured, compared, undone,
		// reverted and checked for unsaved changes in one place.
		struct MaterialState
		{
			glm::vec3 AlbedoColor{ 1.0f };
			float Metalness = 0.0f;
			float Roughness = 0.5f;
			float Emission = 0.0f;
			float Transparency = 1.0f;
			bool CastShadows = true;
			bool UseNormalMap = false;
			AssetHandle AlbedoMap = 0;
			AssetHandle NormalMap = 0;
			AssetHandle MetalnessMap = 0;
			AssetHandle RoughnessMap = 0;
			MaterialSurfaceParameters Surface;
			int32_t AcousticTag = -1;

			bool operator==(const MaterialState& other) const = default;

			static MaterialState Capture(Ref<MaterialAsset> material);
			void Apply(Ref<MaterialAsset> material) const;
		};

		struct Document
		{
			AssetHandle Handle = 0;
			MaterialState Saved;          // state on disk (captured at open and on save)
			MaterialState EditBaseline;   // state before the edit currently in progress
			bool EditInProgress = false;
		};

		Document* GetActiveDocument();
		void CloseDocument(size_t index);
		void SaveDocument(Document& document);
		bool IsDirty(const Document& document) const;

		void UI_Tabs();
		void UI_Toolbar(Document& document, Ref<MaterialAsset> material);
		void UI_Preview(float width);
		void UI_Properties(Document& document, Ref<MaterialAsset> material);
		void UI_CloseConfirm();
		void TrackEdit(Document& document, Ref<MaterialAsset> material);

	private:
		std::vector<Document> m_Documents;
		int m_ActiveDocument = -1;
		bool m_SelectActiveTab = false;

		int m_PendingCloseIndex = -1;
		bool m_OpenCloseConfirm = false;

		Ref<MaterialPreview> m_Preview;
		UndoPushFn m_PushUndo;
	};
}
