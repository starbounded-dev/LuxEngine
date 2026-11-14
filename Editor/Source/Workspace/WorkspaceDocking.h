// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Lux
{

	// ImGui's central dock keeps its neighbours at fixed pixel sizes. Remember the user's
	// split ratios independently so a temporary small host cannot overwrite their intent.
	class WorkspaceDocking
	{
	public:
		static bool ValidateIni(const std::string& text)
		{
			struct Node
			{
				unsigned Parent = 0;
				unsigned Children = 0;
				bool Split = false;
			};
			std::unordered_map<unsigned, Node> nodes;
			std::istringstream lines(text);
			std::string line;
			bool docking = false;
			while (std::getline(lines, line))
			{
				if (!line.empty() && line[0] == '[')
					docking = line.starts_with("[Docking][Data]");
				if (!docking || line.find("ID=") == std::string::npos)
					continue;
				unsigned id = 0, parent = 0;
				if (std::sscanf(line.c_str() + line.find("ID="), "ID=0x%X", &id) != 1 || !id ||
					nodes.contains(id) || nodes.size() >= 1024)
					return false;
				if (const auto offset = line.find("Parent="); offset != std::string::npos)
				{
					if (std::sscanf(line.c_str() + offset, "Parent=0x%X", &parent) != 1 ||
						!nodes.contains(parent) || ++nodes.at(parent).Children > 2)
						return false;
				}
				nodes[id] = {parent, 0, line.find("Split=") != std::string::npos};
			}
			for (const auto& [id, node] : nodes)
				if (node.Children != (node.Split ? 2u : 0u))
					return false;
			return !nodes.empty();
		}
		struct SplitRatio
		{
			ImGuiID Node;
			ImGuiID First;
			ImGuiID Second;
			float Ratio;
		};
		std::vector<SplitRatio> CaptureRatios() const
		{
			std::vector<SplitRatio> result;
			for (const auto& [id, split] : m_Ratios)
				result.push_back({id, split.First, split.Second, split.Ratio});
			std::ranges::sort(result, {}, &SplitRatio::Node);
			return result;
		}
		void RestoreRatios(const std::vector<SplitRatio>& ratios)
		{
			Reset();
			for (const auto& split : ratios)
			{
				if (std::isfinite(split.Ratio) && split.Ratio > 0.0f && split.Ratio < 1.0f)
					m_Ratios[split.Node] = {split.First, split.Second, split.Ratio};
			}
		}
		void Reset()
		{
			m_Ratios.clear();
			m_HostSize = {};
		}

		// Main thread, immediately before DockSpace. Does not rebuild nodes or move windows.
		void Prepare(ImGuiID dockspace, ImVec2 size)
		{
			ImGuiDockNode* root = ImGui::DockBuilderGetNode(dockspace);
			if (!root || size.x < 2.0f || size.y < 2.0f)
				return;

			const bool resized = size.x != m_HostSize.x || size.y != m_HostSize.y;
			const bool userEdit = !resized && GImGui->ActiveId != 0;
			Remember(root, userEdit);
			if (resized)
				Resize(root, size);
			m_HostSize = size;
		}

	private:
		struct Split
		{
			ImGuiID First = 0;
			ImGuiID Second = 0;
			float Ratio = 0.5f;
		};

		void Remember(ImGuiDockNode* node, bool userEdit)
		{
			if (!node->IsSplitNode())
				return;
			const auto* first = node->ChildNodes[0];
			const auto* second = node->ChildNodes[1];
			auto [entry, inserted] = m_Ratios.try_emplace(node->ID);
			Split& split = entry->second;
			// Only a drag of this node's splitter changes its intended ratio. Clicking
			// an unrelated widget in a constrained host must not commit the clamp.
			const int nodeID = static_cast<int>(node->ID);
			const ImGuiID seed = node->HostWindow ? ImHashData(&nodeID, sizeof(nodeID), node->HostWindow->ID) : 0;
			const bool splitterActive = userEdit && seed && GImGui->ActiveId == ImHashStr("##Splitter", 0, seed);
			if (inserted || splitterActive || split.First != first->ID || split.Second != second->ID)
			{
				const int axis = node->SplitAxis;
				const float total = first->SizeRef[axis] + second->SizeRef[axis];
				split = {first->ID, second->ID,
						 total > 0.0f ? std::clamp(first->SizeRef[axis] / total, 0.01f, 0.99f) : 0.5f};
			}
			Remember(node->ChildNodes[0], userEdit);
			Remember(node->ChildNodes[1], userEdit);
		}

		void Resize(ImGuiDockNode* node, ImVec2 size)
		{
			if (!node->IsSplitNode())
				return;
			const int axis = node->SplitAxis;
			const float available = std::max(2.0f, size[axis] - ImGui::GetStyle().DockingSeparatorSize);
			const float minimum = std::min(available * 0.5f, ImGui::GetStyle().WindowMinSize[axis]);
			ImVec2 firstSize = size;
			ImVec2 secondSize = size;
			firstSize[axis] =
				std::clamp(std::floor(available * m_Ratios.at(node->ID).Ratio), minimum, available - minimum);
			secondSize[axis] = available - firstSize[axis];
			// One lock selects the explicit-size path even when the other child is central.
			// This vendored version's two-lock path initializes both sizes from the parent.
			node->ChildNodes[0]->Size = node->ChildNodes[0]->SizeRef = firstSize;
			node->ChildNodes[1]->Size = node->ChildNodes[1]->SizeRef = secondSize;
			node->ChildNodes[0]->WantLockSizeOnce = true;
			node->ChildNodes[1]->WantLockSizeOnce = false;
			Resize(node->ChildNodes[0], firstSize);
			Resize(node->ChildNodes[1], secondSize);
		}

		std::unordered_map<ImGuiID, Split> m_Ratios;
		ImVec2 m_HostSize{};
	};

} // namespace Lux
