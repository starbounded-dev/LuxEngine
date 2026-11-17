// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "MaterialGraphCanvas.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiUtilities.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/Renderer/MaterialGraph/MaterialGraphRegistry.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui-node-editor/imgui_node_editor.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <map>
#include <cmath>

namespace ed = ax::NodeEditor;

namespace Lux {

	namespace {

		// Bumped by undo and redo, which replace a graph's content behind the canvas's back: the
		// canvas then places every node again from the graph.
		std::atomic<uint64_t> s_RestoreGeneration{ 0 };

		constexpr uint64_t k_OutputPinBit = 0x8000;
		constexpr ImVec2 k_NodeFootprint{ 160.0f, 80.0f };
		enum class GraphAction
		{
			Add,
			Fit,
			Find,
			Canvas,
			Details,
			Minimap,
			StopPreview
		};

		ed::PinId InputPin(uint32_t node, uint32_t pin) { return ed::PinId((static_cast<uint64_t>(node) << 16) | pin); }
		ed::PinId OutputPin(uint32_t node, uint32_t pin) { return ed::PinId((static_cast<uint64_t>(node) << 16) | k_OutputPinBit | pin); }

		struct DecodedPin
		{
			uint32_t Node = 0;
			uint32_t Pin = 0;
			bool Output = false;
		};

		DecodedPin Decode(ed::PinId id)
		{
			const uint64_t value = id.Get();
			return { static_cast<uint32_t>(value >> 16), static_cast<uint32_t>(value & (k_OutputPinBit - 1)), (value & k_OutputPinBit) != 0 };
		}

		ImVec4 CategoryColor(std::string_view category)
		{
			if (category == "Output") return ImVec4(0.95f, 0.55f, 0.25f, 1.0f);
			if (category == "Input") return ImVec4(0.55f, 0.75f, 0.95f, 1.0f);
			if (category == "Constant") return ImVec4(0.75f, 0.9f, 0.55f, 1.0f);
			if (category == "Texture") return ImVec4(0.9f, 0.7f, 0.95f, 1.0f);
			if (category == "Procedural") return ImVec4(0.95f, 0.85f, 0.45f, 1.0f);
			if (category == "Vector") return ImVec4(0.6f, 0.9f, 0.85f, 1.0f);
			if (category == "Function") return ImVec4(0.95f, 0.6f, 0.7f, 1.0f);
			return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
		}

		bool IsParameterNode(std::string_view type)
		{
			return type == "Float" || type == "Color" || type == "SampleTexture" || type == "NormalMap" || type == "Triplanar";
		}

		bool UsesTexture(std::string_view type)
		{
			return type == "SampleTexture" || type == "NormalMap" || type == "Triplanar";
		}

		bool EditPinValue(const char* label, glm::vec4& value, MaterialGraphType type)
		{
			switch (type)
			{
				case MaterialGraphType::Vec2: return ImGui::DragFloat2(label, &value.x, 0.01f);
				case MaterialGraphType::Vec3: return ImGui::DragFloat3(label, &value.x, 0.01f);
				case MaterialGraphType::Vec4: return ImGui::DragFloat4(label, &value.x, 0.01f);
				default: return ImGui::DragFloat(label, &value.x, 0.01f);
			}
		}
	}

	MaterialGraphCanvas::MaterialGraphCanvas() = default;

	MaterialGraphCanvas::~MaterialGraphCanvas()
	{
		StopPreview();
	}

	void MaterialGraphCanvas::StopPreview()
	{
		m_PreviewNode = 0;
		m_PreviewSource.clear();
		if (!m_PreviewMaterial)
			return;
		MaterialGraphRegistry::Unregister(m_PreviewMaterial);
		if (Project::GetActive())
			AssetManager::RemoveAsset(m_PreviewMaterial);
		m_PreviewMaterial = 0;
	}

	void MaterialGraphCanvas::PreviewNode(uint32_t node)
	{
		m_PreviewNode = node;
		m_PreviewSource.clear();
	}

	void MaterialGraphCanvas::UpdatePreview(const Ref<MaterialAsset>& material, const MaterialGraph& graph)
	{
		const MaterialGraphNode* node = m_PreviewNode ? graph.FindNode(m_PreviewNode) : nullptr;
		if (!node || MaterialGraph::GetPins(*node).Outputs.empty())
		{
			StopPreview();
			return;
		}

		// Rebuilt (and recompiled) only when the graph's code or the previewed node changes.
		std::string source = std::format("{}|{}", m_PreviewNode, m_LastCode);
		if (source == m_PreviewSource)
			return;
		m_PreviewSource = std::move(source);

		// The same graph, with the node's first output as emission over a black surface.
		Ref<MaterialGraph> preview = graph.Clone();
		preview->DispatchIndex = 0;
		preview->HasPackedParameters = false;
		uint32_t outputID = 0;
		for (const MaterialGraphNode& candidate : preview->Nodes)
		{
			if (candidate.Type == "Output")
				outputID = candidate.ID;
		}
		std::erase_if(preview->Links, [outputID](const MaterialGraphLink& link) { return link.ToNode == outputID; });
		const uint32_t black = preview->AddNode("Vector", glm::vec2(0.0f))->ID;
		preview->Connect(black, 0, outputID, 0);
		preview->Connect(m_PreviewNode, 0, outputID, 3);

		if (!m_PreviewMaterial)
			m_PreviewMaterial = AssetManager::AddMemoryOnlyAsset(Ref<MaterialAsset>::Create());
		if (Ref<MaterialAsset> previewMaterial = AssetManager::GetAsset<MaterialAsset>(m_PreviewMaterial))
		{
			previewMaterial->SetGraph(preview);
			previewMaterial->SetGraphParameters(material->GetGraphParameters());
			MaterialGraphRegistry::Register(m_PreviewMaterial);
			MaterialGraphRegistry::MarkDirty();
		}
	}

	std::string MaterialGraphCanvas::Snapshot(const MaterialGraph& graph)
	{
		Yaml::Writer out;
		graph.Serialize(out);
		return out.str();
	}

	void MaterialGraphCanvas::Restore(MaterialGraph& graph, const std::string& snapshot)
	{
		if (Ref<MaterialGraph> restored = MaterialGraph::Deserialize(Yaml::Load(snapshot)))
			graph.CopyContentFrom(*restored);
		s_RestoreGeneration.fetch_add(1, std::memory_order_relaxed);
	}

	void MaterialGraphCanvas::SaveState(Yaml::Writer& out) const
	{
		out << Yaml::Key << "GraphDetails" << Yaml::Value << m_ShowDetails;
		out << Yaml::Key << "GraphNarrowDetails" << Yaml::Value << m_NarrowDetails;
		out << Yaml::Key << "GraphMinimap" << Yaml::Value << m_ShowMinimap;
		out << Yaml::Key << "GraphCanvasFraction" << Yaml::Value << m_CanvasFraction;
	}

	void MaterialGraphCanvas::LoadState(const Yaml::Node& state)
	{
		m_ShowDetails = state["GraphDetails"].as<bool>(true);
		m_NarrowDetails = state["GraphNarrowDetails"].as<bool>(false);
		m_ShowMinimap = state["GraphMinimap"].as<bool>(true);
		const float fraction = state["GraphCanvasFraction"].as<float>(0.7f);
		m_CanvasFraction = std::isfinite(fraction) ? std::clamp(fraction, 0.2f, 0.9f) : 0.7f;
	}

	void MaterialGraphCanvas::SaveView(Yaml::Writer& out) const
	{
		SaveState(out);
		out << Yaml::Key << "CanvasView" << Yaml::Value << m_Canvas.GetView();
	}

	void MaterialGraphCanvas::LoadView(const Yaml::Node& state)
	{
		LoadState(state);
		m_Canvas.RestoreView(state["CanvasView"].Scalar());
		m_Placed.clear();
		m_CommentNodeSizes.clear();
	}

	void MaterialGraphCanvas::Draw(const Ref<MaterialAsset>& material, const UndoPushFn& pushUndo)
	{
		Ref<MaterialGraph> graph = material ? material->GetGraph() : nullptr;
		if (!graph)
			return;

		const uint64_t generation = s_RestoreGeneration.load(std::memory_order_relaxed);
		if (material->Handle != m_Material || generation != m_SeenGeneration)
		{
			const bool switched = material->Handle != m_Material;
			if (switched)
				StopPreview();
			m_Material = material->Handle;
			m_SeenGeneration = generation;
			m_Placed.clear();
			m_CommentNodeSizes.clear();
			m_SelectedNode = 0;
			m_Baseline = Snapshot(*graph);
			if (switched)
			{
				m_LastCode = graph->Compile().Body;
			}
		}

		const float font = ImGui::GetFontSize();
		const ImGuiEx::PanelSplit split = ImGuiEx::PanelSplitWidths(
			ImGui::GetContentRegionAvail().x, m_CanvasFraction, font * 23.0f, font * 16.0f);
		m_Compact = !split.SideBySide;
		DrawToolbar(material, *graph);
		const bool detailsOnly = m_Compact && m_NarrowDetails;
		const bool detailsBeside = !m_Compact && m_ShowDetails;
		if (!detailsOnly)
		{
			if (ImGui::BeginChild("##material_graph_canvas", {detailsBeside ? split.First : 0.0f, 0.0f},
								  ImGuiChildFlags_None,
								  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
				DrawCanvas(material, *graph);
			ImGui::EndChild();
		}
		if (detailsBeside)
		{
			ImGui::SameLine();
			ImGuiEx::PanelSplitHandle("##material_graph_split", split, m_CanvasFraction);
			ImGui::SameLine();
		}
		if (detailsOnly || detailsBeside)
		{
			if (ImGui::BeginChild("##material_graph_inspector", {0.0f, 0.0f}, ImGuiChildFlags_Borders))
				DrawInspector(material, *graph);
			ImGui::EndChild();
		}

		TrackChanges(material, *graph, pushUndo);
		UpdatePreview(material, *graph);
	}

	void MaterialGraphCanvas::DrawToolbar(const Ref<MaterialAsset>& material, MaterialGraph& graph)
	{
		const ImGuiEx::PanelAction actions[] = {
			{static_cast<int>(GraphAction::Add), "Add Node", LUX_ICON_PLUS, "Space over canvas", true, false,
			 true},
			{static_cast<int>(GraphAction::Fit), "Fit", LUX_ICON_EXPAND, nullptr, true, false, true},
			{static_cast<int>(GraphAction::Find), "Find", LUX_ICON_SEARCH},
			{static_cast<int>(GraphAction::Canvas), "Canvas", LUX_ICON_SITEMAP, nullptr, true,
			 !m_Compact || !m_NarrowDetails},
			{static_cast<int>(GraphAction::Details), "Details", LUX_ICON_SLIDERS, nullptr, true,
			 m_Compact ? m_NarrowDetails : m_ShowDetails},
			{static_cast<int>(GraphAction::Minimap), "Minimap", nullptr, nullptr, true, m_ShowMinimap, false,
			 true},
			{static_cast<int>(GraphAction::StopPreview), "Stop Node Preview", nullptr, nullptr,
			 m_PreviewNode != 0, false, false, true}};
		const int action = ImGuiEx::PanelActionBar("graph_actions", actions);
		if (action == static_cast<int>(GraphAction::Add))
		{
			m_OpenPalette = true;
			m_PaletteFromNode = 0;
			m_PaletteAtCenter = true;
			m_NarrowDetails = false;
		}
		if (action == static_cast<int>(GraphAction::Fit))
		{
			m_Canvas.RequestFit();
			m_NarrowDetails = false;
		}
		if (action == static_cast<int>(GraphAction::Find))
			m_OpenFind = true;
		if (action == static_cast<int>(GraphAction::Canvas))
			m_NarrowDetails = false;
		if (action == static_cast<int>(GraphAction::Details))
		{
			if (m_Compact)
				m_NarrowDetails = !m_NarrowDetails;
			else
				m_ShowDetails = !m_ShowDetails;
		}
		if (action == static_cast<int>(GraphAction::Minimap))
			m_ShowMinimap = !m_ShowMinimap;
		if (action == static_cast<int>(GraphAction::StopPreview))
			StopPreview();
		if (m_OpenFind || NodeCanvasCore::IsSearchOpen(NodeCanvasCore::Search::Find))
		{
			m_FindChoices.clear();
			m_FindChoices.reserve(graph.Nodes.size());
			for (const MaterialGraphNode& node : graph.Nodes)
			{
				const auto* info = MaterialGraph::FindNodeInfo(node.Type);
				const std::string_view name = info ? info->Name : std::string_view(node.Type);
				const std::string_view label = node.Parameter.empty() ? name : std::string_view(node.Parameter);
				m_FindChoices.push_back({ node.ID, label, name });
			}
			if (const uint32_t chosen = m_Canvas.DrawSearch(NodeCanvasCore::Search::Find, m_OpenFind, m_FindChoices, m_SelectedNode))
			{
				m_PendingSelect = chosen;
				m_NavigateToSelection = true;
				m_NarrowDetails = false;
			}
			m_OpenFind = false;
			m_FindChoices.clear(); // Views into the graph are only valid until the next edit.
		}

		// Stats: what the graph costs.
		size_t textures = 0;
		size_t parameters = 0;
		for (const MaterialGraphNode& node : graph.Nodes)
		{
			textures += UsesTexture(node.Type) ? (node.Type == "Triplanar" ? 3 : 1) : 0;
			parameters += node.Parameter.empty() ? 0 : 1;
		}
		{
			ImGuiEx::ScopedColour color(ImGuiCol_Text, Colors::Theme::textCaption);
			ImGui::TextWrapped("%zu nodes, %zu links", graph.Nodes.size(), graph.Links.size());
			ImGui::SetItemTooltip("%zu texture samples, %zu parameters. Select a node for Details. "
								  "Right-drag to pan; scroll to zoom.",
								  textures, parameters);
		}

		const auto& errors = MaterialGraphRegistry::GetErrors(material->Handle);
		if (!errors.empty() && ImGui::CollapsingHeader("Compile errors", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginChild("graph_errors",
								  {0.0f, std::max(ImGui::GetFrameHeight(),
												  std::min(ImGui::GetTextLineHeightWithSpacing() * 6.0f,
														   ImGui::GetContentRegionAvail().y * 0.3f))}))
			{
				for (size_t i = 0; i < errors.size(); ++i)
				{
					const auto& error = errors[i];
					ImGuiEx::ScopedID id(static_cast<int>(i));
					const MaterialGraphNode* node = graph.FindNode(error.Node);
					ImGuiEx::PanelMessage(node ? node->Type.c_str() : "Graph", error.Message.c_str(), true);
					if (node && ImGui::SmallButton("Go to node"))
					{
						m_PendingSelect = error.Node;
						m_NavigateToSelection = true;
						m_NarrowDetails = false;
					}
				}
			}
			ImGui::EndChild();
		}
	}

	void MaterialGraphCanvas::DrawCanvas(const Ref<MaterialAsset>& material, MaterialGraph& graph)
	{
		const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
		const ImVec2 canvasMax = ImVec2(canvasMin.x + ImGui::GetContentRegionAvail().x, canvasMin.y + ImGui::GetContentRegionAvail().y);

		std::unordered_set<uint32_t> errorNodes;
		for (const MaterialGraph::Error& error : MaterialGraphRegistry::GetErrors(material->Handle))
			errorNodes.insert(error.Node);

		const NodeCanvasCore::ContextScope context(m_Canvas);
		const bool shortcuts = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
			!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
		m_Canvas.Begin("MaterialGraph", shortcuts);

		// Comments first, so nodes draw over them.
		for (const MaterialGraphComment& comment : graph.Comments)
		{
			if (m_Placed.insert(comment.ID).second)
				ed::SetNodePosition(comment.ID, ImVec2(comment.Position.x, comment.Position.y));
			if (m_ResizeComment == comment.ID)
			{
				ed::SetGroupSize(comment.ID, ImVec2(comment.Size.x, comment.Size.y));
				m_CommentNodeSizes.erase(comment.ID); // the new node size is not a drag-resize
				m_ResizeComment = 0;
			}
			NodeCanvasCore::DrawGroup(comment.ID, comment.Text.c_str(),
				{ comment.Color.r, comment.Color.g, comment.Color.b, comment.Color.a }, { comment.Size.x, comment.Size.y });
		}

		for (const MaterialGraphNode& node : graph.Nodes)
		{
			const MaterialGraphNodeInfo* info = MaterialGraph::FindNodeInfo(node.Type);
			if (m_Placed.insert(node.ID).second)
				ed::SetNodePosition(node.ID, ImVec2(node.Position.x, node.Position.y));

			const bool hasError = errorNodes.contains(node.ID);
			if (hasError)
				ed::PushStyleColor(ed::StyleColor_NodeBorder,
								   ImGui::ColorConvertU32ToFloat4(Colors::Theme::textError));

			ed::BeginNode(node.ID);
			ImGui::PushID(static_cast<int>(node.ID));
			const std::string title = info ? std::string(info->Name) : node.Type;
			ImGui::TextColored(info ? CategoryColor(info->Category) : ImVec4(1.0f, 0.3f, 0.25f, 1.0f), "%s", title.c_str());
			if (!node.Parameter.empty())
				ImGui::TextDisabled("\"%s\"", node.Parameter.c_str());
			else if (info && !info->Modes.empty() && node.Mode >= 0 && node.Mode < static_cast<int32_t>(info->Modes.size()))
				ImGui::TextDisabled("%s", std::string(info->Modes[node.Mode]).c_str());

			if (info)
			{
				const MaterialGraph::Pins pins = MaterialGraph::GetPins(node);
				if (node.Type == "SubGraph")
					ImGui::TextDisabled("%s", node.Graph ? AssetManager::GetAssetType(node.Graph) == AssetType::Material
						? Project::GetEditorAssetManager()->GetMetadata(node.Graph).FilePath.stem().string().c_str() : "(missing)" : "(none)");
				ImGui::BeginGroup();
				for (uint32_t pin = 0; pin < pins.Inputs.size(); pin++)
				{
					ed::BeginPin(InputPin(node.ID, pin), ed::PinKind::Input);
					const bool connected = graph.FindLinkInto(node.ID, pin) != nullptr;
					ImGuiEx::ScopedColour color(ImGuiCol_Text, connected ? Colors::Theme::textBrighter
																		 : Colors::Theme::textCaption);
					const auto name = pins.Inputs[pin].Name;
					ImGui::Text("%s %.*s", connected ? LUX_ICON_CIRCLE : LUX_ICON_CIRCLE_O,
								static_cast<int>(name.size()), name.data());
					ed::EndPin();
				}
				ImGui::EndGroup();
				if (!pins.Inputs.empty() && !pins.Outputs.empty())
					ImGui::SameLine(0.0f, ImGui::GetFontSize() * 1.6f);
				ImGui::BeginGroup();
				for (uint32_t pin = 0; pin < pins.Outputs.size(); pin++)
				{
					ed::BeginPin(OutputPin(node.ID, pin), ed::PinKind::Output);
					const auto name = pins.Outputs[pin].Name;
					ImGui::Text("%.*s %s", static_cast<int>(name.size()), name.data(), LUX_ICON_CIRCLE);
					ed::EndPin();
				}
				ImGui::EndGroup();
			}
			ImGui::PopID();
			ed::EndNode();
			if (hasError)
				ed::PopStyleColor();
		}

		for (const MaterialGraphLink& link : graph.Links)
			ed::Link(link.ID, OutputPin(link.FromNode, link.FromPin), InputPin(link.ToNode, link.ToPin),
					 ImGui::ColorConvertU32ToFloat4(Colors::Theme::textBrighter),
					 std::max(1.5f, ImGui::GetFontSize() * 0.12f));

		// New links, and links dragged into empty space (which open the palette).
		if (ed::BeginCreate(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), 2.0f))
		{
			ed::PinId startId, endId;
			if (ed::QueryNewLink(&startId, &endId))
			{
				DecodedPin a = Decode(startId);
				DecodedPin b = Decode(endId);
				if (!a.Output)
					std::swap(a, b);
				if (a.Output && !b.Output && graph.CanConnect(a.Node, a.Pin, b.Node, b.Pin))
				{
					if (ed::AcceptNewItem(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), 2.0f))
						graph.Connect(a.Node, a.Pin, b.Node, b.Pin);
				}
				else
				{
					ed::RejectNewItem(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), 2.0f);
				}
			}

			ed::PinId pinId;
			if (ed::QueryNewNode(&pinId) && ed::AcceptNewItem())
			{
				const DecodedPin pin = Decode(pinId);
				m_OpenPalette = true;
				m_PaletteFromNode = pin.Node;
				m_PaletteFromPin = pin.Pin;
				m_PaletteFromOutput = pin.Output;
				m_PalettePosition = ImGui::GetMousePos(); // canvas space while the editor is not suspended
			}
			ed::EndCreate();
		}

		if (ed::BeginDelete())
		{
			ed::LinkId linkId;
			while (ed::QueryDeletedLink(&linkId))
			{
				if (ed::AcceptDeletedItem())
					graph.RemoveLink(static_cast<uint32_t>(linkId.Get()));
			}

			ed::NodeId nodeId;
			while (ed::QueryDeletedNode(&nodeId))
			{
				const uint32_t id = static_cast<uint32_t>(nodeId.Get());
				const MaterialGraphNode* node = graph.FindNode(id);
				if (node && node->Type == "Output")
				{
					ed::RejectDeletedItem();
					continue;
				}
				if (!ed::AcceptDeletedItem())
					continue;
				if (node)
					graph.RemoveNode(id);
				else
					std::erase_if(graph.Comments, [id](const MaterialGraphComment& comment) { return comment.ID == id; });
				m_Placed.erase(id);
				if (m_SelectedNode == id)
					m_SelectedNode = 0;
			}
			ed::EndDelete();
		}

		switch (NodeCanvasCore::ReadClipboardAction())
		{
			case NodeCanvasCore::ClipboardAction::Copy:
				CopySelection(graph);
				break;
			case NodeCanvasCore::ClipboardAction::Paste:
				Paste(graph, ImGui::GetMousePos()); // canvas space while the editor is not suspended
				break;
			case NodeCanvasCore::ClipboardAction::Duplicate:
				CopySelection(graph);
				Paste(graph, ImVec2(-1.0f, -1.0f));
				break;
			default:
				break;
		}

		if (m_PendingSelect && graph.FindNode(m_PendingSelect))
		{
			ed::SelectNode(m_PendingSelect);
			if (m_NavigateToSelection)
				ed::NavigateToSelection();
		}
		m_PendingSelect = 0;
		m_NavigateToSelection = false;
		if (!graph.Nodes.empty())
			m_Canvas.FitIfRequested();

		// Popups and the context menu work in ImGui's screen space, so the editor is suspended.
		ed::Suspend();
		if (m_OpenPalette && m_PaletteAtCenter)
		{
			m_PalettePosition = ed::ScreenToCanvas(
				{(canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f});
			m_PaletteAtCenter = false;
		}
		if (ed::ShowBackgroundContextMenu() ||
			(shortcuts && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Space, false)))
		{
			m_OpenPalette = true;
			m_PaletteFromNode = 0;
			m_PalettePosition = ed::ScreenToCanvas(ImGui::GetMousePos());
		}
		DrawPalette(graph);
		ed::Resume();

		ed::End();

		// Positions back into the graph (a moved node is an edit, so it is one undo step).
		for (MaterialGraphNode& node : graph.Nodes)
		{
			const ImVec2 position = ed::GetNodePosition(node.ID);
			// A sub-pixel drift (editor round-trip of a fresh float, e.g. a paste at the mouse) is not an edit.
			if (position.x != FLT_MAX && (std::abs(position.x - node.Position.x) > 0.01f || std::abs(position.y - node.Position.y) > 0.01f))
				node.Position = glm::vec2(position.x, position.y);
		}
		for (MaterialGraphComment& comment : graph.Comments)
		{
			const ImVec2 position = ed::GetNodePosition(comment.ID);
			if (position.x != FLT_MAX && (position.x != comment.Position.x || position.y != comment.Position.y))
				comment.Position = glm::vec2(position.x, position.y);

			// The node editor takes a group's size only when it creates it, and its node size adds the
			// header, so a resize is carried over as the change in node size between frames.
			const ImVec2 size = ed::GetNodeSize(comment.ID);
			auto [it, first] = m_CommentNodeSizes.try_emplace(comment.ID, size);
			if (!first && (size.x != it->second.x || size.y != it->second.y) && it->second.x > 0.0f)
				comment.Size = glm::max(comment.Size + glm::vec2(size.x - it->second.x, size.y - it->second.y), glm::vec2(50.0f));
			it->second = size;
		}

		ed::NodeId selected[1];
		if (ed::GetSelectedNodes(selected, 1) == 1)
			m_SelectedNode = static_cast<uint32_t>(selected[0].Get());
		else if (ed::GetSelectedObjectCount() == 0)
			m_SelectedNode = 0;

		// Minimap over the canvas's bottom-right corner.
		if (m_ShowMinimap)
			DrawMinimap(graph, canvasMin, canvasMax);
	}

	void MaterialGraphCanvas::DrawMinimap(const MaterialGraph& graph, const ImVec2& canvasMin, const ImVec2& canvasMax)
	{
		if (graph.Nodes.empty())
			return;

		m_MinimapRects.clear();
		m_MinimapRects.reserve(graph.Comments.size() + graph.Nodes.size());
		const ImU32 commentColor = ImGuiEx::ColourWithAlpha(Colors::Theme::muted, 0.47f);
		for (const MaterialGraphComment& comment : graph.Comments)
		{
			m_MinimapRects.push_back({ { comment.Position.x, comment.Position.y },
				{ comment.Position.x + comment.Size.x, comment.Position.y + comment.Size.y }, commentColor });
		}
		for (const MaterialGraphNode& node : graph.Nodes)
		{
			const MaterialGraphNodeInfo* info = MaterialGraph::FindNodeInfo(node.Type);
			ImVec4 color = info ? CategoryColor(info->Category) : ImGui::ColorConvertU32ToFloat4(Colors::Theme::textError);
			color.w = node.ID == m_SelectedNode ? 1.0f : 0.6f;
			m_MinimapRects.push_back({ { node.Position.x, node.Position.y },
				{ node.Position.x + k_NodeFootprint.x, node.Position.y + k_NodeFootprint.y },
				ImGui::ColorConvertFloat4ToU32(color) });
		}
		m_Canvas.DrawMinimap(canvasMin, canvasMax, m_MinimapRects);
	}

	void MaterialGraphCanvas::DrawPalette(MaterialGraph& graph)
	{
		if (!m_OpenPalette && !NodeCanvasCore::IsSearchOpen(NodeCanvasCore::Search::Palette))
			return;

		const auto library = MaterialGraph::GetNodeLibrary();
		static const auto choices = []
		{
			const auto nodes = MaterialGraph::GetNodeLibrary();
			std::vector<NodeCanvasCore::SearchItem> items;
			items.reserve(nodes.size() + 1);
			for (uint32_t i = 0; i < nodes.size(); ++i)
			{
				const auto& node = nodes[i];
				if (node.Type != "Output")
					items.push_back({ i + 1, node.Name, {}, node.Category, node.Help });
			}
			items.push_back({ static_cast<uint32_t>(nodes.size() + 1), "Comment", {}, "Organize" });
			return items;
		}();
		const uint32_t chosen = m_Canvas.DrawSearch(NodeCanvasCore::Search::Palette, m_OpenPalette, choices);
		m_OpenPalette = false;
		if (!chosen)
			return;

		auto place = [&](std::string_view type)
		{
			glm::vec2 position(m_PalettePosition.x, m_PalettePosition.y);
			// Step down until clear of existing nodes (a typical node's footprint when its size is unknown).
			for (int attempt = 0; type != "Comment" && attempt < 32; attempt++)
			{
				bool overlaps = false;
				for (const MaterialGraphNode& other : graph.Nodes)
				{
					ImVec2 size = ed::GetNodeSize(other.ID);
					if (size.x <= 0.0f || size.y <= 0.0f)
						size = ImVec2(200.0f, 120.0f);
					overlaps |= position.x < other.Position.x + size.x && other.Position.x < position.x + 200.0f &&
								position.y < other.Position.y + size.y && other.Position.y < position.y + 120.0f;
				}
				if (!overlaps)
					break;
				position.y += 40.0f;
			}
			if (type == "Comment")
			{
				MaterialGraphComment comment;
				comment.ID = graph.NextID++;
				comment.Position = position;
				comment.Text = "Comment";
				graph.Comments.push_back(comment);
				return;
			}
			MaterialGraphNode* node = graph.AddNode(type, position);
			if (!node)
				return;
			const uint32_t id = node->ID;
			// From a dragged link: connect the new node's first pin of the other direction.
			if (m_PaletteFromNode)
			{
				const MaterialGraph::Pins pins = MaterialGraph::GetPins(*graph.FindNode(id));
				if (m_PaletteFromOutput && !pins.Inputs.empty())
					graph.Connect(m_PaletteFromNode, m_PaletteFromPin, id, 0);
				else if (!m_PaletteFromOutput && !pins.Outputs.empty())
					graph.Connect(id, 0, m_PaletteFromNode, m_PaletteFromPin);
			}
			m_PendingSelect = id;
		};

		place(chosen <= library.size() ? library[chosen - 1].Type : "Comment");
	}

	void MaterialGraphCanvas::DrawInspector(const Ref<MaterialAsset>& material, MaterialGraph& graph)
	{
		MaterialGraphNode* node = m_SelectedNode ? graph.FindNode(m_SelectedNode) : nullptr;
		auto comment = std::ranges::find(graph.Comments, m_SelectedNode, &MaterialGraphComment::ID);
		if (!node && comment != graph.Comments.end())
		{
			ImGui::TextUnformatted("Comment");
			ImGui::Separator();
			char text[256];
			std::snprintf(text, sizeof(text), "%s", comment->Text.c_str());
			if (ImGui::InputText("Text", text, sizeof(text)))
				comment->Text = text;
			ImGui::ColorEdit4("Color", &comment->Color.r);
			if (ImGui::DragFloat2("Size", &comment->Size.x, 1.0f, 50.0f, 4000.0f))
				m_ResizeComment = comment->ID;
			return;
		}
		if (!node)
		{
			ImGui::TextDisabled("Select a node to edit it.");
			ImGui::Spacing();
			ImGui::TextWrapped("Right-click the canvas or press Space to add a node; drag from a pin into empty space to add one connected to it. Delete removes the selection; Ctrl+C, Ctrl+V and Ctrl+D copy, paste and duplicate.");
			return;
		}

		const MaterialGraphNodeInfo* info = MaterialGraph::FindNodeInfo(node->Type);
		if (!info)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "Unknown node '%s'", node->Type.c_str());
			return;
		}

		ImGui::PushID(static_cast<int>(node->ID));
		ImGui::TextColored(CategoryColor(info->Category), "%s", std::string(info->Name).c_str());
		if (!info->Help.empty())
			ImGui::TextWrapped("%s", std::string(info->Help).c_str());
		if (node->Type != "Output" && !MaterialGraph::GetPins(*node).Outputs.empty())
		{
			if (m_PreviewNode == node->ID)
			{
				if (ImGui::SmallButton("Stop Preview"))
					StopPreview();
			}
			else if (ImGui::SmallButton("Preview This Node"))
			{
				PreviewNode(node->ID);
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Shows this node's first output on the preview shape (as light, over black). Recompiles once.");
		}
		ImGui::Separator();

		if (!info->Modes.empty())
		{
			const std::string current(info->Modes[std::clamp(node->Mode, 0, static_cast<int32_t>(info->Modes.size()) - 1)]);
			if (ImGui::BeginCombo("Mode", current.c_str()))
			{
				for (int32_t i = 0; i < static_cast<int32_t>(info->Modes.size()); i++)
				{
					if (ImGui::Selectable(std::string(info->Modes[i]).c_str(), node->Mode == i))
						node->Mode = i;
				}
				ImGui::EndCombo();
			}
		}

		if (node->Type == "Float")
			ImGui::DragFloat("Value", &node->Value.x, 0.01f);
		else if (node->Type == "Color")
			ImGui::ColorEdit4("Color", &node->Value.r);
		else if (node->Type == "Vector")
			ImGui::DragFloat3("Vector", &node->Value.x, 0.01f);

		if (UsesTexture(node->Type))
		{
			ImGuiEx::BeginPropertyGrid();
			AssetHandle texture = node->Texture;
			if (ImGuiEx::PropertyAssetReference<Texture2D>("Texture", texture, "", nullptr, {}, false))
				node->Texture = texture;
			ImGuiEx::EndPropertyGrid();
		}

		if (node->Type == "SubGraph")
		{
			ImGuiEx::BeginPropertyGrid();
			AssetHandle function = node->Graph;
			if (ImGuiEx::PropertyAssetReference<MaterialAsset>("Function", function, "A graph material whose graph this node runs.", nullptr, {}, false)
				&& function != material->Handle)
			{
				node->Graph = function;
				// The pins follow the new function: drop links to pins it does not have.
				const MaterialGraph::Pins pins = MaterialGraph::GetPins(*node);
				const uint32_t id = node->ID;
				std::erase_if(graph.Links, [&](const MaterialGraphLink& link)
				{
					return (link.ToNode == id && link.ToPin >= pins.Inputs.size()) || (link.FromNode == id && link.FromPin >= pins.Outputs.size());
				});
				node->InputValues.clear();
				for (const MaterialGraphPinInfo& pin : pins.Inputs)
					node->InputValues.push_back(pin.Default);
			}
			ImGuiEx::EndPropertyGrid();
		}

		if (node->Type == "FunctionInputFloat" || node->Type == "FunctionInputVector")
		{
			char name[64];
			std::snprintf(name, sizeof(name), "%s", node->Parameter.c_str());
			if (ImGui::InputText("Input Name", name, sizeof(name)))
				node->Parameter = name;
			if (node->Type == "FunctionInputFloat")
				ImGui::DragFloat("Default", &node->Value.x, 0.01f);
			else
				ImGui::DragFloat3("Default", &node->Value.x, 0.01f);
		}

		if (IsParameterNode(node->Type))
		{
			char name[64];
			std::snprintf(name, sizeof(name), "%s", node->Parameter.c_str());
			if (ImGui::InputTextWithHint("Parameter", "(not exposed)", name, sizeof(name)))
				node->Parameter = name;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Name it to edit it on the material and its instances without recompiling.");
		}

		if (node->Type == "ColorRamp")
		{
			ImGui::SeparatorText("Stops");
			for (size_t i = 0; i < node->Ramp.size(); i++)
			{
				ImGui::PushID(static_cast<int>(i));
				ImGui::SetNextItemWidth(60.0f);
				ImGui::DragFloat("##position", &node->Ramp[i].w, 0.005f, 0.0f, 1.0f);
				ImGui::SameLine();
				ImGui::ColorEdit3("##color", &node->Ramp[i].r, ImGuiColorEditFlags_NoInputs);
				ImGui::SameLine();
				if (node->Ramp.size() > 1 && ImGui::SmallButton("x"))
				{
					node->Ramp.erase(node->Ramp.begin() + static_cast<ptrdiff_t>(i));
					ImGui::PopID();
					break;
				}
				ImGui::PopID();
			}
			if (ImGui::SmallButton("Add Stop"))
				node->Ramp.push_back(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
		}

		bool header = false;
		const MaterialGraph::Pins pins = MaterialGraph::GetPins(*node);
		for (uint32_t pin = 0; pin < pins.Inputs.size() && pin < node->InputValues.size(); pin++)
		{
			if (graph.FindLinkInto(node->ID, pin) || !pins.Inputs[pin].DefaultExpression.empty())
				continue;
			if (!header)
			{
				ImGui::SeparatorText("Unconnected inputs");
				header = true;
			}
			EditPinValue(std::string(pins.Inputs[pin].Name).c_str(), node->InputValues[pin], pins.Inputs[pin].Type);
		}

		ImGui::PopID();
		(void)material;
	}

	void MaterialGraphCanvas::CopySelection(const MaterialGraph& graph)
	{
		std::vector<ed::NodeId> selected(static_cast<size_t>(std::max(ed::GetSelectedObjectCount(), 0)));
		const int count = selected.empty() ? 0 : ed::GetSelectedNodes(selected.data(), static_cast<int>(selected.size()));

		m_ClipboardNodes.clear();
		m_ClipboardLinks.clear();
		std::unordered_set<uint32_t> ids;
		for (int i = 0; i < count; i++)
		{
			const MaterialGraphNode* node = graph.FindNode(static_cast<uint32_t>(selected[i].Get()));
			if (node && node->Type != "Output")
			{
				m_ClipboardNodes.push_back(*node);
				ids.insert(node->ID);
			}
		}
		for (const MaterialGraphLink& link : graph.Links)
		{
			if (ids.contains(link.FromNode) && ids.contains(link.ToNode))
				m_ClipboardLinks.push_back(link);
		}
	}

	void MaterialGraphCanvas::Paste(MaterialGraph& graph, ImVec2 at)
	{
		if (m_ClipboardNodes.empty())
			return;

		glm::vec2 origin(FLT_MAX);
		for (const MaterialGraphNode& node : m_ClipboardNodes)
			origin = glm::min(origin, node.Position);
		// Pasted at the mouse; duplicated (at < 0) just beside the original.
		const glm::vec2 offset = at.x < 0.0f ? glm::vec2(40.0f) : glm::vec2(at.x, at.y) - origin;

		std::map<uint32_t, uint32_t> remap;
		ed::ClearSelection();
		for (MaterialGraphNode node : m_ClipboardNodes)
		{
			const uint32_t id = graph.NextID++;
			remap[node.ID] = id;
			node.ID = id;
			node.Position = glm::round(node.Position + offset); // whole units, like authored nodes: no round-trip drift
			graph.Nodes.push_back(node);
		}
		for (const MaterialGraphLink& link : m_ClipboardLinks)
			graph.Connect(remap[link.FromNode], link.FromPin, remap[link.ToNode], link.ToPin);
	}

	void MaterialGraphCanvas::TrackChanges(const Ref<MaterialAsset>& material, MaterialGraph& graph, const UndoPushFn& pushUndo)
	{
		// One undo step per finished edit: a slider drag or a node drag is one step.
		if (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left))
			return;

		std::string current = Snapshot(graph);
		if (current == m_Baseline)
			return;

		const std::string before = std::exchange(m_Baseline, current);
		Ref<MaterialAsset>(material)->MarkChanged(); // parameter defaults feed the material row

		// Only a change in the generated code recompiles the surface shaders; moving nodes does not.
		std::string code = graph.Compile().Body;
		for (const MaterialGraphParameter& parameter : graph.Compile().Parameters)
			code += std::format("|{}:{}:{}", parameter.Name, static_cast<int>(parameter.Kind), parameter.Slot);
		if (code != m_LastCode)
		{
			m_LastCode = std::move(code);
			MaterialGraphRegistry::MarkDirty();
		}

		if (!pushUndo)
			return;
		const AssetHandle handle = material->Handle;
		auto apply = [handle](const std::string& snapshot)
		{
			Ref<MaterialAsset> target = AssetManager::GetAsset<MaterialAsset>(handle);
			if (!target || !target->GetGraph())
				return;
			Restore(*target->GetGraph(), snapshot);
			target->MarkChanged();
			MaterialGraphRegistry::MarkDirty();
		};
		pushUndo("Edit Material Graph", [apply, before]() { apply(before); }, [apply, current]() { apply(current); });
	}

}
