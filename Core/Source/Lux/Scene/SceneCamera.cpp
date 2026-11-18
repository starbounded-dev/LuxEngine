// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "SceneCamera.h"

namespace Lux {


	void SceneCamera::SetPerspective(float degVerticalFOV, float nearClip, float farClip)
	{
		m_ProjectionType = ProjectionType::Perspective;
		m_DegPerspectiveFOV = degVerticalFOV;
		m_PerspectiveNear = nearClip;
		m_PerspectiveFar = farClip;
	}

	void SceneCamera::SetOrthographic(float size, float nearClip, float farClip)
	{
		m_ProjectionType = ProjectionType::Orthographic;
		m_OrthographicSize = size;
		m_OrthographicNear = nearClip;
		m_OrthographicFar = farClip;
	}

	void SceneCamera::SetViewportSize(uint32_t width, uint32_t height)
	{
		SetViewportBounds(0, 0, width, height);
	}

	void SceneCamera::Rebuild()
	{
		const auto [left, top, right, bottom] = m_ViewportBounds;
		SetViewportBounds(left, top, right, bottom);
	}

	uint32_t SceneCamera::GetPixelScale() const
	{
		if (!m_PixelPerfect)
			return 1;

		const auto [left, top, right, bottom] = m_ViewportBounds;
		return std::max(1u, static_cast<uint32_t>(static_cast<float>(bottom - top) / std::max(m_ReferenceHeight, 1.0f)));
	}

	float SceneCamera::GetPixelSnapUnit() const
	{
		if (!m_PixelPerfect || m_ProjectionType != ProjectionType::Orthographic)
			return 0.0f;

		return 1.0f / (std::max(m_PixelsPerUnit, 1e-3f) * static_cast<float>(GetPixelScale()));
	}

	void SceneCamera::SetViewportBounds(uint32_t left, uint32_t top, uint32_t right, uint32_t bottom)
	{
		m_ViewportBounds = { left, top, right, bottom };

		float viewportWidth = (float)(right - left);
		float viewportHeight = (float)(bottom - top);
		if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
			return;

		switch (m_ProjectionType)
		{
		case ProjectionType::Perspective:
			SetPerspectiveProjectionMatrix(glm::radians(m_DegPerspectiveFOV), viewportWidth, viewportHeight, m_PerspectiveNear, m_PerspectiveFar);
			break;
		case ProjectionType::Orthographic:
		{
			float aspect = viewportWidth / viewportHeight;
			float orthoHeight = m_PixelPerfect ? viewportHeight * GetPixelSnapUnit() : m_OrthographicSize;
			float orthoWidth = orthoHeight * aspect;
			SetOrthoProjectionMatrix(orthoWidth, orthoHeight, m_OrthographicNear, m_OrthographicFar);
			break;
		}
		}
	}

}
