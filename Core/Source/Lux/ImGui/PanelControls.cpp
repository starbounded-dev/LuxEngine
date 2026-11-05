// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "PanelControls.h"

#include "Colors.h"
#include "ImGuiUtilities.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>

namespace Lux::ImGuiEx
{
	namespace
	{
		struct ActionLayout
		{
			int PrimaryCount = 0;
			int SecondaryCount = 0;
			bool CompactPrimary = false;
			bool Overflow = false;
		};

		float ActionWidth(const PanelAction& action, bool compact)
		{
			const auto& style = ImGui::GetStyle();
			const bool icon = action.Icon && action.Icon[0];
			float width = icon ? ImGui::CalcTextSize(action.Icon).x : 0.0f;
			if (!compact || !icon)
			{
				width += ImGui::CalcTextSize(action.Label).x;
				if (icon)
					width += style.ItemInnerSpacing.x;
			}
			return std::max(ImGui::GetFrameHeight(), width + style.FramePadding.x * 2.0f);
		}

		ActionLayout FitActions(std::span<const PanelAction> actions, float available, float moreWidth)
		{
			ActionLayout layout;
			const float gap = ImGui::GetStyle().ItemSpacing.x;
			float allWidth = 0.0f, primaryWidth = 0.0f;
			int count = 0, primaryCount = 0;
			for (const auto& action : actions)
			{
				layout.Overflow |= action.MenuOnly;
				if (action.MenuOnly)
					continue;
				allWidth += ActionWidth(action, false) + (count++ ? gap : 0.0f);
				if (action.Primary)
					primaryWidth += ActionWidth(action, false) + (primaryCount++ ? gap : 0.0f);
			}
			if (allWidth + (layout.Overflow ? moreWidth + (count ? gap : 0.0f) : 0.0f) <= available)
			{
				layout.PrimaryCount = primaryCount;
				layout.SecondaryCount = count - primaryCount;
				return layout;
			}
			layout.Overflow = true;
			float budget = std::max(0.0f, available - moreWidth - gap);
			layout.CompactPrimary = primaryWidth > budget;
			for (bool primary : {true, false})
			{
				for (const auto& action : actions)
				{
					if (action.MenuOnly || action.Primary != primary)
						continue;
					const float width = ActionWidth(action, primary && layout.CompactPrimary);
					if (width > budget)
						break;
					budget -= width + gap;
					++(primary ? layout.PrimaryCount : layout.SecondaryCount);
				}
			}
			return layout;
		}

		bool ActionButton(const PanelAction& action, bool compact, float width)
		{
			ScopedID id(action.ID);
			ScopedDisable disabled(!action.Enabled);
			ScopedColourStack colors(ImGuiCol_Button,
									 action.Selected ? Colors::Theme::selectionMuted
													 : Colors::Theme::backgroundDark,
									 ImGuiCol_ButtonHovered, Colors::Theme::backgroundHover,
									 ImGuiCol_ButtonActive, Colors::Theme::accentTabActive, ImGuiCol_Text,
									 action.Selected ? Colors::Theme::accent : Colors::Theme::textBrighter);
			const bool pressed = ImGui::Button("##action", {width, ImGui::GetFrameHeight()});
			const ImVec2 minimum = ImGui::GetItemRectMin();
			const ImVec2 maximum = ImGui::GetItemRectMax();
			const auto& style = ImGui::GetStyle();
			ImVec2 position(minimum.x + style.FramePadding.x, minimum.y + style.FramePadding.y);
			ImDrawList* draw = ImGui::GetWindowDrawList();
			draw->PushClipRect(minimum, maximum, true);
			const bool icon = action.Icon && action.Icon[0];
			if (icon)
			{
				draw->AddText(position, ImGui::GetColorU32(ImGuiCol_Text), action.Icon);
				position.x += ImGui::CalcTextSize(action.Icon).x + style.ItemInnerSpacing.x;
			}
			if (!compact || !icon)
				draw->AddText(position, ImGui::GetColorU32(ImGuiCol_Text), action.Label);
			draw->PopClipRect();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				if (action.Shortcut && action.Shortcut[0])
					ImGui::SetTooltip("%s  (%s)%s", action.Label, action.Shortcut,
									  action.Enabled ? "" : "\nUnavailable in the current view");
				else
					ImGui::SetTooltip("%s%s", action.Label,
									  action.Enabled ? "" : "\nUnavailable in the current view");
			}
			return pressed;
		}
	} // namespace

	int PanelActionBar(const char* id, std::span<const PanelAction> actions)
	{
		int activated = -1;
		const auto& style = ImGui::GetStyle();
		const float padding = style.FramePadding.y;
		ScopedStyle childPadding(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
		ScopedColour background(ImGuiCol_ChildBg, Colors::Theme::backgroundDark);
		const bool visible = ImGui::BeginChild(
			id, {0.0f, ImGui::GetFrameHeight() + padding * 2.0f}, ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		if (visible)
		{
			const float available = ImGui::GetContentRegionAvail().x;
			const float moreWidth =
				std::min(available, ImGui::CalcTextSize("More").x + style.FramePadding.x * 2.0f);
			const ActionLayout layout = FitActions(actions, available, moreWidth);
			int primary = 0, secondary = 0;
			bool first = true;
			for (const auto& action : actions)
			{
				if (action.MenuOnly)
					continue;
				const bool shown =
					action.Primary ? primary++ < layout.PrimaryCount : secondary++ < layout.SecondaryCount;
				if (!shown)
					continue;
				if (!first)
					ImGui::SameLine();
				first = false;
				const bool compact = action.Primary && layout.CompactPrimary;
				if (ActionButton(action, compact, ActionWidth(action, compact)))
					activated = action.ID;
			}
			// Keep the popup alive across a resize, even when all its actions now fit in the row.
			if (layout.Overflow && moreWidth > 0.0f)
			{
				const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - moreWidth;
				if (!first)
					ImGui::SameLine(right, 0.0f);
				else
					ImGui::SetCursorPosX(right);
				if (ImGui::Button("More###overflow", {moreWidth, ImGui::GetFrameHeight()}))
					ImGui::OpenPopup("Panel actions");
				ImGui::SetItemTooltip("More actions");
			}
			if (ImGui::BeginPopup("Panel actions"))
			{
				primary = secondary = 0;
				for (const auto& action : actions)
				{
					const bool shown =
						!action.MenuOnly && (action.Primary ? primary++ < layout.PrimaryCount
															: secondary++ < layout.SecondaryCount);
					if (shown && layout.Overflow)
						continue;
					ScopedID actionID(action.ID);
					if (ImGui::MenuItem(action.Label, action.Shortcut, action.Selected, action.Enabled))
						activated = action.ID;
				}
				ImGui::EndPopup();
			}
		}
		ImGui::EndChild();
		return activated;
	}

	int PanelEmptyState(const char* id, const char* title, const char* description,
						std::span<const PanelAction> actions)
	{
		int activated = -1;
		const bool visible = ImGui::BeginChild(id, {0.0f, 0.0f}, ImGuiChildFlags_None);
		if (visible)
		{
			const float available = ImGui::GetContentRegionAvail().x;
			const float width = std::min(available, ImGui::GetFontSize() * 32.0f);
			const float top = std::max(0.0f, ImGui::GetContentRegionAvail().y * 0.18f);
			ImGui::Dummy({0.0f, top});
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (available - width) * 0.5f));
			const bool content = ImGui::BeginChild("content", {width, 0.0f}, ImGuiChildFlags_None);
			if (content)
			{
				ScopedColour heading(ImGuiCol_Text, Colors::Theme::textBrighter);
				ImGui::TextWrapped("%s", title);
				ImGui::Spacing();
				{
					ScopedColour caption(ImGuiCol_Text, Colors::Theme::textCaption);
					ImGui::TextWrapped("%s", description);
				}
				ImGui::Spacing();
				if (!actions.empty())
					activated = PanelActionBar("actions", actions);
			}
			ImGui::EndChild();
		}
		ImGui::EndChild();
		return activated;
	}

	PanelSplit PanelSplitWidths(float available, float fraction, float firstMinimum, float secondMinimum)
	{
		PanelSplit result;
		result.Handle = std::max(4.0f, ImGui::GetFontSize() * 0.35f);
		const float usable = available - result.Handle - ImGui::GetStyle().ItemSpacing.x * 2.0f;
		result.SideBySide = usable >= firstMinimum + secondMinimum;
		if (result.SideBySide)
		{
			fraction = std::isfinite(fraction) ? std::clamp(fraction, 0.0f, 1.0f) : 0.5f;
			result.First = std::clamp(usable * fraction, firstMinimum, usable - secondMinimum);
			result.Second = usable - result.First;
		}
		return result;
	}

	void PanelSplitHandle(const char* id, const PanelSplit& split, float& fraction)
	{
		const float usable = split.First + split.Second;
		if (!split.SideBySide || usable <= 0.0f)
			return;
		const auto& style = ImGui::GetStyle();
		const float height = std::max(ImGui::GetFrameHeight(), ImGui::GetContentRegionAvail().y);
		ScopedStyle padding(ImGuiStyleVar_FramePadding,
							ImVec2(style.FramePadding.x, (height - ImGui::GetFontSize()) * 0.5f));
		ScopedColourStack colors(ImGuiCol_FrameBg, Colors::Theme::backgroundPopup, ImGuiCol_FrameBgHovered,
								 Colors::Theme::accent, ImGuiCol_FrameBgActive, Colors::Theme::accent);
		ImGui::SetNextItemWidth(split.Handle);
		float value = split.First / usable;
		if (ImGui::DragFloat(id, &value, 1.0f / usable, 0.0f, 1.0f, "",
							 ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp))
			fraction = value;
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		ImGui::SetItemTooltip("Drag to resize. Keyboard: Enter to activate, then Left/Right. Proportions "
							  "adapt to the panel width.");
	}

	void PanelMessage(const char* title, const char* description, bool error)
	{
		ScopedColour heading(ImGuiCol_Text, error ? Colors::Theme::textError : Colors::Theme::textBrighter);
		ImGui::TextWrapped("%s", title);
		ScopedColour caption(ImGuiCol_Text, Colors::Theme::textCaption);
		ImGui::TextWrapped("%s", description);
		ImGui::Spacing();
	}
} // namespace Lux::ImGuiEx
