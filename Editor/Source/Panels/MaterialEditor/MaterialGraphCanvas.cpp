// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "MaterialGraphCanvas.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Renderer/MaterialGraph/MaterialGraphRegistry.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Serialization/Yaml.h"
#include "Lux/Utilities/StringUtils.h"

#include <imgui-node-editor/imgui_node_editor.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <map>

namespace ed = ax::NodeEditor;

namespace Lux {

	namespace {

		// Bumped by undo and redo, which replace a graph's content behind the canvas's back: the
		// canvas then places every node again from the graph.
		std::atomic<uint64_t> s_RestoreGeneration{ 0 };

		constexpr uint64_t k_OutputPinBit = 0x8000;

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

		bool ContainsNoCase(std::string_view text, std::string_view query)
		{
			if (query.empty())
				return true;
			return Utils::String::ToLowerCopy(std::string(text)).find(Utils::String::ToLowerCopy(std::string(query))) != std::string::npos;
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

	MaterialGraphCanvas::MaterialGraphCanvas()
	{
		ed::Config config;
		config.SettingsFile = nullptr; // node positions live in the graph
		m_Context = ed::CreateEditor(&config);
	}

	MaterialGraphCanvas::~MaterialGraphCanvas()
	{
		ed::DestroyEditor(m_Context);
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

	void MaterialGraphCanvas::Draw(const Ref<MaterialAsset>& material, const UndoPushFn& pushUndo)
	{
		Ref<MaterialGraph> graph = material ? material->GetGraph() : nullptr;
		if (!graph)
			return;

		static uint64_t s_SeenGeneration = 0;
		const uint64_t generation = s_RestoreGeneration.load(std::memory_order_relaxed);
		if (material->Handle != m_Material || generation != s_SeenGeneration)
		{
			const bool switched = material->Handle != m_Material;
			m_Material = material->Handle;
			s_SeenGeneration = generation;
			m_Placed.clear();
			m_CommentNodeSizes.clear();
			m_SelectedNode = 0;
			m_Baseline = Snapshot(*graph);
			if (switched)
			{
				m_LastCode = graph->Compile().Body;
				m_NavigateToContent = true;
			}
		}

		DrawToolbar(material, *graph);

		const float inspectorWidth = 280.0f;
		const float canvasWidth = std::max(ImGui::GetContentRegionAvail().x - inspectorWidth - ImGui::GetStyle().ItemSpacing.x, 100.0f);
		if (ImGui::BeginChild("##material_graph_canvas", ImVec2(canvasWidth, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
			DrawCanvas(material, *graph);
		ImGui::EndChild();

		ImGui::SameLine();
		if (ImGui::BeginChild("##material_graph_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
			DrawInspector(material, *graph);
		ImGui::EndChild();

		TrackChanges(material, *graph, pushUndo);
	}

	void MaterialGraphCanvas::DrawToolbar(const Ref<MaterialAsset>& material, MaterialGraph& graph)
	{
		if (ImGui::Button("Add Node"))
		{
			m_OpenPalette = true;
			m_PaletteFromNode = 0;
			m_PalettePosition = ImVec2(0.0f, 0.0f);
		}
		ImGui::SameLine();
		if (ImGui::Button("Fit"))
			m_NavigateToContent = true;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::InputTextWithHint("##material_graph_find", "Find in graph...", m_FindSearch, sizeof(m_FindSearch), ImGuiInputTextFlags_EnterReturnsTrue) && m_FindSearch[0])
		{
			for (const MaterialGraphNode& node : graph.Nodes)
			{
				const MaterialGraphNodeInfo* info = MaterialGraph::FindNodeInfo(node.Type);
				if ((info && ContainsNoCase(info->Name, m_FindSearch)) || ContainsNoCase(node.Parameter, m_FindSearch))
				{
					m_PendingSelect = node.ID;
					m_NavigateToSelection = true;
					break;
				}
			}
		}

		// Stats: what the graph costs.
		size_t textures = 0;
		size_t parameters = 0;
		for (const MaterialGraphNode& node : graph.Nodes)
		{
			textures += UsesTexture(node.Type) ? (node.Type == "Triplanar" ? 3 : 1) : 0;
			parameters += node.Parameter.empty() ? 0 : 1;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%zu nodes, %zu links, %zu texture samples, %zu parameters", graph.Nodes.size(), graph.Links.size(), textures, parameters);

		for (const MaterialGraph::Error& error : MaterialGraphRegistry::GetErrors(material->Handle))
		{
			const MaterialGraphNode* node = graph.FindNode(error.Node);
			const MaterialGraphNodeInfo* info = node ? MaterialGraph::FindNodeInfo(node->Type) : nullptr;
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s%s", info ? std::format("{}: ", info->Name).c_str() : "", error.Message.c_str());
		}
	}

	void MaterialGraphCanvas::DrawCanvas(const Ref<MaterialAsset>& material, MaterialGraph& graph)
	{
		const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
		const ImVec2 canvasMax = ImVec2(canvasMin.x + ImGui::GetContentRegionAvail().x, canvasMin.y + ImGui::GetContentRegionAvail().y);

		std::unordered_set<uint32_t> errorNodes;
		for (const MaterialGraph::Error& error : MaterialGraphRegistry::GetErrors(material->Handle))
			errorNodes.insert(error.Node);

		ed::SetCurrentEditor(m_Context);
		ed::Begin("MaterialGraph");

		// Comments first, so nodes draw over them.
		for (const MaterialGraphComment& comment : graph.Comments)
		{
			if (m_Placed.insert(comment.ID).second)
				ed::SetNodePosition(comment.ID, ImVec2(comment.Position.x, comment.Position.y));
			const ImVec4 color(comment.Color.r, comment.Color.g, comment.Color.b, comment.Color.a);
			ed::PushStyleColor(ed::StyleColor_NodeBg, color);
			ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(color.x, color.y, color.z, 1.0f));
			if (m_ResizeComment == comment.ID)
			{
				ed::SetGroupSize(comment.ID, ImVec2(comment.Size.x, comment.Size.y));
				m_CommentNodeSizes.erase(comment.ID); // the new node size is not a drag-resize
				m_ResizeComment = 0;
			}
			ed::BeginNode(comment.ID);
			ImGui::PushID(static_cast<int>(comment.ID));
			ImGui::TextUnformatted(comment.Text.empty() ? "Comment" : comment.Text.c_str());
			ed::Group(ImVec2(comment.Size.x, comment.Size.y));
			ImGui::PopID();
			ed::EndNode();
			ed::PopStyleColor(2);
		}

		for (const MaterialGraphNode& node : graph.Nodes)
		{
			const MaterialGraphNodeInfo* info = MaterialGraph::FindNodeInfo(node.Type);
			if (m_Placed.insert(node.ID).second)
				ed::SetNodePosition(node.ID, ImVec2(node.Position.x, node.Position.y));

			const bool hasError = errorNodes.contains(node.ID);
			if (hasError)
				ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(1.0f, 0.3f, 0.25f, 1.0f));

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
					ImGui::Text("%s %s", connected ? "(o)" : "( )", std::string(pins.Inputs[pin].Name).c_str());
					ed::EndPin();
				}
				ImGui::EndGroup();
				if (!pins.Inputs.empty() && !pins.Outputs.empty())
					ImGui::SameLine(0.0f, 24.0f);
				ImGui::BeginGroup();
				for (uint32_t pin = 0; pin < pins.Outputs.size(); pin++)
				{
					ed::BeginPin(OutputPin(node.ID, pin), ed::PinKind::Output);
					ImGui::Text("%s (o)", std::string(pins.Outputs[pin].Name).c_str());
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
			ed::Link(link.ID, OutputPin(link.FromNode, link.FromPin), InputPin(link.ToNode, link.ToPin), ImVec4(0.8f, 0.8f, 0.85f, 1.0f), 2.0f);

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
		}
		ed::EndCreate();

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
		}
		ed::EndDelete();

		if (ed::BeginShortcut())
		{
			if (ed::AcceptCopy())
				CopySelection(graph);
			if (ed::AcceptPaste())
				Paste(graph, ImGui::GetMousePos()); // canvas space while the editor is not suspended
			if (ed::AcceptDuplicate())
			{
				CopySelection(graph);
				Paste(graph, ImVec2(-1.0f, -1.0f));
			}
		}
		ed::EndShortcut();

		if (m_PendingSelect && graph.FindNode(m_PendingSelect))
		{
			ed::SelectNode(m_PendingSelect);
			if (m_NavigateToSelection)
				ed::NavigateToSelection();
		}
		m_PendingSelect = 0;
		m_NavigateToSelection = false;
		if (m_NavigateToContent && !graph.Nodes.empty())
		{
			ed::NavigateToContent(0.0f);
			m_NavigateToContent = false;
		}

		// Popups and the context menu work in ImGui's screen space, so the editor is suspended.
		ed::Suspend();
		if (ed::ShowBackgroundContextMenu() || (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space, false)))
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
			if (position.x != FLT_MAX && (position.x != node.Position.x || position.y != node.Position.y))
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
		DrawMinimap(graph, canvasMin, canvasMax);
		ed::SetCurrentEditor(nullptr);
	}

	void MaterialGraphCanvas::DrawMinimap(MaterialGraph& graph, const ImVec2& canvasMin, const ImVec2& canvasMax)
	{
		if (graph.Nodes.empty())
			return;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 mapMax(canvasMax.x - 8.0f, canvasMax.y - 8.0f);
		const ImVec2 mapMin(mapMax.x - 180.0f, mapMax.y - 120.0f);
		if (mapMin.x < canvasMin.x || mapMin.y < canvasMin.y)
			return;

		drawList->AddRectFilled(mapMin, mapMax, IM_COL32(20, 20, 24, 200), 4.0f);
		glm::vec2 boundsMin(FLT_MAX), boundsMax(-FLT_MAX);
		for (const MaterialGraphNode& node : graph.Nodes)
		{
			boundsMin = glm::min(boundsMin, node.Position);
			boundsMax = glm::max(boundsMax, node.Position + glm::vec2(160.0f, 80.0f));
		}
		// What the canvas shows widens the box, so the view rectangle always fits.
		const ImVec2 viewMin = ed::ScreenToCanvas(canvasMin);
		const ImVec2 viewMax = ed::ScreenToCanvas(canvasMax);
		boundsMin = glm::min(boundsMin, glm::vec2(viewMin.x, viewMin.y));
		boundsMax = glm::max(boundsMax, glm::vec2(viewMax.x, viewMax.y));

		const glm::vec2 extent = glm::max(boundsMax - boundsMin, glm::vec2(1.0f));
		const float scale = std::min((mapMax.x - mapMin.x) / extent.x, (mapMax.y - mapMin.y) / extent.y);
		auto map = [&](glm::vec2 p) { return ImVec2(mapMin.x + (p.x - boundsMin.x) * scale, mapMin.y + (p.y - boundsMin.y) * scale); };
		for (const MaterialGraphComment& comment : graph.Comments)
			drawList->AddRectFilled(map(comment.Position), map(comment.Position + comment.Size), IM_COL32(90, 90, 90, 120));
		for (const MaterialGraphNode& node : graph.Nodes)
		{
			const MaterialGraphNodeInfo* info = MaterialGraph::FindNodeInfo(node.Type);
			const ImVec4 color = info ? CategoryColor(info->Category) : ImVec4(1.0f, 0.3f, 0.25f, 1.0f);
			drawList->AddRectFilled(map(node.Position), map(node.Position + glm::vec2(160.0f, 80.0f)), ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, node.ID == m_SelectedNode ? 1.0f : 0.6f)));
		}
		drawList->AddRect(map(glm::vec2(viewMin.x, viewMin.y)), map(glm::vec2(viewMax.x, viewMax.y)), IM_COL32(255, 255, 255, 160));
	}

	void MaterialGraphCanvas::DrawPalette(MaterialGraph& graph)
	{
		if (m_OpenPalette)
		{
			ImGui::OpenPopup("Add Node##material_graph");
			m_PaletteSearch[0] = '\0';
			m_OpenPalette = false;
		}

		ImGui::SetNextWindowSizeConstraints(ImVec2(240.0f, 0.0f), ImVec2(320.0f, 420.0f));
		if (!ImGui::BeginPopup("Add Node##material_graph"))
			return;

		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		const bool enter = ImGui::InputTextWithHint("##palette_search", "Search nodes...", m_PaletteSearch, sizeof(m_PaletteSearch), ImGuiInputTextFlags_EnterReturnsTrue);

		auto place = [&](std::string_view type)
		{
			glm::vec2 position(m_PalettePosition.x, m_PalettePosition.y);
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

		bool placed = false;
		std::string_view category;
		for (const MaterialGraphNodeInfo& info : MaterialGraph::GetNodeLibrary())
		{
			if (info.Type == "Output")
				continue;
			if (!ContainsNoCase(info.Name, m_PaletteSearch) && !ContainsNoCase(info.Category, m_PaletteSearch))
				continue;
			if (info.Category != category)
			{
				category = info.Category;
				ImGui::SeparatorText(std::string(category).c_str());
			}
			if (ImGui::Selectable(std::string(info.Name).c_str()) || (enter && !placed))
			{
				place(info.Type);
				placed = true;
				ImGui::CloseCurrentPopup();
			}
			if (!info.Help.empty() && ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", std::string(info.Help).c_str());
		}
		if (ContainsNoCase("Comment", m_PaletteSearch))
		{
			ImGui::SeparatorText("Organize");
			if (ImGui::Selectable("Comment") || (enter && !placed))
			{
				place("Comment");
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndPopup();
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
		ed::SetCurrentEditor(m_Context);
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
			node.Position += offset;
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
