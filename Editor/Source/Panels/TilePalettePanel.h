// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Editor/EditorPanel.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Tilemap.h"

#include <glm/glm.hpp>

namespace Lux
{
	class Renderer2D;

	// Paints the selected entity's tilemap: pick a tile from its tile set here, then paint, erase, fill a rectangle
	// or flood fill in the Scene View (or pick a tile up from the map). Each stroke is one undo step. The Scene
	// View routes the mouse here while IsPainting(); selection and the gizmo step aside. Main thread only.
	class TilePalettePanel : public EditorPanel
	{
	public:
		enum class Tool { Paint = 0, Erase, Rectangle, Fill, Pick };

		void OnImGuiRender(bool& isOpen) override;
		void SetSceneContext(const Ref<Scene>& context) override { m_Scene = context; m_Target = {}; }
		void OnProjectChanged(const Ref<Project>&) override { m_Target = {}; m_Tile = 0; m_Layer = 0; }

		// The entity to paint (or none): the Scene View's selection when it has a Tilemap. Called every frame.
		void SetTarget(Entity entity);
		Entity GetTarget() const { return m_Target; }

		// Whether the Scene View should paint: the panel is open, "Paint in Scene View" is on and there is a tilemap.
		bool IsPainting() const { return m_PanelOpen && m_PaintInScene && m_Target; }

		// The mouse over the Scene View. `cell` is the cell under it (valid when `onPlane`), `visible` the cells in view
		// (the limit of a flood fill of empty space). `down` is the left button; the panel finds the press and release.
		void OnSceneMouse(bool onPlane, const glm::ivec2& cell, const Tilemap::CellRect& visible, bool down);

		// The cell under the mouse and a rectangle being dragged, as lines in the tilemap's plane.
		void DrawOverlay(Renderer2D& renderer, const glm::mat4& tilemapWorld) const;

	private:
		void EndStroke();
		TilemapComponent* GetTilemap() const;

		Ref<Scene> m_Scene;
		Entity m_Target;
		bool m_PanelOpen = false;
		bool m_PaintInScene = true;
		Tool m_Tool = Tool::Paint;
		int32_t m_Tile = 0;       // the frame painted
		int32_t m_Layer = 0;      // the layer painted
		float m_Zoom = 2.0f;

		bool m_WasDown = false;
		bool m_Stroking = false;
		bool m_StrokeChanged = false;
		glm::ivec2 m_StrokeStart{ 0 };
		glm::ivec2 m_HoverCell{ 0 };
		bool m_HoverValid = false;
		glm::ivec2 m_LastPainted{ INT32_MIN };
	};
}
