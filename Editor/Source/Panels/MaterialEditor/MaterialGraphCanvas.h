// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/MaterialAsset.h"

#include <imgui/imgui.h>

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace ax::NodeEditor { struct EditorContext; }

namespace Lux {

	// The node canvas of the Material Editor for one material's graph (imgui-node-editor). The graph
	// is the source of truth: the canvas draws it each frame and writes edits straight into it, and
	// reports them so the registry recompiles the surface shaders once an edit is finished.
	// Main thread (ImGui).
	class MaterialGraphCanvas
	{
	public:
		using UndoPushFn = std::function<void(const std::string& label, std::function<void()> undo, std::function<void()> redo)>;

		MaterialGraphCanvas();
		~MaterialGraphCanvas();
		MaterialGraphCanvas(const MaterialGraphCanvas&) = delete;
		MaterialGraphCanvas& operator=(const MaterialGraphCanvas&) = delete;

		// Toolbar, canvas and the selected node's inspector, filling the available region.
		void Draw(const Ref<MaterialAsset>& material, const UndoPushFn& pushUndo);

		// The graph as text, for dirty checks and undo snapshots.
		static std::string Snapshot(const MaterialGraph& graph);
		static void Restore(MaterialGraph& graph, const std::string& snapshot);

	private:
		void DrawToolbar(const Ref<MaterialAsset>& material, MaterialGraph& graph);
		void DrawCanvas(const Ref<MaterialAsset>& material, MaterialGraph& graph);
		void DrawPalette(MaterialGraph& graph);
		void DrawInspector(const Ref<MaterialAsset>& material, MaterialGraph& graph);
		void DrawMinimap(MaterialGraph& graph, const ImVec2& canvasMin, const ImVec2& canvasMax);
		void CopySelection(const MaterialGraph& graph);
		void Paste(MaterialGraph& graph, ImVec2 offset);
		void TrackChanges(const Ref<MaterialAsset>& material, MaterialGraph& graph, const UndoPushFn& pushUndo);

	private:
		ax::NodeEditor::EditorContext* m_Context = nullptr;
		AssetHandle m_Material = 0;
		std::unordered_set<uint32_t> m_Placed; // nodes whose position the node editor already has
		bool m_NavigateToContent = true;

		uint32_t m_SelectedNode = 0;
		uint32_t m_PendingSelect = 0; // selected in the node editor next frame (found, or just added)
		bool m_NavigateToSelection = false;
		bool m_OpenPalette = false;
		ImVec2 m_PalettePosition{ 0.0f, 0.0f }; // canvas space
		uint32_t m_PaletteFromNode = 0;      // a link dragged into empty space: connect the new node to it
		uint32_t m_PaletteFromPin = 0;
		bool m_PaletteFromOutput = true;
		char m_PaletteSearch[64] = {};
		char m_FindSearch[64] = {};

		std::vector<MaterialGraphNode> m_ClipboardNodes;
		std::vector<MaterialGraphLink> m_ClipboardLinks;

		std::string m_Baseline;   // the graph before the edit in progress
		std::string m_LastCode;   // the generated code the registry last got
	};

}
