// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Ref.h"
#include "Lux/Particles/ParticleSystem.h"

#include "Lux/Renderer/SceneRenderer.h"     // SceneRendererCamera, LightEnvironment
#include "Lux/Renderer/PostProcessSettings.h"  // PostProcessSettings
#include "Lux/Renderer/RenderScene.h"       // RenderScene
#include "Lux/Renderer/SceneEnvironment.h"  // Environment
#include "Lux/Renderer/Texture.h"           // Texture2D
#include "Lux/Renderer/Mesh.h"              // StaticMesh, MeshSource

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Lux {

	class Font;

	// A self-contained, double-bufferable snapshot of everything the renderer needs for one frame,
	// captured from the ECS while it is quiescent (Scene::BuildRenderPacket*). Render submission then
	// consumes ONLY this packet (Scene::SubmitRenderPacket) and never touches the live scene registry -
	// which is the prerequisite for advancing simulation on a separate thread.
	struct FrameRenderPacket
	{
		struct Draw2DItem
		{
			enum class Kind : uint8_t { Quad, TexturedQuad, Circle, Text, Tiles, Particles };

			// One tile of a Tiles item: its cell (in cells, from the tilemap's origin) and the texture rectangle it shows.
			struct Tile
			{
				glm::vec2 Cell{ 0.0f };
				glm::vec2 UV0{ 0.0f };
				glm::vec2 UV1{ 1.0f };
			};

			Kind Type = Kind::Quad;
			glm::mat4 Transform{ 1.0f };
			glm::vec4 Color{ 1.0f };

			// TexturedQuad
			Ref<Texture2D> Texture;
			float TilingFactor = 1.0f;
			bool PointFilter = false; // nearest-neighbour sampling
			glm::vec2 UV0{ 0.0f }; // bottom left of the texture rectangle drawn
			glm::vec2 UV1{ 1.0f }; // top right

			// Tiles: one layer's visible tiles. Transform is the tilemap's world transform scaled by its cell size, so
			// a unit quad moved to (Cell + 0.5) is one tile; Texture is the tile set's.
			std::vector<Tile> Tiles;

			// Particles: one 2D system's particles, older first, in world space; Texture is the system's (none draws colored quads).
			std::vector<Lux::Particles::Instance> ParticleInstances;

			// Text
			Ref<Font> FontAsset;
			std::string Text;
			float MaxWidth = 0.0f;
			float LineSpacing = 0.0f;
			float Kerning = 0.0f;
		};

		struct DebugLine
		{
			glm::vec3 Start, End;
			glm::vec4 Color;
			bool OnTop = true; // false: hidden behind geometry
		};

		struct ColliderDebugItem
		{
			Ref<StaticMesh> Mesh;
			Ref<MeshSource> Source;
			glm::mat4 Transform{ 1.0f };
			bool SimpleCollider = true;
			int32_t DebugCategory = -1; // DebugCategoryPalette index; -1 draws the collider colours
		};

		bool Valid = false;

		SceneRendererCamera Camera;
		glm::mat4 Overlay2DView{ 1.0f };
		glm::mat4 Overlay2DViewProjection{ 1.0f };

		LightEnvironment Lights;
		Ref<Environment> SkyEnvironment;
		float EnvironmentIntensity = 1.0f;
		float EnvironmentLod = 0.0f;
		float EnvironmentRotation = 0.0f; // degrees about +Y
		PostProcessSettings PostProcess;

		Ref<RenderScene> Meshes;
		float MeshSyncMs = 0.0f; // CPU time of the render-scene sync that produced Meshes

		std::vector<Draw2DItem> Draw2D;
		// Game UI, in draw order, with transforms in screen pixels (origin bottom left). Drawn after Draw2D with a
		// pixel-space projection of UIScreenSize.
		std::vector<Draw2DItem> UI;
		glm::vec2 UIScreenSize{ 0.0f };
		// 3D particle systems, far to near: SceneRenderer draws them after the transparent meshes.
		std::vector<SceneRendererParticleBatch> ParticleBatches;
		std::vector<DebugLine> AudioZoneLines;
		std::vector<DebugLine> ScriptLines; // C# Gizmos (Scene::AddDebugLine)
		std::vector<ColliderDebugItem> ColliderDebug;

		void Reset()
		{
			Valid = false;
			SkyEnvironment = nullptr;
			Meshes = nullptr;
			MeshSyncMs = 0.0f;
			Draw2D.clear();
			UI.clear();
			ParticleBatches.clear();
			AudioZoneLines.clear();
			ScriptLines.clear();
			ColliderDebug.clear();
		}
	};

}
