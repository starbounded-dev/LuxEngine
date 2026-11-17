// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "NodeCanvasCore.h"
#include "EditorSearch.h"

#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui/imgui_internal.h>

#include <imgui-node-editor/crude_json.h>
#include <imgui-node-editor/imgui_node_editor.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

namespace ed = ax::NodeEditor;

namespace Lux
{
	namespace
	{
		constexpr size_t k_MaxViewBytes = 64 * 1024;
		constexpr double k_MaxViewCoordinate = 1.0e9;

		std::string CanvasViewOnly(std::string_view state)
		{
			if (state.empty())
				return {};
			const auto settings = crude_json::value::parse(std::string(state));
			if (!settings.is_object())
				return {};

			const auto finite = [](const crude_json::value& value, double maximum)
			{
				return value.is_number() && std::abs(value.get<double>()) <= maximum;
			};
			const auto point = [&](const crude_json::value& value)
			{
				return value.is_object() && finite(value["x"], k_MaxViewCoordinate) && finite(value["y"], k_MaxViewCoordinate);
			};
			const auto& view = settings["view"];
			if (!view.is_object() || !point(view["scroll"]) || !finite(view["zoom"], 100.0) || view["zoom"].get<double>() < 0.001)
				return {};

			// Node positions and sizes are authoring data. Only camera and selection are view state.
			crude_json::object outputView{ { "scroll", view["scroll"] }, { "zoom", view["zoom"] } };
			const auto& visible = view["visible_rect"];
			if (visible.is_object() && point(visible["min"]) && point(visible["max"]))
				outputView["visible_rect"] = visible;
			crude_json::object output{ { "view", std::move(outputView) } };
			const auto& selection = settings["selection"];
			if (selection.is_array())
				output["selection"] = selection;

			std::string result = crude_json::value(std::move(output)).dump();
			return result.size() <= k_MaxViewBytes ? result : std::string();
		}

		const char* SearchPopup(NodeCanvasCore::Search search)
		{
			return search == NodeCanvasCore::Search::Palette ? "Add Node##node_canvas" : "Find node##node_canvas";
		}

		bool Matches(std::string_view text, std::string_view query)
		{
			return query.empty() || std::search(text.begin(), text.end(), query.begin(), query.end(),
				[](unsigned char item, unsigned char letter) { return std::tolower(item) == std::tolower(letter); }) != text.end();
		}
	}

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
		RestoreView({});
		m_FindQuery[0] = m_PaletteQuery[0] = '\0';
	}

	void NodeCanvasCore::RestoreView(std::string_view state)
	{
		std::string view = state.size() <= k_MaxViewBytes ? CanvasViewOnly(state) : std::string();
		CreateContext();
		m_View = std::move(view);
		m_FitRequested = m_View.empty();
	}

	void NodeCanvasCore::CreateContext()
	{
		ed::Config config;
		config.SettingsFile = nullptr;
		config.UserPointer = this;
		config.LoadSettings = [](char* data, void* user)
		{
			const auto& view = static_cast<const NodeCanvasCore*>(user)->m_View;
			if (data && !view.empty())
				std::memcpy(data, view.data(), view.size());
			return view.size();
		};
		config.SaveSettings = [](const char* data, size_t size, ed::SaveReasonFlags, void* user)
		{
			static_cast<NodeCanvasCore*>(user)->m_View = CanvasViewOnly({ data, size });
			return true;
		};
		auto* context = ed::CreateEditor(&config);
		if (m_Context)
			ed::DestroyEditor(m_Context);
		m_Context = context;
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

	bool NodeCanvasCore::IsSearchOpen(Search search)
	{
		return ImGui::IsPopupOpen(SearchPopup(search));
	}

	uint32_t NodeCanvasCore::DrawSearch(Search search, bool open, std::span<const SearchItem> choices, uint32_t selected)
	{
		const bool palette = search == Search::Palette;
		auto& query = palette ? m_PaletteQuery : m_FindQuery;
		const char* popup = SearchPopup(search);
		if (open)
		{
			ImGui::OpenPopup(popup);
			if (palette)
				query[0] = '\0';
		}

		const float font = ImGui::GetFontSize();
		const ImVec2 work = ImGui::GetWindowViewport()->WorkSize;
		const float width = std::min(font * (palette ? 23.0f : 26.0f), work.x * 0.9f);
		if (palette)
			ImGui::SetNextWindowSizeConstraints({ std::min(font * 16.0f, width), 0.0f }, { width, std::min(font * 28.0f, work.y * 0.8f) });
		else
			ImGui::SetNextWindowSize({ width, 0.0f }, ImGuiCond_Appearing);
		if (!ImGui::BeginPopup(popup))
			return 0;

		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		if (!palette)
			ImGui::SetNextItemWidth(-1.0f);
		const bool enter = ImGui::InputTextWithHint("##query", palette ? "Search nodes..." : "Find a node or parameter...",
			query, sizeof(query), ImGuiInputTextFlags_EnterReturnsTrue);

		uint32_t result = 0;
		const bool visible = palette || ImGui::BeginChild("matches", { 0.0f, ImGui::GetTextLineHeightWithSpacing() * 8.0f });
		if (visible)
		{
			const auto containsQuery = [&](const SearchItem& item)
			{
				return Matches(item.Label, query) || Matches(item.Alias, query) || Matches(item.Category, query);
			};
			const bool useFuzzy = !std::ranges::any_of(choices, containsQuery);
			const std::string normalizedQuery = useFuzzy ? Utils::String::ToLowerCopy(query) : std::string();
			const auto fuzzyMatches = [&](std::string_view text)
			{
				int score = 0;
				return EditorSearch::FuzzyMatch(normalizedQuery, Utils::String::ToLowerCopy(std::string(text)), score);
			};
			bool found = false;
			std::string_view category;
			for (const SearchItem& item : choices)
			{
				const bool match = useFuzzy
					? fuzzyMatches(item.Label) || fuzzyMatches(item.Alias) || fuzzyMatches(item.Category)
					: containsQuery(item);
				if (!match)
					continue;
				if (item.Category != category)
				{
					category = item.Category;
					ImGui::SeparatorText(std::string(category).c_str());
				}
				found = true;
				const ImGuiEx::ScopedID id(static_cast<int>(item.ID));
				if (ImGuiEx::PanelListItem("##result", item.Label, selected == item.ID, item.Help) || (enter && !result))
					result = item.ID;
			}
			if (!found && !palette)
				ImGui::TextUnformatted("No matching nodes.");
		}
		if (!palette)
			ImGui::EndChild();
		if (result)
		{
			if (enter)
				ImGui::SetKeyOwner(ImGuiKey_Enter, ImGui::GetID("search_submit"), ImGuiInputFlags_LockThisFrame);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return result;
	}

	NodeCanvasCore::ClipboardAction NodeCanvasCore::ReadClipboardAction()
	{
		if (!ed::BeginShortcut())
			return ClipboardAction::None;
		ClipboardAction action = ClipboardAction::None;
		if (ed::AcceptCopy())
			action = ClipboardAction::Copy;
		else if (ed::AcceptPaste())
			action = ClipboardAction::Paste;
		else if (ed::AcceptDuplicate())
			action = ClipboardAction::Duplicate;
		ed::EndShortcut();
		return action;
	}

	void NodeCanvasCore::DrawGroup(uint32_t id, const char* title, ImVec4 color, ImVec2 size)
	{
		ed::PushStyleColor(ed::StyleColor_NodeBg, color);
		ed::PushStyleColor(ed::StyleColor_NodeBorder, { color.x, color.y, color.z, 1.0f });
		ed::BeginNode(id);
		{
			const ImGuiEx::ScopedID scope(static_cast<int>(id));
			ImGui::TextUnformatted(*title ? title : "Comment");
			ed::Group(size);
		}
		ed::EndNode();
		ed::PopStyleColor(2);
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
