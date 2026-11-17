// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "NodeCanvasCore.h"

#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiUtilities.h"

#include <imgui-node-editor/imgui_node_editor.h>

#include <algorithm>

namespace ed = ax::NodeEditor;

namespace Lux
{
	NodeCanvasCore::ContextScope::ContextScope(const NodeCanvasCore& canvas)
		: m_Previous(ed::GetCurrentEditor())
	{
		ed::SetCurrentEditor(canvas.m_Context);
	}

	NodeCanvasCore::ContextScope::~ContextScope()
	{
		ed::SetCurrentEditor(m_Previous);
	}

	NodeCanvasCore::NodeCanvasCore()
	{
		Reset();
	}

	NodeCanvasCore::~NodeCanvasCore()
	{
		if (m_Context)
			ed::DestroyEditor(m_Context);
	}

	void NodeCanvasCore::Reset()
	{
		if (m_Context)
			ed::DestroyEditor(m_Context);
		ed::Config config;
		config.SettingsFile = nullptr;
		m_Context = ed::CreateEditor(&config);
		RequestFit();
	}

	void NodeCanvasCore::Begin(const char* id, bool enableShortcuts)
	{
		ed::EnableShortcuts(enableShortcuts);
		auto& style = ed::GetStyle();
		style.Colors[ed::StyleColor_Bg] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::backgroundDark);
		style.Colors[ed::StyleColor_NodeBg] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::groupHeader);
		style.Colors[ed::StyleColor_NodeBorder] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::textDarker);
		style.Colors[ed::StyleColor_SelNodeBorder] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::accent);
		style.Colors[ed::StyleColor_SelLinkBorder] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::accent);
		const float font = ImGui::GetFontSize();
		style.NodePadding = { font * 0.6f, font * 0.5f, font * 0.6f, font * 0.5f };
		ed::Begin(id);
	}

	void NodeCanvasCore::FitIfRequested()
	{
		if (!m_FitRequested)
			return;
		ed::NavigateToContent(0.0f);
		m_FitRequested = false;
	}

	void NodeCanvasCore::DrawMinimap(ImVec2 canvasMin, ImVec2 canvasMax, std::span<const MinimapRect> rects) const
	{
		if (rects.empty())
			return;

		const float font = ImGui::GetFontSize();
		const ImVec2 mapMax(canvasMax.x - font * 0.5f, canvasMax.y - font * 0.5f);
		const ImVec2 mapMin(mapMax.x - font * 12.0f, mapMax.y - font * 8.0f);
		if (mapMin.x < canvasMin.x || mapMin.y < canvasMin.y)
			return;

		// Include the visible canvas and all the document's rectangles so the view indicator and
		// oversized comment groups both stay inside the map.
		const ImVec2 viewMin = ed::ScreenToCanvas(canvasMin);
		const ImVec2 viewMax = ed::ScreenToCanvas(canvasMax);
		ImVec2 boundsMin = viewMin;
		ImVec2 boundsMax = viewMax;
		for (const MinimapRect& rect : rects)
		{
			boundsMin = { std::min(boundsMin.x, rect.Min.x), std::min(boundsMin.y, rect.Min.y) };
			boundsMax = { std::max(boundsMax.x, rect.Max.x), std::max(boundsMax.y, rect.Max.y) };
		}

		const float scale = std::min((mapMax.x - mapMin.x) / std::max(boundsMax.x - boundsMin.x, 1.0f),
			(mapMax.y - mapMin.y) / std::max(boundsMax.y - boundsMin.y, 1.0f));
		const auto map = [&](ImVec2 position)
		{
			return ImVec2(mapMin.x + (position.x - boundsMin.x) * scale, mapMin.y + (position.y - boundsMin.y) * scale);
		};
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(mapMin, mapMax, Colors::Theme::backgroundPopup, ImGui::GetStyle().FrameRounding);
		for (const MinimapRect& rect : rects)
			drawList->AddRectFilled(map(rect.Min), map(rect.Max), rect.Color);
		drawList->AddRect(map(viewMin), map(viewMax), ImGuiEx::ColourWithAlpha(Colors::Theme::textBrighter, 0.63f));
	}
}
