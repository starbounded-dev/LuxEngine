// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "TilePalettePanel.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Editor/EditorStack.h"
#include "Lux/ImGui/ImGuiEx.h"
#include "Lux/Physics2D/PhysicsScene2D.h"
#include "Lux/Renderer/Renderer2D.h"
#include "Lux/Renderer/SpriteSheet.h"
#include "Lux/Renderer/Texture.h"
#include "Lux/Scene/Scene.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <format>

namespace Lux
{
	namespace
	{
		constexpr float kMinZoom = 0.25f;
		constexpr float kMaxZoom = 16.0f;
	}

	TilemapComponent* TilePalettePanel::GetTilemap() const
	{
		Entity target = m_Target; // GetComponent is non-const on Entity
		return target && target.HasComponent<TilemapComponent>() ? &target.GetComponent<TilemapComponent>() : nullptr;
	}

	void TilePalettePanel::SetTarget(Entity entity)
	{
		Entity next = entity && entity.HasComponent<TilemapComponent>() ? entity : Entity{};
		if (next != m_Target)
		{
			EndStroke();
			m_Target = next;
			m_Layer = 0;
		}
	}

	void TilePalettePanel::EndStroke()
	{
		if (m_Stroking && m_StrokeChanged && m_Target)
		{
			if (m_Scene)
			{
				if (PhysicsScene2D* physics = m_Scene->GetPhysicsScene2D())
					physics->RebuildTilemap(m_Target);
			}
			EditorStack::Get().MarkEntitiesEdited({ m_Target.GetUUID() }, "Paint Tiles");
		}
		m_Stroking = m_StrokeChanged = false;
		m_LastPainted = glm::ivec2(INT32_MIN);
	}

	void TilePalettePanel::OnImGuiRender(bool& isOpen)
	{
		m_PanelOpen = isOpen;
		if (!isOpen)
		{
			EndStroke();
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(320.0f, 460.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Tile Palette", &isOpen))
		{
			m_PanelOpen = false;
			ImGui::End();
			return;
		}

		TilemapComponent* tilemap = GetTilemap();
		if (!tilemap)
		{
			ImGui::TextDisabled("Select an entity with a Tilemap to paint it.");
			ImGui::End();
			return;
		}

		const Ref<SpriteSheet> sheet = tilemap->TileSet ? AssetManager::GetAsset<SpriteSheet>(tilemap->TileSet) : nullptr;
		if (!sheet)
		{
			ImGui::TextDisabled("Give the Tilemap a Tile Set (a sprite sheet) first.");
			ImGui::End();
			return;
		}

		const int32_t frameCount = static_cast<int32_t>(sheet->Frames.size());
		m_Tile = std::clamp(m_Tile, 0, std::max(frameCount - 1, 0));
		m_Layer = std::clamp(m_Layer, 0, std::max(static_cast<int32_t>(tilemap->Layers.size()) - 1, 0));

		ImGui::Checkbox("Paint in Scene View##tile_paint", &m_PaintInScene);

		static const char* toolNames[] = { "Paint", "Erase", "Rectangle", "Fill", "Pick" };
		for (int32_t i = 0; i < 5; i++)
		{
			if (i)
				ImGui::SameLine();
			const std::string label = std::format("{}##tile_tool_{}", toolNames[i], i);
			const bool active = static_cast<int32_t>(m_Tool) == i;
			if (active)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
			if (ImGui::Button(label.c_str()))
				m_Tool = static_cast<Tool>(i);
			if (active)
				ImGui::PopStyleColor();
		}

		{
			const std::string preview = tilemap->Layers.empty() ? std::string("(none)") : tilemap->Layers[static_cast<size_t>(m_Layer)].Name;
			if (ImGui::BeginCombo("Layer##tile_layer", preview.c_str()))
			{
				for (int32_t i = 0; i < static_cast<int32_t>(tilemap->Layers.size()); i++)
				{
					const std::string label = std::format("{}##tile_layer_{}", tilemap->Layers[static_cast<size_t>(i)].Name, i);
					if (ImGui::Selectable(label.c_str(), i == m_Layer))
						m_Layer = i;
				}
				ImGui::EndCombo();
			}
		}

		ImGui::Text("Tile %d", m_Tile);
		if (m_Tile < frameCount)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", sheet->Frames[static_cast<size_t>(m_Tile)].Name.c_str());
		}

		const Ref<Texture2D> texture = sheet->Texture ? AssetManager::GetAsset<Texture2D>(sheet->Texture) : nullptr;
		if (!texture)
		{
			// Without a texture the tiles are plain squares: pick by number.
			if (ImGui::BeginChild("##tile_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
			{
				for (int32_t i = 0; i < frameCount; i++)
				{
					const std::string label = std::format("{}: {}##tile_{}", i, sheet->Frames[static_cast<size_t>(i)].Name, i);
					if (ImGui::Selectable(label.c_str(), i == m_Tile))
						m_Tile = i;
				}
			}
			ImGui::EndChild();
			ImGui::End();
			return;
		}

		ImGui::SliderFloat("Zoom##tile_zoom", &m_Zoom, kMinZoom, kMaxZoom, "%.2fx", ImGuiSliderFlags_Logarithmic);
		if (ImGui::BeginChild("##tile_image", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
		{
			const glm::ivec2 textureSize(texture->GetSize());
			const ImVec2 size(textureSize.x * m_Zoom, textureSize.y * m_Zoom);
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			// The texture is uploaded with its bottom row first, so it is drawn with v flipped.
			ImGui::Image(ImGuiEx::GetTextureID(texture), size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
			const bool hovered = ImGui::IsItemHovered();
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			for (int32_t i = 0; i < frameCount; i++)
			{
				const glm::ivec4& rect = sheet->Frames[static_cast<size_t>(i)].Rect;
				const ImVec2 min(origin.x + rect.x * m_Zoom, origin.y + rect.y * m_Zoom);
				const ImVec2 max(min.x + rect.z * m_Zoom, min.y + rect.w * m_Zoom);
				drawList->AddRect(min, max, i == m_Tile ? IM_COL32(255, 200, 40, 255) : IM_COL32(255, 255, 255, 50), 0.0f, 0, i == m_Tile ? 2.0f : 1.0f);
			}
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				const ImVec2 mouse = ImGui::GetMousePos();
				const glm::vec2 texel((mouse.x - origin.x) / m_Zoom, (mouse.y - origin.y) / m_Zoom);
				for (int32_t i = frameCount - 1; i >= 0; i--)
				{
					const glm::ivec4& rect = sheet->Frames[static_cast<size_t>(i)].Rect;
					if (texel.x >= rect.x && texel.x < rect.x + rect.z && texel.y >= rect.y && texel.y < rect.y + rect.w)
					{
						m_Tile = i;
						if (m_Tool == Tool::Erase || m_Tool == Tool::Pick)
							m_Tool = Tool::Paint;
						break;
					}
				}
			}
		}
		ImGui::EndChild();
		ImGui::End();
	}

	void TilePalettePanel::OnSceneMouse(bool onPlane, const glm::ivec2& cell, const Tilemap::CellRect& visible, bool down)
	{
		TilemapComponent* tilemap = GetTilemap();
		m_HoverValid = tilemap && onPlane;
		m_HoverCell = cell;
		if (!tilemap || tilemap->Layers.empty())
		{
			EndStroke();
			m_WasDown = down;
			return;
		}

		TilemapLayer& layer = tilemap->Layers[static_cast<size_t>(std::clamp(m_Layer, 0, static_cast<int32_t>(tilemap->Layers.size()) - 1))];
		const bool pressed = down && !m_WasDown && onPlane;
		const bool released = !down && m_WasDown;
		m_WasDown = down;

		const int32_t paintTile = m_Tool == Tool::Erase ? -1 : m_Tile;
		if (pressed)
		{
			m_Stroking = true;
			m_StrokeChanged = false;
			m_StrokeStart = cell;
			if (m_Tool == Tool::Pick)
			{
				const int32_t picked = Tilemap::GetTile(layer, cell.x, cell.y);
				if (picked >= 0)
					m_Tile = picked;
				m_Tool = Tool::Paint;
				m_Stroking = false;
			}
			else if (m_Tool == Tool::Fill)
			{
				m_StrokeChanged = Tilemap::FloodFill(layer, cell.x, cell.y, m_Tile, visible) > 0;
				EndStroke();
			}
		}

		if (m_Stroking && down && onPlane && (m_Tool == Tool::Paint || m_Tool == Tool::Erase) && cell != m_LastPainted)
		{
			// A fast drag skips cells: fill the straight line from the last painted one.
			glm::ivec2 from = m_LastPainted.x == INT32_MIN ? cell : m_LastPainted;
			const glm::ivec2 delta = cell - from;
			const int32_t steps = std::max(std::abs(delta.x), std::abs(delta.y));
			for (int32_t i = 0; i <= steps; i++)
			{
				const glm::ivec2 p = steps ? from + glm::ivec2(glm::round(glm::vec2(delta) * (static_cast<float>(i) / static_cast<float>(steps)))) : from;
				m_StrokeChanged |= Tilemap::SetTile(layer, p.x, p.y, paintTile);
			}
			m_LastPainted = cell;
		}

		if (released && m_Stroking)
		{
			if (m_Tool == Tool::Rectangle)
			{
				const glm::ivec2 low = glm::min(m_StrokeStart, cell), high = glm::max(m_StrokeStart, cell);
				m_StrokeChanged = Tilemap::FillRect(layer, { low.x, low.y, high.x - low.x + 1, high.y - low.y + 1 }, m_Tile) > 0;
			}
			EndStroke();
		}
	}

	void TilePalettePanel::DrawOverlay(Renderer2D& renderer, const glm::mat4& tilemapWorld) const
	{
		const TilemapComponent* tilemap = GetTilemap();
		if (!tilemap || !m_HoverValid)
			return;

		const auto drawBox = [&](const glm::ivec2& low, const glm::ivec2& high, const glm::vec4& colour)
		{
			const glm::vec2 a = glm::vec2(low) * tilemap->CellSize, b = glm::vec2(high + 1) * tilemap->CellSize;
			const glm::vec3 p[4] = {
				glm::vec3(tilemapWorld * glm::vec4(a.x, a.y, 0.0f, 1.0f)), glm::vec3(tilemapWorld * glm::vec4(b.x, a.y, 0.0f, 1.0f)),
				glm::vec3(tilemapWorld * glm::vec4(b.x, b.y, 0.0f, 1.0f)), glm::vec3(tilemapWorld * glm::vec4(a.x, b.y, 0.0f, 1.0f)) };
			for (int32_t i = 0; i < 4; i++)
				renderer.DrawLine(p[i], p[(i + 1) % 4], colour, true);
		};

		if (m_Stroking && m_Tool == Tool::Rectangle)
			drawBox(glm::min(m_StrokeStart, m_HoverCell), glm::max(m_StrokeStart, m_HoverCell), glm::vec4(1.0f, 0.8f, 0.2f, 1.0f));
		else
			drawBox(m_HoverCell, m_HoverCell, glm::vec4(1.0f, 1.0f, 1.0f, 0.9f));
	}
}
