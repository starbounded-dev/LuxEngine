// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Asset/Asset.h"
#include "Lux/Core/Ref.h"
#include "Lux/Renderer/Image.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"

#include <glm/glm.hpp>

namespace Lux
{
	class Viewport;

	// A self-contained scene (one mesh, a sun and a dynamic sky) rendered by its own SceneRenderer,
	// used to preview a material outside the edited scene. Editor-only render targets are disabled,
	// so its cost is a small off-screen render. All calls are main-thread; the renderer queues its
	// GPU work through Renderer::Submit like the main viewport.
	class MaterialPreview : public RefCounted
	{
	public:
		enum class Shape : uint8_t
		{
			Sphere = 0,
			Cube,
			Cylinder,
			Plane,
			Torus,
			Count
		};

		MaterialPreview();
		~MaterialPreview();

		void SetMaterial(AssetHandle materialHandle);
		AssetHandle GetMaterial() const { return m_MaterialHandle; }

		void SetShape(Shape shape);
		Shape GetShape() const { return m_Shape; }

		void SetSkyEnabled(bool enabled);
		bool IsSkyEnabled() const { return m_SkyEnabled; }

		// Orbit around the mesh. Deltas are in degrees; zoom multiplies the camera distance.
		void Orbit(float yawDegrees, float pitchDegrees);
		void Zoom(float factor);
		void ResetCamera();

		// Queue a render at the given output size and return the image it writes. Returns null
		// until the renderer has created its GPU resources (the first frames after creation).
		Ref<Image2D> Render(uint32_t width, uint32_t height);

		static const char* GetShapeName(Shape shape);

	private:
		void ApplyShape();
		void ApplyCamera();

	private:
		Ref<Scene> m_Scene;
		Ref<Viewport> m_Viewport;
		Entity m_MeshEntity;
		Entity m_SkyEntity;

		AssetHandle m_MaterialHandle = 0;
		Shape m_Shape = Shape::Sphere;
		bool m_SkyEnabled = true;

		float m_Yaw = 0.0f;
		float m_Pitch = 15.0f;
		float m_Distance = 2.0f;
	};
}
