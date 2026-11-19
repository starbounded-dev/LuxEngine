// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ParticleWidgets.h"

#include "Lux/ImGui/ImGuiEx.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Lux::ParticleWidgets
{
	namespace
	{
		constexpr float k_PickRadius = 9.0f;

		float Distance(const ImVec2& a, const ImVec2& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

		// The key a drag moves to: its time stays between its neighbours (the end keys keep theirs).
		float ClampTime(float time, uint32_t index, uint32_t count, float previous, float next)
		{
			if (index == 0 || index + 1 == count)
				return index == 0 ? 0.0f : 1.0f;
			return std::clamp(time, previous + 0.001f, next - 0.001f);
		}
	}

	bool Curve(const char* id, Particles::Curve& curve, float maxValue, ImVec2 size)
	{
		maxValue = std::max(maxValue, 0.001f);
		ImGui::PushID(id);
		if (size.x <= 0.0f)
			size.x = std::max(ImGui::GetContentRegionAvail().x, 40.0f);
		if (size.y <= 0.0f)
			size.y = ImGui::GetFontSize() * 6.0f;

		const ImVec2 min = ImGui::GetCursorScreenPos();
		const ImVec2 max = { min.x + size.x, min.y + size.y };
		ImGui::InvisibleButton("##curve", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const ImGuiID itemID = ImGui::GetItemID();
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();

		ImGuiStorage* storage = ImGui::GetStateStorage();
		const ImGuiID selectedID = ImGui::GetID("selected");
		int selected = std::min(storage->GetInt(selectedID, 0), static_cast<int>(curve.Count) - 1);

		const auto toScreen = [&](float time, float value) { return ImVec2(min.x + time * size.x, max.y - std::clamp(value / maxValue, 0.0f, 1.0f) * size.y); };
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		int hit = -1;
		float best = k_PickRadius;
		for (uint32_t i = 0; i < curve.Count; i++)
		{
			const float distance = Distance(mouse, toScreen(curve.Keys[i].T, curve.Keys[i].Value));
			if (distance <= best)
			{
				best = distance;
				hit = static_cast<int>(i);
			}
		}

		bool changed = false;
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			if (hit >= 0)
				selected = hit;
			else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				const float time = std::clamp((mouse.x - min.x) / size.x, 0.0f, 1.0f);
				const float value = std::clamp((max.y - mouse.y) / size.y * maxValue, 0.0f, maxValue);
				if (curve.Add(time, value))
				{
					for (uint32_t i = 0; i < curve.Count; i++)
						if (std::abs(curve.Keys[i].T - time) < 1e-5f)
							selected = static_cast<int>(i);
					changed = true;
				}
			}
		}
		else if (hovered && hit >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && curve.Count > 1)
		{
			curve.Remove(static_cast<uint32_t>(hit));
			selected = std::min(selected, static_cast<int>(curve.Count) - 1);
			changed = true;
		}
		else if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && selected >= 0 && selected < static_cast<int>(curve.Count) && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
		{
			Particles::Curve::Key& key = curve.Keys[selected];
			const float previous = selected > 0 ? curve.Keys[selected - 1].T : 0.0f;
			const float next = selected + 1 < static_cast<int>(curve.Count) ? curve.Keys[selected + 1].T : 1.0f;
			const float time = ClampTime(std::clamp((mouse.x - min.x) / size.x, 0.0f, 1.0f), static_cast<uint32_t>(selected), curve.Count, previous, next);
			const float value = std::clamp((max.y - mouse.y) / size.y * maxValue, 0.0f, maxValue);
			if (time != key.T || value != key.Value)
			{
				key.T = time;
				key.Value = value;
				changed = true;
			}
		}
		storage->SetInt(selectedID, selected);
		if (changed)
			ImGui::MarkItemEdited(itemID);

		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(min, max, IM_COL32(24, 24, 28, 255), 3.0f);
		for (int line = 1; line < 4; line++)
		{
			const float y = max.y - size.y * static_cast<float>(line) / 4.0f;
			draw->AddLine({ min.x, y }, { max.x, y }, IM_COL32(255, 255, 255, 22));
		}
		for (int line = 1; line < 4; line++)
		{
			const float x = min.x + size.x * static_cast<float>(line) / 4.0f;
			draw->AddLine({ x, min.y }, { x, max.y }, IM_COL32(255, 255, 255, 16));
		}
		if (maxValue > 1.0f)
		{
			const float y = toScreen(0.0f, 1.0f).y;
			draw->AddLine({ min.x, y }, { max.x, y }, IM_COL32(255, 200, 80, 60));
		}

		std::vector<ImVec2> points;
		points.push_back(toScreen(0.0f, curve.Keys[0].Value));
		for (uint32_t i = 0; i < curve.Count; i++)
			points.push_back(toScreen(curve.Keys[i].T, curve.Keys[i].Value));
		points.push_back(toScreen(1.0f, curve.Keys[curve.Count - 1].Value));
		draw->AddPolyline(points.data(), static_cast<int>(points.size()), IM_COL32(120, 200, 255, 255), ImDrawFlags_None, 2.0f);
		for (uint32_t i = 0; i < curve.Count; i++)
		{
			const ImVec2 point = toScreen(curve.Keys[i].T, curve.Keys[i].Value);
			const bool isSelected = static_cast<int>(i) == selected;
			draw->AddCircleFilled(point, isSelected ? 6.0f : 4.5f, isSelected ? IM_COL32(255, 220, 90, 255) : IM_COL32(230, 235, 245, 255));
			draw->AddCircle(point, isSelected ? 6.0f : 4.5f, IM_COL32(0, 0, 0, 200));
		}
		draw->AddRect(min, max, hovered || active ? IM_COL32(255, 255, 255, 110) : IM_COL32(255, 255, 255, 50), 3.0f);
		draw->AddText({ min.x + 4.0f, min.y + 2.0f }, IM_COL32(255, 255, 255, 120), std::format("{:g}", maxValue).c_str());

		if (hovered && hit >= 0)
			ImGui::SetTooltip("Life %.2f   value %.3f\nDrag to move, right-click to remove", curve.Keys[hit].T, curve.Keys[hit].Value);
		else if (hovered)
			ImGui::SetTooltip("Double-click to add a key");

		ImGui::PopID();
		return changed;
	}

	bool Gradient(const char* id, Particles::Gradient& gradient, float width)
	{
		ImGui::PushID(id);
		if (width <= 0.0f)
			width = std::max(ImGui::GetContentRegionAvail().x, 40.0f);
		const float barHeight = ImGui::GetFrameHeight();
		const float markerHeight = ImGui::GetFontSize() * 0.8f;

		const ImVec2 min = ImGui::GetCursorScreenPos();
		const ImVec2 barMax = { min.x + width, min.y + barHeight };
		ImGui::InvisibleButton("##gradient", { width, barHeight + markerHeight }, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		const ImGuiID itemID = ImGui::GetItemID();
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();

		ImGuiStorage* storage = ImGui::GetStateStorage();
		const ImGuiID selectedID = ImGui::GetID("selected");
		int selected = std::min(storage->GetInt(selectedID, 0), static_cast<int>(gradient.Count) - 1);

		const ImVec2 mouse = ImGui::GetIO().MousePos;
		int hit = -1;
		float best = k_PickRadius;
		for (uint32_t i = 0; i < gradient.Count; i++)
		{
			const float distance = std::abs(mouse.x - (min.x + gradient.Keys[i].T * width));
			if (distance <= best && mouse.y >= min.y)
			{
				best = distance;
				hit = static_cast<int>(i);
			}
		}

		bool changed = false;
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			if (hit >= 0)
				selected = hit;
			else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				const float time = std::clamp((mouse.x - min.x) / width, 0.0f, 1.0f);
				if (gradient.Add(time, gradient.Evaluate(time)))
				{
					for (uint32_t i = 0; i < gradient.Count; i++)
						if (std::abs(gradient.Keys[i].T - time) < 1e-5f)
							selected = static_cast<int>(i);
					changed = true;
				}
			}
		}
		else if (hovered && hit >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && gradient.Count > 1)
		{
			gradient.Remove(static_cast<uint32_t>(hit));
			selected = std::min(selected, static_cast<int>(gradient.Count) - 1);
			changed = true;
		}
		else if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && selected >= 0 && selected < static_cast<int>(gradient.Count) && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
		{
			Particles::Gradient::Key& key = gradient.Keys[selected];
			const float previous = selected > 0 ? gradient.Keys[selected - 1].T : 0.0f;
			const float next = selected + 1 < static_cast<int>(gradient.Count) ? gradient.Keys[selected + 1].T : 1.0f;
			const float time = ClampTime(std::clamp((mouse.x - min.x) / width, 0.0f, 1.0f), static_cast<uint32_t>(selected), gradient.Count, previous, next);
			if (time != key.T)
			{
				key.T = time;
				changed = true;
			}
		}
		storage->SetInt(selectedID, selected);
		if (changed)
			ImGui::MarkItemEdited(itemID);

		ImDrawList* draw = ImGui::GetWindowDrawList();
		// A checkerboard under the bar shows the alpha.
		const float cell = barHeight * 0.5f;
		for (float x = min.x, column = 0.0f; x < barMax.x; x += cell, column += 1.0f)
		{
			for (int row = 0; row < 2; row++)
			{
				const ImVec2 a = { x, min.y + cell * static_cast<float>(row) };
				const ImVec2 b = { std::min(x + cell, barMax.x), a.y + cell };
				draw->AddRectFilled(a, b, (static_cast<int>(column) + row) % 2 ? IM_COL32(90, 90, 90, 255) : IM_COL32(60, 60, 60, 255));
			}
		}
		const auto packed = [](const glm::vec4& color) { return ImGui::ColorConvertFloat4ToU32({ color.r, color.g, color.b, color.a }); };
		const auto segment = [&](float from, float to, const glm::vec4& left, const glm::vec4& right)
		{
			if (to <= from)
				return;
			draw->AddRectFilledMultiColor({ min.x + from * width, min.y }, { min.x + to * width, barMax.y }, packed(left), packed(right), packed(right), packed(left));
		};
		segment(0.0f, gradient.Keys[0].T, gradient.Keys[0].Color, gradient.Keys[0].Color);
		for (uint32_t i = 1; i < gradient.Count; i++)
			segment(gradient.Keys[i - 1].T, gradient.Keys[i].T, gradient.Keys[i - 1].Color, gradient.Keys[i].Color);
		segment(gradient.Keys[gradient.Count - 1].T, 1.0f, gradient.Keys[gradient.Count - 1].Color, gradient.Keys[gradient.Count - 1].Color);
		draw->AddRect(min, barMax, hovered || active ? IM_COL32(255, 255, 255, 110) : IM_COL32(255, 255, 255, 50));

		for (uint32_t i = 0; i < gradient.Count; i++)
		{
			const float x = min.x + gradient.Keys[i].T * width;
			const bool isSelected = static_cast<int>(i) == selected;
			const ImVec2 top = { x, barMax.y };
			const ImVec2 left = { x - markerHeight * 0.5f, barMax.y + markerHeight };
			const ImVec2 right = { x + markerHeight * 0.5f, barMax.y + markerHeight };
			const glm::vec4& color = gradient.Keys[i].Color;
			draw->AddTriangleFilled(top, left, right, packed({ color.r, color.g, color.b, 1.0f }));
			draw->AddTriangle(top, left, right, isSelected ? IM_COL32(255, 220, 90, 255) : IM_COL32(0, 0, 0, 220), isSelected ? 2.0f : 1.0f);
		}

		if (hovered && hit >= 0)
			ImGui::SetTooltip("Life %.2f\nDrag to move, right-click to remove", gradient.Keys[hit].T);
		else if (hovered)
			ImGui::SetTooltip("Double-click to add a key");

		// The selected key's color; its time is the marker's place.
		if (selected >= 0 && selected < static_cast<int>(gradient.Count))
		{
			ImGui::SetNextItemWidth(width);
			glm::vec4& color = gradient.Keys[selected].Color;
			if (ImGui::ColorEdit4("##key_color", &color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_HDR))
				changed = true;
		}

		ImGui::PopID();
		return changed;
	}

	bool PropertyCurve(const char* label, Particles::Curve& curve, float maxValue, const char* helpText)
	{
		ImGuiEx::ShiftCursor(10.0f, 9.0f);
		ImGuiEx::PropertyLabel(label, helpText);
		ImGui::NextColumn();
		ImGuiEx::ShiftCursorY(4.0f);
		ImGui::PushItemWidth(ImGuiEx::GetPropertyItemWidth());
		const bool changed = Curve(label, curve, maxValue, { ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 6.0f });
		ImGui::PopItemWidth();
		ImGui::NextColumn();
		ImGuiEx::Draw::Underline();
		return changed;
	}

	bool PropertyGradient(const char* label, Particles::Gradient& gradient, const char* helpText)
	{
		ImGuiEx::ShiftCursor(10.0f, 9.0f);
		ImGuiEx::PropertyLabel(label, helpText);
		ImGui::NextColumn();
		ImGuiEx::ShiftCursorY(4.0f);
		ImGui::PushItemWidth(ImGuiEx::GetPropertyItemWidth());
		const bool changed = Gradient(label, gradient, ImGui::GetContentRegionAvail().x);
		ImGui::PopItemWidth();
		ImGui::NextColumn();
		ImGuiEx::Draw::Underline();
		return changed;
	}
}
