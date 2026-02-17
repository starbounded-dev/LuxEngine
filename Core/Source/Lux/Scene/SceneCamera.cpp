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

	void SceneCamera::SetViewportBounds(uint32_t left, uint32_t top, uint32_t right, uint32_t bottom)
	{
		m_ViewportBounds = { left, top, right, bottom };

		float width = (float)(right - left);
		float height = (float)(bottom - top);

		switch (m_ProjectionType)
		{
		case ProjectionType::Perspective:
			SetPerspectiveProjectionMatrix(glm::radians(m_DegPerspectiveFOV), width, height, m_PerspectiveNear, m_PerspectiveFar);
			break;
		case ProjectionType::Orthographic:
			float aspect = width / height;
			float width = m_OrthographicSize * aspect;
			float height = m_OrthographicSize;
			SetOrthoProjectionMatrix(width, height, m_OrthographicNear, m_OrthographicFar);
			break;
		}
	}

}
