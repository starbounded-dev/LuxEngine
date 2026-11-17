// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Core/Base.h"
#include "Lux/Utilities/StringUtils.h"

#include <filesystem>
#include <format>
#include <functional>
#include <vector>
#include <string>

namespace Lux::Yaml { class Node; class Writer; }

namespace Lux
{
	// Shared tab identity and the file operations provided by each graph asset type.
	class GraphDocument
	{
	public:
		using OpenFn = std::function<Scope<GraphDocument>(AssetHandle, const std::filesystem::path&)>;
		using UndoPushFn = std::function<void(const std::string&, std::function<void()>, std::function<void()>)>;

		explicit GraphDocument(const Ref<Asset>& asset) : Handle(asset->Handle), Type(asset->GetAssetType()), Source(asset) {}
		virtual ~GraphDocument() = default;

		virtual bool IsDirty() const = 0;
		virtual bool Save() = 0;
		virtual void Discard() = 0;
		virtual AssetHandle Draw() = 0;
		virtual void OnDeactivate() {}
		virtual void SaveView(Yaml::Writer& out, bool navigation) const = 0;
		virtual void LoadView(const Yaml::Node& state, bool navigation) = 0;

		void PushUndo(std::function<void()> undo, std::function<void()> redo);
		bool CanUndo() const { return m_UndoCursor > 0; }
		bool CanRedo() const { return m_UndoCursor < m_History.size(); }
		void Undo();
		void Redo();

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
		const Ref<Asset> Source;
		bool DirtyTab = false;
		std::string TabLabel;
		std::string DisplayPath;
		std::string SaveError;

	private:
		struct Edit
		{
			std::function<void()> Undo;
			std::function<void()> Redo;
		};
		std::vector<Edit> m_History;
		size_t m_UndoCursor = 0;
		std::filesystem::path m_LabelPath;
	};
}
