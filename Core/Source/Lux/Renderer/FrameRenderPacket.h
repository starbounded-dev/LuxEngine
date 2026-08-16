#pragma once

#include "Lux/Core/Ref.h"

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
			enum class Kind : uint8_t { Quad, TexturedQuad, Circle, Text };

			Kind Type = Kind::Quad;
			glm::mat4 Transform{ 1.0f };
			glm::vec4 Color{ 1.0f };

			// TexturedQuad
			Ref<Texture2D> Texture;
			float TilingFactor = 1.0f;

			// Text
			Ref<Font> FontAsset;
			std::string Text;
			float MaxWidth = 0.0f;
			float LineSpacing = 0.0f;
			float Kerning = 0.0f;
		};

		struct ColliderDebugItem
		{
			Ref<StaticMesh> Mesh;
			Ref<MeshSource> Source;
			glm::mat4 Transform{ 1.0f };
			bool SimpleCollider = true;
		};

		bool Valid = false;

		SceneRendererCamera Camera;
		glm::mat4 Overlay2DView{ 1.0f };
		glm::mat4 Overlay2DViewProjection{ 1.0f };

		LightEnvironment Lights;
		Ref<Environment> SkyEnvironment;
		float EnvironmentIntensity = 1.0f;
		float EnvironmentLod = 0.0f;
		PostProcessSettings PostProcess;

		Ref<RenderScene> Meshes;

		std::vector<Draw2DItem> Draw2D;
		std::vector<ColliderDebugItem> ColliderDebug;

		void Reset()
		{
			Valid = false;
			SkyEnvironment = nullptr;
			Meshes = nullptr;
			Draw2D.clear();
			ColliderDebug.clear();
		}
	};

}
