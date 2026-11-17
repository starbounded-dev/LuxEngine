// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <imgui/imgui.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace ax::NodeEditor { struct EditorContext; }

namespace Lux
{
	// View-only services for one graph canvas. All operations run on the ImGui thread.
	class NodeCanvasCore
	{
	public:
		struct MinimapRect
		{
			ImVec2 Min;
			ImVec2 Max;
			ImU32 Color;
		};

		struct SearchItem
		{
			uint32_t ID;
			std::string_view Label;
			std::string_view Alias = {};
			std::string_view Category = {};
			std::string_view Help = {};
		};

		enum class Search { Find, Palette };
		enum class ClipboardAction { None, Copy, Paste, Duplicate };

		// The vendor stores a single current context. Restore the caller's binding when drawing ends.
		class ContextScope
		{
		public:
			explicit ContextScope(const NodeCanvasCore& canvas);
			~ContextScope();
			ContextScope(const ContextScope&) = delete;
			ContextScope& operator=(const ContextScope&) = delete;

		private:
			ax::NodeEditor::EditorContext* m_Previous;
		};

		NodeCanvasCore();
		~NodeCanvasCore();
		NodeCanvasCore(const NodeCanvasCore&) = delete;
		NodeCanvasCore& operator=(const NodeCanvasCore&) = delete;

		void Reset();
		void RequestFit() { m_FitRequested = true; }

		// Requires a bound context. Apply the fit after its nodes are submitted and draw the
		// minimap after the vendor canvas ends, when ImGui coordinates are in screen space again.
		void Begin(const char* id, bool enableShortcuts);
		void FitIfRequested();
		void DrawMinimap(ImVec2 canvasMin, ImVec2 canvasMax, std::span<const MinimapRect> rects) const;

		// Build document-specific choices only while the popup is requested or visible.
		static bool IsSearchOpen(Search search);
		uint32_t DrawSearch(Search search, bool open, std::span<const SearchItem> choices, uint32_t selected = 0);
		static ClipboardAction ReadClipboardAction();

	private:
		char m_FindQuery[64] = {};
		char m_PaletteQuery[64] = {};
		ax::NodeEditor::EditorContext* m_Context = nullptr;
		bool m_FitRequested = true;
	};
}
