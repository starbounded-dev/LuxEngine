// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Utilities/StringUtils.h"

#include <filesystem>
#include <format>
#include <string>

namespace Lux
{
	// Shared tab identity and the file operations provided by each graph asset type.
	class GraphDocument
	{
	public:
		GraphDocument(AssetHandle handle, AssetType type) : Handle(handle), Type(type) {}
		virtual ~GraphDocument() = default;

		virtual bool IsDirty() const = 0;
		virtual bool Save() = 0;
		virtual void Discard() = 0;

		void RefreshLabel(const std::filesystem::path& path)
		{
			if (path == m_LabelPath && !TabLabel.empty())
				return;
			m_LabelPath = path;
			DisplayPath = Utils::PathToUTF8(path);
			const std::string name = path.empty() ? "Graph" : Utils::PathToUTF8(path.stem());
			TabLabel = std::format("{}###graph_tab_{}", name, static_cast<uint64_t>(Handle));
		}

		const AssetHandle Handle;
		const AssetType Type;
		std::string TabLabel;
		std::string DisplayPath;
		std::string SaveError;

	private:
		std::filesystem::path m_LabelPath;
	};
}
