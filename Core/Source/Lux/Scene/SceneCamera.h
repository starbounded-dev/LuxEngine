// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Renderer/Camera.h"

#include <utility>

namespace Lux {

	class SceneCamera : public Camera
	{
	public:
		enum class ProjectionType { Perspective = 0, Orthographic = 1 };
	public:
		void SetPerspective(float verticalFOV, float nearClip = 0.1f, float farClip = 1000.0f);
		void SetOrthographic(float size, float nearClip = -1.0f, float farClip = 1.0f);
		void SetViewportSize(uint32_t width, uint32_t height);
		void SetViewportBounds(uint32_t left, uint32_t top, uint32_t right, uint32_t bottom);
		std::tuple<uint32_t, uint32_t, uint32_t, uint32_t> GetViewportBounds() const { return m_ViewportBounds; }

		void SetDegPerspectiveVerticalFOV(const float degVerticalFov) { m_DegPerspectiveFOV = degVerticalFov; }
		void SetRadPerspectiveVerticalFOV(const float degVerticalFov) { m_DegPerspectiveFOV = glm::degrees(degVerticalFov); }
		float GetDegPerspectiveVerticalFOV() const { return m_DegPerspectiveFOV; }
		float GetRadPerspectiveVerticalFOV() const { return glm::radians(m_DegPerspectiveFOV); }
		void SetPerspectiveNearClip(const float nearClip) { m_PerspectiveNear = nearClip; }
		float GetPerspectiveNearClip() const { return m_PerspectiveNear; }
		void SetPerspectiveFarClip(const float farClip) { m_PerspectiveFar = farClip; }
		float GetPerspectiveFarClip() const { return m_PerspectiveFar; }

		void SetOrthographicSize(const float size) { m_OrthographicSize = size; }
		float GetOrthographicSize() const { return m_OrthographicSize; }
		void SetOrthographicNearClip(const float nearClip) { m_OrthographicNear = nearClip; }
		float GetOrthographicNearClip() const { return m_OrthographicNear; }
		void SetOrthographicFarClip(const float farClip) { m_OrthographicFar = farClip; }
		float GetOrthographicFarClip() const { return m_OrthographicFar; }

		// Pixel-perfect orthographic view: one world unit is PixelsPerUnit pixels times a whole-number
		// scale (the largest that still shows ReferenceHeight pixels), so every sprite pixel covers a whole
		// number of screen pixels. The orthographic size is ignored while it is on.
		void SetPixelPerfect(bool enabled) { m_PixelPerfect = enabled; Rebuild(); }
		bool IsPixelPerfect() const { return m_PixelPerfect; }
		void SetPixelsPerUnit(float pixelsPerUnit) { m_PixelsPerUnit = pixelsPerUnit; Rebuild(); }
		float GetPixelsPerUnit() const { return m_PixelsPerUnit; }
		void SetReferenceHeight(float pixels) { m_ReferenceHeight = pixels; Rebuild(); }
		float GetReferenceHeight() const { return m_ReferenceHeight; }
		// Whole-number screen pixels per sprite pixel at the current viewport (1 when not pixel perfect).
		uint32_t GetPixelScale() const;
		// World-space size of one screen pixel for a pixel-perfect orthographic camera, else 0. The
		// camera's position is rounded to it so a moving camera does not shimmer the sprites.
		float GetPixelSnapUnit() const;

		void SetProjectionType(ProjectionType type) { m_ProjectionType = type; }
		ProjectionType GetProjectionType() const { return m_ProjectionType; }
	private:
		void Rebuild();

		ProjectionType m_ProjectionType = ProjectionType::Perspective;

		float m_DegPerspectiveFOV = 45.0f;
		float m_PerspectiveNear = 0.1f, m_PerspectiveFar = 1000.0f;

		float m_OrthographicSize = 10.0f;
		bool m_PixelPerfect = false;
		float m_PixelsPerUnit = 100.0f;
		float m_ReferenceHeight = 180.0f;
		float m_OrthographicNear = -1.0f, m_OrthographicFar = 1.0f;

		std::tuple<uint32_t, uint32_t, uint32_t, uint32_t> m_ViewportBounds = { 0, 0, 0, 0 };
	};

}
