// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <imgui/imgui.h>

#include <span>

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

	private:
		ax::NodeEditor::EditorContext* m_Context = nullptr;
		bool m_FitRequested = true;
	};
}
