// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Editor/EditorPanel.h"
#include "Lux/Asset/AssetMetadata.h"
#include <imgui/imgui.h>
#include <string>
#include <vector>

namespace Lux {

	class EditorAssetManager;

	class AssetManagerPanel : public EditorPanel
	{
	public:
		AssetManagerPanel() = default;
		virtual ~AssetManagerPanel() = default;

		virtual void OnImGuiRender(bool& isOpen) override;
		void OnProjectChanged(const Ref<Project>& project) override;
		void OnClose() override;
		void OnEvent(Event& event) override;
#ifndef LUX_DIST
		static bool RunSelfTests(std::vector<std::string>& failures);
#endif

	private:
		struct RegistryRow
		{
			AssetHandle ID;
			AssetMetadata Metadata;
			std::string Path;
			std::string Handle;
			std::string Type;
			std::string Search;
			std::string Caption;
		};
		std::vector<RegistryRow> m_Rows;
		std::vector<size_t> m_VisibleRows;
		uint64_t m_RegistryRevision = 0;
		const EditorAssetManager* m_RegistryOwner = nullptr;
		AssetHandle m_Inspecting = 0;
		ImGuiID m_DetailsPopupID = 0;
		std::string m_Query;
		bool m_IsFocused = false;
		bool m_FocusSearch = false;
		char m_SearchBuffer[256]{};
		bool m_ShowOnlyLoaded = false;
	};

}
