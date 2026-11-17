// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AnimationGraphDocument.h"

#include "Lux/Animation/Animation.h"
#include "Lux/Asset/AssetImporter.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Editor/FontAwesome.h"
#include "Lux/Editor/SelectionManager.h"
#include "Lux/ImGui/Colors.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/ImGui/PanelControls.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Serialization/Yaml.h"

#include <imgui-node-editor/imgui_node_editor.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace ed = ax::NodeEditor;

namespace Lux
{
	namespace
	{
		constexpr uintptr_t k_AnyNode = 1;
		constexpr uint32_t k_Columns = 3;
		constexpr std::array k_ParameterTypes = { "Float", "Bool", "Trigger" };
		constexpr std::array k_Operators = { "Greater", "Less", "Equal", "Not equal", "True", "False", "Triggered" };

		enum class CanvasAction { Save, Revert, Add, Fit, Find, Minimap, Canvas, Details };

		ed::NodeId StateNode(uint32_t state)
		{
			return state == AnimationGraphTransition::AnyState ? ed::NodeId(k_AnyNode) : ed::NodeId(state + 2);
		}

		uint32_t StateIndex(uintptr_t value)
		{
			return value >= 2 ? static_cast<uint32_t>(value - 2) : AnimationGraph::StateNone;
		}

		ed::PinId StatePin(uint32_t state, bool output)
		{
			return ed::PinId((StateNode(state).Get() << 1) | static_cast<uintptr_t>(output));
		}

		const char* StateName(const AnimationGraph& graph, uint32_t state)
		{
			return state < graph.States.size() ? graph.States[state].Name.c_str() : "Any State";
		}

		std::string TransitionLabel(const AnimationGraph& graph, uint32_t index)
		{
			const auto& transition = graph.Transitions[index];
			return std::format("{}. {} → {}", index + 1, StateName(graph, transition.From), StateName(graph, transition.To));
		}

		AnimationConditionOp DefaultOperator(AnimationParameterType type)
		{
			if (type == AnimationParameterType::Trigger)
				return AnimationConditionOp::Triggered;
			return type == AnimationParameterType::Bool ? AnimationConditionOp::True : AnimationConditionOp::Greater;
		}

		void NodeTitle(std::string_view text, float width)
		{
			const ImVec2 start = ImGui::GetCursorScreenPos();
			const ImVec2 end(start.x + width, start.y + ImGui::GetTextLineHeight());
			ImGui::Dummy({ width, ImGui::GetTextLineHeight() });
			ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), start, end, end.x,
				text.data(), text.data() + text.size(), nullptr);
		}

		bool ShortcutsEnabled()
		{
			return ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
				!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
		}
	}

	GraphDocument::OpenFn AnimationGraphDocument::MakeFactory()
	{
		return [](AssetHandle handle, const std::filesystem::path& path) -> Scope<GraphDocument>
		{
			const auto graph = AssetManager::GetAsset<AnimationGraph>(handle);
			if (!graph || graph->States.empty())
				return {};
			return CreateScope<AnimationGraphDocument>(graph, path);
		};
	}

	AnimationGraphDocument::AnimationGraphDocument(const Ref<AnimationGraph>& graph, const std::filesystem::path& path)
		: GraphDocument(graph.As<Asset>()), m_Draft(Ref<AnimationGraph>::Create()),
		  m_Saved(graph->ToYAML()), m_Current(m_Saved), m_Baseline(m_Saved), m_SelectedState(graph->DefaultState)
	{
		m_Draft->Handle = Handle;
		RestoreDraft(m_Saved);
		RefreshLabel(path);
	}

	bool AnimationGraphDocument::Save()
	{
		RefreshDraft(true);
		if (!ValidateDraft())
		{
			SaveError = "Repair the graph before saving.";
			return false;
		}
		if (!IsDirty() && SaveError.empty())
			return true;
		if (!AssetImporter::TrySerialize(m_Draft.As<Asset>()))
		{
			SaveError = "Could not write the animation graph. Check that its folder is writable.";
			return false;
		}
		auto graph = Source.As<AnimationGraph>();
		if (!graph->FromYAML(m_Current))
		{
			SaveError = "The file was saved, but the editor could not refresh the graph. Reopen the asset.";
			LUX_CORE_ERROR_TAG("Editor", "Could not refresh saved animation graph '{}'", DisplayPath);
			return false;
		}
		m_Saved = m_Current;
		SaveError.clear();
		DirtyTab = false;
		return true;
	}

	void AnimationGraphDocument::Discard()
	{
		RestoreDraft(m_Saved);
		SaveError.clear();
		DirtyTab = false;
	}

	void AnimationGraphDocument::OnDeactivate()
	{
		RefreshDraft(true);
	}

	void AnimationGraphDocument::RestoreDraft(const std::string& snapshot)
	{
		if (!m_Draft->FromYAML(snapshot))
		{
			m_Diagnostic = "The animation graph snapshot could not be restored.";
			return;
		}
		m_Current = m_Baseline = snapshot;
		m_Diagnostic.clear();
		m_InvalidTransition = AnimationGraph::StateNone;
		m_EditPending = false;
		m_SyncLinkSelection = m_PlacedStates != 0;
		m_PlacedStates = 0;
		m_PendingSelection = AnimationGraph::StateNone;
		m_SelectedTransition = AnimationGraph::StateNone;
		if (m_SelectedState >= m_Draft->States.size())
			m_SelectedState = m_Draft->DefaultState;
	}

	bool AnimationGraphDocument::ValidateDraft()
	{
		m_Diagnostic.clear();
		if (!m_Draft->Validate(&m_Diagnostic, &m_InvalidTransition))
			return false;
		const uint32_t any = m_Draft->FindState("Any");
		if (any != AnimationGraph::StateNone && std::ranges::any_of(m_Draft->Transitions,
			[any](const auto& transition) { return transition.From == any; }))
		{
			m_Diagnostic = "Rename the state 'Any' before adding an outgoing transition. The graph file reserves 'Any' as a transition source.";
			return false;
		}
		return true;
	}

	void AnimationGraphDocument::RefreshDraft(bool finish)
	{
		if (!m_EditPending)
			return;
		m_Current = m_Draft->ToYAML();
		if (!ValidateDraft() || !finish)
			return;
		m_EditPending = false;
		if (m_Current == m_Baseline)
			return;
		const std::string before = std::exchange(m_Baseline, m_Current);
		PushUndo([this, before] { RestoreDraft(before); }, [this, after = m_Current] { RestoreDraft(after); });
	}

	void AnimationGraphDocument::SaveView(Yaml::Writer& out, bool navigation) const
	{
		out << Yaml::Key << "AnimationDetails" << Yaml::Value << m_NarrowDetails;
		out << Yaml::Key << "AnimationMinimap" << Yaml::Value << m_ShowMinimap;
		if (navigation)
			out << Yaml::Key << "AnimationCanvasView" << Yaml::Value << m_Canvas.GetView();
	}

	void AnimationGraphDocument::LoadView(const Yaml::Node& state, bool navigation)
	{
		m_NarrowDetails = state["AnimationDetails"].as<bool>(false);
		m_ShowMinimap = state["AnimationMinimap"].as<bool>(true);
		if (navigation)
		{
			m_Canvas.RestoreView(state["AnimationCanvasView"].Scalar());
			m_AnyPlaced = false;
			m_PlacedStates = 0;
		}
	}

	void AnimationGraphDocument::SetSceneContext(const Ref<Scene>& scene)
	{
		m_Scene = scene;
	}

	SkeletonComponent* AnimationGraphDocument::FindPreview(Scene& scene) const
	{
		if (scene.IsRunning())
			return nullptr;
		const auto graph = Source.As<AnimationGraph>();
		for (UUID id : SelectionManager::GetSelections(SelectionContext::Scene))
		{
			Entity entity = scene.TryGetEntityWithUUID(id);
			if (!entity || !entity.HasComponent<SkeletonComponent>())
				continue;
			auto& component = entity.GetComponent<SkeletonComponent>();
			const auto& state = component.State;
			if (component.Enabled && component.Graph == Handle && state.Graph.Initialized && !state.PlayMode &&
				state.GraphSource == graph && state.GraphRevision == graph->GetRevision())
				return &component;
		}
		return nullptr;
	}

	AssetHandle AnimationGraphDocument::Draw()
	{
		const float font = ImGui::GetFontSize();
		const auto split = ImGuiEx::PanelSplitWidths(ImGui::GetContentRegionAvail().x, 0.65f, font * 23.0f, font * 19.0f);
		using ImGuiEx::PanelAction;
		const PanelAction actions[] = {
			{static_cast<int>(CanvasAction::Save), "Save", LUX_ICON_FLOPPY_O, "Ctrl+S", IsDirty() || !SaveError.empty(), false, true},
			{static_cast<int>(CanvasAction::Revert), "Revert", LUX_ICON_UNDO, nullptr, IsDirty()},
			{static_cast<int>(CanvasAction::Add), "Add State", LUX_ICON_PLUS, nullptr, m_Draft->States.size() < AnimationGraph::MaxStates},
			{static_cast<int>(CanvasAction::Fit), "Fit", LUX_ICON_EXPAND},
			{static_cast<int>(CanvasAction::Find), "Find", LUX_ICON_SEARCH},
			{static_cast<int>(CanvasAction::Minimap), "Minimap", nullptr, nullptr, true, m_ShowMinimap, false, true},
			{static_cast<int>(CanvasAction::Canvas), "Canvas", LUX_ICON_SITEMAP, nullptr, !split.SideBySide, !split.SideBySide && !m_NarrowDetails},
			{static_cast<int>(CanvasAction::Details), "Details", LUX_ICON_SLIDERS, nullptr, !split.SideBySide, !split.SideBySide && m_NarrowDetails},
		};
		const auto action = static_cast<CanvasAction>(ImGuiEx::PanelActionBar("animation_actions", actions));
		switch (action)
		{
			case CanvasAction::Save:
				Save();
				break;
			case CanvasAction::Revert:
			{
				const auto previous = m_Current;
				const bool valid = m_Diagnostic.empty();
				Discard();
				if (valid && previous != m_Saved)
					PushUndo([this, previous] { RestoreDraft(previous); }, [this, saved = m_Saved] { RestoreDraft(saved); });
				break;
			}
			case CanvasAction::Add:
				AddState();
				m_NarrowDetails = false;
				break;
			case CanvasAction::Fit:
				m_Canvas.RequestFit();
				m_NarrowDetails = false;
				break;
			case CanvasAction::Minimap:
				m_ShowMinimap = !m_ShowMinimap;
				break;
			case CanvasAction::Canvas:
			case CanvasAction::Details:
				m_NarrowDetails = action == CanvasAction::Details;
				break;
			default:
				break;
		}

		const bool find = action == CanvasAction::Find;
		if (find || NodeCanvasCore::IsSearchOpen(NodeCanvasCore::Search::Find))
		{
			m_SearchChoices.clear();
			m_SearchChoices.reserve(m_Draft->States.size());
			for (uint32_t i = 0; i < m_Draft->States.size(); ++i)
				m_SearchChoices.push_back({ i + 1, m_Draft->States[i].Name });
			const uint32_t chosen = m_Canvas.DrawSearch(NodeCanvasCore::Search::Find, find, m_SearchChoices, m_SelectedState + 1);
			if (chosen)
			{
				m_PendingSelection = chosen - 1;
				m_NarrowDetails = false;
			}
			m_SearchChoices.clear();
		}
		if (!SaveError.empty())
			ImGuiEx::PanelMessage("Could not save", SaveError.c_str(), true);
		if (!m_Diagnostic.empty())
			ImGuiEx::PanelMessage("Animation graph", m_Diagnostic.c_str(), true);
		auto scene = m_Scene.Lock();
		auto* preview = scene ? FindPreview(*scene) : nullptr;
		const uint32_t active = preview && !IsDirty() ? preview->State.Graph.CurrentState : AnimationGraph::StateNone;
		if (preview)
			DrawPreview(*preview);

		if (split.SideBySide || !m_NarrowDetails)
		{
			if (ImGui::BeginChild("##graph", { split.SideBySide ? split.First : 0.0f, 0.0f },
				ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
				DrawCanvas(active);
			ImGui::EndChild();
		}
		if (split.SideBySide)
			ImGui::SameLine();
		if (split.SideBySide || m_NarrowDetails)
		{
			if (ImGui::BeginChild("##details", { 0.0f, 0.0f }, ImGuiChildFlags_Borders))
				DrawDetails();
			ImGui::EndChild();
		}
		RefreshDraft(!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left));
		return 0;
	}

	void AnimationGraphDocument::DrawCanvas(uint32_t activeState)
	{
		const ImVec2 minimum = ImGui::GetCursorScreenPos();
		const ImVec2 maximum = minimum + ImGui::GetContentRegionAvail();
		const float font = ImGui::GetFontSize();
		const bool shortcuts = ShortcutsEnabled();
		const NodeCanvasCore::ContextScope context(m_Canvas);
		m_Canvas.Begin("AnimationGraph", shortcuts);
		if (!m_AnyPlaced)
		{
			ed::SetNodePosition(k_AnyNode, { 0.0f, 0.0f });
			m_AnyPlaced = true;
		}
		ed::BeginNode(k_AnyNode);
		ed::BeginPin(StatePin(AnimationGraphTransition::AnyState, true), ed::PinKind::Output);
		ImGui::Text("Any State  %s", LUX_ICON_CIRCLE_O);
		ed::EndPin();
		ed::EndNode();

		for (uint32_t i = 0; i < m_Draft->States.size(); ++i)
		{
			const auto& state = m_Draft->States[i];
			const auto node = StateNode(i);
			if (i >= m_PlacedStates)
				ed::SetNodePosition(node, { font * 18.0f * static_cast<float>(i % k_Columns + 1),
					font * 7.0f * static_cast<float>(i / k_Columns) });
			if (i == activeState)
				ed::PushStyleColor(ed::StyleColor_NodeBorder, ImGui::ColorConvertU32ToFloat4(Colors::Theme::accent));
			ed::BeginNode(node);
			ed::BeginPin(StatePin(i, false), ed::PinKind::Input);
			ImGui::TextUnformatted(LUX_ICON_CIRCLE_O);
			ed::EndPin();
			ImGui::SameLine();
			NodeTitle(state.Name, font * 9.0f);
			ImGui::SameLine();
			ed::BeginPin(StatePin(i, true), ed::PinKind::Output);
			ImGui::TextUnformatted(LUX_ICON_CIRCLE_O);
			ed::EndPin();
			if (i == activeState || i == m_Draft->DefaultState)
				ImGui::TextDisabled("%s", i == activeState ? "Playing" : "Entry");
			ed::EndNode();
			if (i == activeState)
				ed::PopStyleColor();
		}
		m_PlacedStates = static_cast<uint32_t>(m_Draft->States.size());

		for (uint32_t i = 0; i < m_Draft->Transitions.size(); ++i)
		{
			const auto& transition = m_Draft->Transitions[i];
			ImU32 color = m_SelectedTransition == i ? Colors::Theme::accent : Colors::Theme::textCaption;
			if (m_InvalidTransition == i)
				color = Colors::Theme::textError;
			ed::Link(i + 1, StatePin(transition.From, true), StatePin(transition.To, false),
				ImGui::ColorConvertU32ToFloat4(color), 2.0f);
		}

		if (std::exchange(m_SyncLinkSelection, false))
		{
			if (m_SelectedTransition < m_Draft->Transitions.size())
				ed::SelectLink(m_SelectedTransition + 1);
			else
				ed::ClearSelection();
		}

		if (ed::BeginCreate())
		{
			ed::PinId start, end;
			if (ed::QueryNewLink(&start, &end) && start && end)
			{
				if (!(start.Get() & 1))
					std::swap(start, end);
				const uint32_t from = StateIndex(start.Get() >> 1);
				const uint32_t to = StateIndex(end.Get() >> 1);
				if ((start.Get() & 1) && !(end.Get() & 1) && to < m_Draft->States.size() &&
					m_Draft->Transitions.size() < AnimationGraph::MaxTransitions)
				{
					if (ed::AcceptNewItem())
					{
						m_SelectedTransition = static_cast<uint32_t>(m_Draft->Transitions.size());
						m_FocusTransitionInspector = true;
						m_SyncLinkSelection = true;
						m_Draft->Transitions.push_back({ from, to });
						MarkEdited();
					}
				}
				else
					ed::RejectNewItem();
			}
			ed::EndCreate();
		}

		// Node-editor IDs remain valid through End. Apply removals afterwards so index-based links
		// cannot be shifted while the library is still iterating its deletion candidates.
		std::vector<uint32_t> deleteLinks, deleteStates;
		if (ed::BeginDelete())
		{
			ed::LinkId link;
			while (ed::QueryDeletedLink(&link))
			{
				if (ed::AcceptDeletedItem())
					deleteLinks.push_back(static_cast<uint32_t>(link.Get() - 1));
			}
			ed::NodeId node;
			while (ed::QueryDeletedNode(&node))
			{
				const uint32_t state = StateIndex(node.Get());
				if (state < m_Draft->States.size() && deleteStates.size() + 1 < m_Draft->States.size())
				{
					if (ed::AcceptDeletedItem())
						deleteStates.push_back(state);
				}
				else
					ed::RejectDeletedItem();
			}
			ed::EndDelete();
		}

		if (m_PendingSelection < m_PlacedStates)
		{
			ed::SelectNode(StateNode(m_PendingSelection));
			ed::NavigateToSelection();
			m_PendingSelection = AnimationGraph::StateNone;
		}
		const auto clipboard = NodeCanvasCore::ReadClipboardAction();
		m_Canvas.FitIfRequested();
		ed::End();

		ed::NodeId selectedNode;
		if (ed::GetSelectedNodes(&selectedNode, 1))
			m_SelectedState = StateIndex(selectedNode.Get());
		ed::LinkId selectedLink;
		if (!m_SyncLinkSelection && ed::GetSelectedLinks(&selectedLink, 1))
		{
			const auto index = static_cast<uint32_t>(selectedLink.Get() - 1);
			if (m_SelectedTransition != index)
			{
				m_SelectedTransition = index;
				m_FocusTransitionInspector = true;
			}
		}
		if ((clipboard == NodeCanvasCore::ClipboardAction::Copy || clipboard == NodeCanvasCore::ClipboardAction::Duplicate) &&
			m_SelectedState < m_Draft->States.size())
			m_Clipboard = m_Draft->States[m_SelectedState];

		if (m_ShowMinimap)
		{
			m_Minimap.clear();
			m_Minimap.reserve(m_Draft->States.size() + 1);
			for (uint32_t i = 0; i < m_PlacedStates; ++i)
			{
				const auto node = StateNode(i);
				const ImVec2 position = ed::GetNodePosition(node);
				const ImVec2 size = ed::GetNodeSize(node);
				m_Minimap.push_back({ position, position + size, i == activeState ? Colors::Theme::accent : Colors::Theme::textDarker });
			}
			m_Canvas.DrawMinimap(minimum, maximum, m_Minimap);
		}
		std::ranges::sort(deleteLinks, std::greater<>());
		for (uint32_t index : deleteLinks)
			RemoveTransition(index);
		std::ranges::sort(deleteStates, std::greater<>());
		for (uint32_t index : deleteStates)
			RemoveState(index);
		if (!deleteLinks.empty() || !deleteStates.empty())
			ed::ClearSelection();
		if (m_Clipboard && (clipboard == NodeCanvasCore::ClipboardAction::Paste || clipboard == NodeCanvasCore::ClipboardAction::Duplicate))
			AddState(&*m_Clipboard);
	}

	void AnimationGraphDocument::AddState(const AnimationGraphState* source)
	{
		if (m_Draft->States.size() >= AnimationGraph::MaxStates)
			return;
		auto state = source ? *source : AnimationGraphState{};
		const std::string prefix = state.Name.empty() ? "State" : state.Name;
		for (uint32_t i = 1; ; ++i)
		{
			state.Name = std::format("{} {}", prefix, i);
			if (m_Draft->FindState(state.Name) == AnimationGraph::StateNone)
				break;
		}
		m_SelectedState = m_PendingSelection = static_cast<uint32_t>(m_Draft->States.size());
		m_Draft->States.push_back(std::move(state));
		MarkEdited();
	}

	void AnimationGraphDocument::RemoveState(uint32_t index)
	{
		if (index >= m_Draft->States.size() || m_Draft->States.size() <= 1)
			return;
		std::erase_if(m_Draft->Transitions, [index](const auto& transition) { return transition.From == index || transition.To == index; });
		for (auto& transition : m_Draft->Transitions)
		{
			if (transition.From != AnimationGraphTransition::AnyState && transition.From > index)
				--transition.From;
			if (transition.To > index)
				--transition.To;
		}
		if (m_Draft->DefaultState == index)
			m_Draft->DefaultState = 0;
		else if (m_Draft->DefaultState > index)
			--m_Draft->DefaultState;
		m_Draft->States.erase(m_Draft->States.begin() + index);
		m_SelectedState = m_Draft->DefaultState;
		m_SelectedTransition = AnimationGraph::StateNone;
		m_PlacedStates = 0;
		MarkEdited();
	}

	void AnimationGraphDocument::RemoveTransition(uint32_t index)
	{
		if (index >= m_Draft->Transitions.size())
			return;
		m_Draft->Transitions.erase(m_Draft->Transitions.begin() + index);
		m_SelectedTransition = AnimationGraph::StateNone;
		MarkEdited();
	}

	void AnimationGraphDocument::DrawDetails()
	{
		if (!ImGui::BeginTabBar("##graph_details"))
			return;

		if (ImGui::BeginTabItem("State"))
		{
			DrawState();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Transitions", nullptr,
			m_FocusTransitionInspector ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
		{
			m_FocusTransitionInspector = false;
			DrawTransition();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Parameters"))
		{
			DrawParameters();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	void AnimationGraphDocument::DrawState()
	{
		if (m_SelectedState >= m_Draft->States.size())
		{
			ImGui::TextWrapped("Select a state on the canvas to edit it.");
			return;
		}
		const ImGuiEx::ScopedID id(static_cast<int>(m_SelectedState));
		auto& state = m_Draft->States[m_SelectedState];
		ImGui::TextUnformatted("State name");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputText("##state_name", &state.Name))
			MarkEdited();
		ImGuiEx::BeginPropertyGrid();
		if (ImGuiEx::PropertyAssetReference<Animation>("Clip", state.Clip, "Clip for this state", nullptr, {}, false))
			MarkEdited();
		ImGuiEx::EndPropertyGrid();
		if (ImGui::DragFloat("Speed", &state.Speed, 0.05f))
			MarkEdited();
		if (ImGui::Checkbox("Loop", &state.Loop))
			MarkEdited();
		{
			const ImGuiEx::ScopedDisable disabled(m_Draft->DefaultState == m_SelectedState);
			if (ImGui::Button("Set as Entry"))
			{
				m_Draft->DefaultState = m_SelectedState;
				MarkEdited();
			}
		}
	}

	void AnimationGraphDocument::DrawTransition()
	{
		const std::string label = m_SelectedTransition < m_Draft->Transitions.size()
			? TransitionLabel(*m_Draft, m_SelectedTransition) : "Choose transition";
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##transition", label.c_str()))
		{
			for (uint32_t i = 0; i < m_Draft->Transitions.size(); ++i)
			{
				const ImGuiEx::ScopedID id(static_cast<int>(i));
				if (ImGuiEx::PanelListItem("##transition", TransitionLabel(*m_Draft, i), i == m_SelectedTransition))
				{
					m_SelectedTransition = i;
					m_SyncLinkSelection = true;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndCombo();
		}
		if (m_SelectedTransition >= m_Draft->Transitions.size())
			return;
		{
			const ImGuiEx::ScopedDisable disabled(m_SelectedTransition == 0);
			if (ImGui::SmallButton("Earlier"))
			{
				std::swap(m_Draft->Transitions[m_SelectedTransition], m_Draft->Transitions[m_SelectedTransition - 1]);
				--m_SelectedTransition;
				m_SyncLinkSelection = true;
				MarkEdited();
			}
		}
		ImGui::SameLine();
		{
			const ImGuiEx::ScopedDisable disabled(m_SelectedTransition + 1 >= m_Draft->Transitions.size());
			if (ImGui::SmallButton("Later"))
			{
				std::swap(m_Draft->Transitions[m_SelectedTransition], m_Draft->Transitions[m_SelectedTransition + 1]);
				++m_SelectedTransition;
				m_SyncLinkSelection = true;
				MarkEdited();
			}
		}
		ImGui::SetItemTooltip("Transitions are evaluated in the order shown.");
		auto& transition = m_Draft->Transitions[m_SelectedTransition];
		if (ImGui::DragFloat("Cross-fade (s)", &transition.Duration, 0.01f, 0.0f, 60.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
			MarkEdited();
		if (ImGui::DragFloat("Exit time", &transition.ExitTime, 0.01f, -1.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
			MarkEdited();
		ImGui::SetItemTooltip("Fraction of the current clip. Use a negative value for no exit time.");

		for (uint32_t i = 0; i < transition.Conditions.size(); ++i)
		{
			const ImGuiEx::ScopedID id(static_cast<int>(i));
			auto& condition = transition.Conditions[i];
			if (ImGui::BeginCombo("Parameter", condition.Parameter.c_str()))
			{
				for (const auto& entry : m_Draft->Parameters)
				{
					const ImGuiEx::ScopedID entryID(&entry);
					if (ImGuiEx::PanelListItem("##parameter", entry.Name, condition.Parameter == entry.Name))
					{
						condition.Parameter = entry.Name;
						condition.Op = DefaultOperator(entry.Type);
						MarkEdited();
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::EndCombo();
			}
			const uint32_t parameter = m_Draft->FindParameter(condition.Parameter);
			if (parameter < m_Draft->Parameters.size())
			{
				const auto type = m_Draft->Parameters[parameter].Type;
				const auto op = static_cast<uint32_t>(condition.Op);
				if (ImGui::BeginCombo("Operator", k_Operators[op]))
				{
					for (uint32_t j = 0; j < k_Operators.size(); ++j)
					{
						if ((type == AnimationParameterType::Trigger) != (j == static_cast<uint32_t>(AnimationConditionOp::Triggered)))
							continue;
						if (ImGui::Selectable(k_Operators[j], op == j))
						{
							condition.Op = static_cast<AnimationConditionOp>(j);
							MarkEdited();
						}
					}
					ImGui::EndCombo();
				}
			}
			if (condition.Op <= AnimationConditionOp::NotEqual && ImGui::DragFloat("Value", &condition.Value, 0.05f))
				MarkEdited();
			if (ImGui::SmallButton("Remove Condition"))
			{
				transition.Conditions.erase(transition.Conditions.begin() + i);
				MarkEdited();
				break;
			}
			ImGui::Separator();
		}
		const ImGuiEx::ScopedDisable disabled(m_Draft->Parameters.empty() || transition.Conditions.size() >= AnimationGraph::MaxConditions);
		if (ImGui::Button("Add Condition"))
		{
			const auto& parameter = m_Draft->Parameters.front();
			transition.Conditions.push_back({ parameter.Name, DefaultOperator(parameter.Type) });
			MarkEdited();
		}
	}

	void AnimationGraphDocument::DrawParameters()
	{
		for (uint32_t i = 0; i < m_Draft->Parameters.size(); ++i)
		{
			const ImGuiEx::ScopedID id(static_cast<int>(i));
			auto& parameter = m_Draft->Parameters[i];
			std::string name = parameter.Name;
			if (ImGui::InputText("Name", &name, ImGuiInputTextFlags_EnterReturnsTrue) && name != parameter.Name)
			{
				if (name.empty() || m_Draft->FindParameter(name) != AnimationGraph::ParameterNone)
				{
					m_Diagnostic = "Parameter names must be nonempty and unique.";
				}
				else
				{
					for (auto& transition : m_Draft->Transitions)
						for (auto& condition : transition.Conditions)
							if (condition.Parameter == parameter.Name)
								condition.Parameter = name;
					parameter.Name = std::move(name);
					MarkEdited();
				}
			}
			ImGui::SetItemTooltip("Press Enter to rename this parameter and update its conditions.");
			int type = static_cast<int>(parameter.Type);
			if (ImGui::Combo("Type", &type, k_ParameterTypes.data(), static_cast<int>(k_ParameterTypes.size())))
			{
				parameter.Type = static_cast<AnimationParameterType>(type);
				for (auto& transition : m_Draft->Transitions)
					for (auto& condition : transition.Conditions)
						if (condition.Parameter == parameter.Name)
							condition.Op = DefaultOperator(parameter.Type);
				MarkEdited();
			}
			if (parameter.Type == AnimationParameterType::Float && ImGui::DragFloat("Default", &parameter.Default, 0.05f))
				MarkEdited();
			if (parameter.Type == AnimationParameterType::Bool)
			{
				bool value = parameter.Default != 0.0f;
				if (ImGui::Checkbox("Default", &value))
				{
					parameter.Default = value ? 1.0f : 0.0f;
					MarkEdited();
				}
			}
			const bool used = std::ranges::any_of(m_Draft->Transitions, [&](const auto& transition) {
				return std::ranges::any_of(transition.Conditions, [&](const auto& condition) { return condition.Parameter == parameter.Name; });
			});
			{
				const ImGuiEx::ScopedDisable disabled(used);
				if (ImGui::SmallButton("Remove Parameter"))
				{
					m_Draft->Parameters.erase(m_Draft->Parameters.begin() + i);
					MarkEdited();
					break;
				}
			}
			if (used && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Remove the conditions that use this parameter first.");
			ImGui::Separator();
		}
		const ImGuiEx::ScopedDisable disabled(m_Draft->Parameters.size() >= AnimationGraph::MaxParameters);
		if (ImGui::Button("Add Parameter"))
		{
			std::string name;
			for (uint32_t i = 1; ; ++i)
			{
				name = std::format("Parameter {}", i);
				if (m_Draft->FindParameter(name) == AnimationGraph::ParameterNone)
					break;
			}
			m_Draft->Parameters.push_back({ std::move(name) });
			MarkEdited();
		}
	}

	void AnimationGraphDocument::DrawPreview(SkeletonComponent& component)
	{
		if (!ImGui::CollapsingHeader("Preview Selected Skeleton"))
			return;

		const auto graph = Source.As<AnimationGraph>();
		auto& state = component.State;
		if (state.Graph.CurrentState >= graph->States.size() || state.Graph.Values.size() != graph->Parameters.size())
			return;

		ImGui::TextWrapped("%s%s", graph->States[state.Graph.CurrentState].Name.c_str(), IsDirty() ? " (saved graph)" : "");
		if (ImGui::Button(state.Playing ? "Pause###transport" : "Play###transport"))
		{
			state.Playing = !state.Playing;
			state.PreviewOverride = true;
		}
		const auto clip = state.GraphClip ? AssetManager::GetAsset<Animation>(state.GraphClip) : nullptr;
		if (clip && clip->GetDuration() > 0.0f)
		{
			float time = std::clamp(state.Time, 0.0f, clip->GetDuration());
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::SliderFloat("##preview_time", &time, 0.0f, clip->GetDuration(), "%.2f s", ImGuiSliderFlags_AlwaysClamp))
			{
				state.Time = time;
				state.Playing = false;
				state.PreviewOverride = true;
				state.FadeFromClip = 0;
			}
		}
		for (uint32_t i = 0; i < graph->Parameters.size(); ++i)
		{
			const ImGuiEx::ScopedID id(static_cast<int>(i));
			const auto& parameter = graph->Parameters[i];
			ImGui::TextWrapped("%s", parameter.Name.c_str());
			float& value = state.Graph.Values[i];
			switch (parameter.Type)
			{
				case AnimationParameterType::Float:
					ImGui::DragFloat("##preview_float", &value, 0.01f);
					break;
				case AnimationParameterType::Bool:
				{
					bool checked = value != 0.0f;
					if (ImGui::Checkbox("##preview_bool", &checked))
						value = checked ? 1.0f : 0.0f;
					break;
				}
				case AnimationParameterType::Trigger:
					if (ImGui::Button("Trigger"))
						value = 1.0f;
					break;
			}
		}
	}
}
