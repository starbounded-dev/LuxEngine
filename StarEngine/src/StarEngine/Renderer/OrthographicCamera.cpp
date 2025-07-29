#include "sepch.h"  
#include "StarEngine/Renderer/OrthographicCamera.h"  

#include <glm/gtc/matrix_transform.hpp>  
#include "StarEngine/Debug/Profiler.h"

namespace StarEngine {  

	OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top)  
		: m_ProjectionMatrix(glm::ortho(left, right, bottom, top, -1.0f, 1.0f)), m_ViewMatrix(1.0f)  
	{  
		SE_PROFILE_FUNCTION("OrthographicCamera::OrthographicCamera");  

		m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;  
	}  

	void OrthographicCamera::SetProjection(float left, float right, float bottom, float top)  
	{  
		SE_PROFILE_FUNCTION("OrthographicCamera::SetProjection");  

		m_ProjectionMatrix = glm::ortho(left, right, bottom, top, -1.0f, 1.0f);  
		m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;  
	}  

	void OrthographicCamera::RecalculateViewMatrix()  
	{  
		SE_PROFILE_FUNCTION("OrthographicCamera::RecalculateViewMatrix"); // Ensure proper semicolon is added  

		glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_Position) *  
			glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation), glm::vec3(0, 0, 1));  

		m_ViewMatrix = glm::inverse(transform);  
		m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;  
	}  

}
