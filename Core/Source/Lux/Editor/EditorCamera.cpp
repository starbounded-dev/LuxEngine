// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "EditorCamera.h"

#include "Lux/Core/Input.h"

#include <GLFW/glfw3.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "Lux/Core/Application.h"
#include "Lux/ImGui/ImGuiCore.h"

namespace Lux {

	EditorCamera::EditorCamera(const float degFov, const float width, const float height, const float nearP, const float farP)
		: Camera(glm::perspectiveFov(glm::radians(degFov), width, height, farP, nearP), glm::perspectiveFov(glm::radians(degFov), width, height, nearP, farP)), m_FocalPoint(0.0f), m_VerticalFOV(glm::radians(degFov)), m_NearClip(nearP), m_FarClip(farP)
	{
		Init();
	}

	void EditorCamera::Init()
	{
		constexpr glm::vec3 position = { -5, 5, 5 };
		m_Distance = glm::distance(position, m_FocalPoint);

		m_Yaw = 3.0f * glm::pi<float>() / 4.0f;
		m_Pitch = glm::pi<float>() / 4.0f;

		m_Position = CalculatePosition();
		const glm::quat orientation = GetOrientation();
		m_Direction = glm::eulerAngles(orientation) * (180.0f / glm::pi<float>());
		m_ViewMatrix = glm::translate(glm::mat4(1.0f), m_Position) * glm::toMat4(orientation);
		m_ViewMatrix = glm::inverse(m_ViewMatrix);
	}

	namespace
	{
		// The 2D camera sits this far in front of the XY plane; the clip range spans both sides of it.
		constexpr float k_Distance2D = 500.0f;
		constexpr float k_NearClip2D = 0.1f;
		constexpr float k_FarClip2D = 1000.0f;
		constexpr float k_MinOrthoHeight = 0.05f;
		constexpr float k_MaxOrthoHeight = 100000.0f;
	}

	void EditorCamera::RebuildProjection()
	{
		const float width = (float)(m_ViewportRight - m_ViewportLeft);
		const float height = (float)(m_ViewportBottom - m_ViewportTop);
		if (width <= 0.0f || height <= 0.0f)
			return;

		if (m_Mode2D)
			SetOrthoProjectionMatrix(m_OrthoHeight * width / height, m_OrthoHeight, k_NearClip2D, k_FarClip2D);
		else
			SetPerspectiveProjectionMatrix(m_VerticalFOV, width, height, m_NearClip, m_FarClip);
	}

	void EditorCamera::Set2DMode(bool enabled)
	{
		if (m_Mode2D == enabled)
			return;

		m_Mode2D = enabled;
		m_CameraMode = CameraMode::ARCBALL;
		m_PitchDelta = m_YawDelta = 0.0f;
		m_PositionDelta = glm::vec3(0.0f);
		if (enabled)
		{
			m_Saved3DDistance = m_Distance;
			m_Saved3DPitch = m_Pitch;
			m_Saved3DYaw = m_Yaw;
			m_Pitch = m_Yaw = 0.0f; // identity orientation: forward -Z, up +Y
			m_FocalPoint.z = 0.0f;
			m_Distance = k_Distance2D;
		}
		else
		{
			m_Distance = m_Saved3DDistance;
			m_Pitch = m_Saved3DPitch;
			m_Yaw = m_Saved3DYaw;
		}
		m_Position = m_FocalPoint - GetForwardDirection() * m_Distance;
		RebuildProjection();
		UpdateCameraView();
	}

	static void DisableMouse()
	{
#ifdef LUX_PLATFORM_LINUX
		Input::SetCursorMode(CursorMode::Locked);
#else
		Input::SetCursorMode(CursorMode::Hidden);
#endif
		ImGuiEx::SetInputEnabled(false);
	}

	static void EnableMouse()
	{
		Input::SetCursorMode(CursorMode::Normal);
		ImGuiEx::SetInputEnabled(true);
	}

	void EditorCamera::OnUpdate(const Timestep ts)
	{
		glm::vec2 mouse{ Input::GetMouseX(), Input::GetMouseY() };
		const bool rightMouseDown = Input::IsMouseButtonDown(MouseButton::Right);
		const bool altDown = Input::IsKeyDown(KeyCode::LeftAlt);
		const bool flycamMouseCapture = rightMouseDown && !altDown;
		const bool arcballMouseCapture = altDown
			&& (Input::IsMouseButtonDown(MouseButton::Middle)
				|| Input::IsMouseButtonDown(MouseButton::Left)
				|| rightMouseDown);
		const bool pan2D = m_Mode2D && (Input::IsMouseButtonDown(MouseButton::Middle) || rightMouseDown
			|| (altDown && Input::IsMouseButtonDown(MouseButton::Left)));
		const bool cameraMouseCapture = m_IsActive && (m_Mode2D ? pan2D : (flycamMouseCapture || arcballMouseCapture));

		if (cameraMouseCapture && !m_CameraMouseCaptured)
		{
			m_InitialMousePosition = mouse;
			m_CameraMouseCaptured = true;
		}
		else if (!cameraMouseCapture)
		{
			m_CameraMouseCaptured = false;
		}

		const glm::vec2 delta = (mouse - m_InitialMousePosition) * 0.002f;

		//LUX_CORE_WARN("EditorCamera=m_IsActive{}", m_IsActive);
		if (!m_IsActive)
		{
			m_CameraMouseCaptured = false;
			// If the camera went inactive mid-capture (focus/hover lost while flying), the cursor
			// was left hidden/locked. Restore it here — otherwise it stays captured and unusable
			// until the window loses focus (which is what makes GLFW release it).
			if (Input::GetCursorMode() != CursorMode::Normal)
				EnableMouse();
			else if (!ImGuiEx::IsInputEnabled())
				ImGuiEx::SetInputEnabled(true);

			return;
		}

		if (m_Mode2D)
		{
			if (cameraMouseCapture)
			{
				DisableMouse();
				const float panelHeight = m_PanelHeight > 1.0f ? m_PanelHeight : float(m_ViewportBottom - m_ViewportTop);
				const float worldPerPixel = m_OrthoHeight / glm::max(panelHeight, 1.0f);
				const glm::vec2 pixels = mouse - m_InitialMousePosition;
				m_FocalPoint.x -= pixels.x * worldPerPixel;
				m_FocalPoint.y += pixels.y * worldPerPixel;
				WrapMousePosition(mouse);
			}
			else
				EnableMouse();

			m_InitialMousePosition = mouse;
			m_Position = m_FocalPoint - GetForwardDirection() * m_Distance;
			UpdateCameraView();
			return;
		}

		if (flycamMouseCapture)
		{
			m_CameraMode = CameraMode::FLYCAM;
			DisableMouse();
			const float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;

			const float speed = GetCameraSpeed();

			if (Input::IsKeyDown(KeyCode::Q))
				m_PositionDelta -= ts.GetMilliseconds() * speed * glm::vec3{ 0.f, yawSign, 0.f };
			if (Input::IsKeyDown(KeyCode::E))
				m_PositionDelta += ts.GetMilliseconds() * speed * glm::vec3{ 0.f, yawSign, 0.f };
			if (Input::IsKeyDown(KeyCode::S))
				m_PositionDelta -= ts.GetMilliseconds() * speed * m_Direction;
			if (Input::IsKeyDown(KeyCode::W))
				m_PositionDelta += ts.GetMilliseconds() * speed * m_Direction;
			if (Input::IsKeyDown(KeyCode::A))
				m_PositionDelta -= ts.GetMilliseconds() * speed * m_RightDirection;
			if (Input::IsKeyDown(KeyCode::D))
				m_PositionDelta += ts.GetMilliseconds() * speed * m_RightDirection;

			constexpr float maxRate{ 0.12f };
			m_YawDelta += glm::clamp(yawSign * delta.x * RotationSpeed(), -maxRate, maxRate);
			m_PitchDelta += glm::clamp(delta.y * RotationSpeed(), -maxRate, maxRate);

			m_RightDirection = glm::cross(m_Direction, glm::vec3{ 0.f, yawSign, 0.f });

			m_Direction = glm::rotate(glm::normalize(glm::cross(glm::angleAxis(-m_PitchDelta, m_RightDirection),
				glm::angleAxis(-m_YawDelta, glm::vec3{ 0.f, yawSign, 0.f }))), m_Direction);

			const float distance = glm::distance(m_FocalPoint, m_Position);
			m_FocalPoint = m_Position + GetForwardDirection() * distance;
			m_Distance = distance;
		}
		else if (altDown)
		{
			m_CameraMode = CameraMode::ARCBALL;

			if (Input::IsMouseButtonDown(MouseButton::Middle))
			{
				DisableMouse();
				MousePan(delta);
			}
			else if (Input::IsMouseButtonDown(MouseButton::Left))
			{
				DisableMouse();
				MouseRotate(delta);
			}
			else if (Input::IsMouseButtonDown(MouseButton::Right))
			{
				DisableMouse();
				MouseZoom((delta.x + delta.y) * 0.1f);
			}
			else
				EnableMouse();
		}
		else
		{
			EnableMouse();
		}

		if (cameraMouseCapture)
			WrapMousePosition(mouse);

		m_InitialMousePosition = mouse;
		m_Position += m_PositionDelta;
		m_Yaw += m_YawDelta;
		m_Pitch += m_PitchDelta;

		if (m_CameraMode == CameraMode::ARCBALL)
			m_Position = CalculatePosition();

		UpdateCameraView();
	}

	float EditorCamera::GetCameraSpeed() const
	{
		float speed = m_NormalSpeed;
		if (Input::IsKeyDown(KeyCode::LeftControl))
			speed /= 2 - glm::log(m_NormalSpeed);
		if (Input::IsKeyDown(KeyCode::LeftShift))
			speed *= 2 - glm::log(m_NormalSpeed);

		return glm::clamp(speed, MIN_SPEED, MAX_SPEED);
	}

	bool EditorCamera::WrapMousePosition(glm::vec2& mouse) const
	{
		const auto [windowWidth, windowHeight] = Application::Get().GetWindow().GetSize();
		constexpr float margin = 2.0f;
		const float maxX = (float)windowWidth - margin;
		const float maxY = (float)windowHeight - margin;

		if (maxX <= margin || maxY <= margin)
			return false;

		glm::vec2 wrappedMouse = mouse;
		if (mouse.x <= margin)
			wrappedMouse.x = maxX;
		else if (mouse.x >= maxX)
			wrappedMouse.x = margin;

		if (mouse.y <= margin)
			wrappedMouse.y = maxY;
		else if (mouse.y >= maxY)
			wrappedMouse.y = margin;

		if (wrappedMouse == mouse)
			return false;

		Input::SetMousePosition(wrappedMouse.x, wrappedMouse.y);
		mouse = wrappedMouse;
		return true;
	}

	void EditorCamera::UpdateCameraView()
	{
		const float yawSign = GetUpDirection().y < 0 ? -1.0f : 1.0f;

		// Extra step to handle the problem when the camera direction is the same as the up vector
		const float cosAngle = glm::dot(GetForwardDirection(), GetUpDirection());
		if (cosAngle * yawSign > 0.99f)
			m_PitchDelta = 0.f;

		const glm::vec3 lookAt = m_Position + GetForwardDirection();
		m_Direction = glm::normalize(lookAt - m_Position);
		m_Distance = glm::distance(m_Position, m_FocalPoint);
		// The orientation's up vector remains perpendicular at top/bottom views.
		// A fixed world-up vector makes those axis-aligned views singular.
		m_ViewMatrix = glm::lookAt(m_Position, lookAt, GetUpDirection());

		//damping for smooth camera
		m_YawDelta *= 0.6f;
		m_PitchDelta *= 0.6f;
		m_PositionDelta *= 0.8f;
	}

	void EditorCamera::Focus(const glm::vec3& focusPoint)
	{
		if (m_Mode2D)
		{
			m_FocalPoint = { focusPoint.x, focusPoint.y, 0.0f };
			m_Position = m_FocalPoint - GetForwardDirection() * m_Distance;
			UpdateCameraView();
			return;
		}

		m_FocalPoint = focusPoint;
		m_CameraMode = CameraMode::FLYCAM;
		if (m_Distance > m_MinFocusDistance)
		{
			m_Distance -= m_Distance - m_MinFocusDistance;
			m_Position = m_FocalPoint - GetForwardDirection() * m_Distance;
		}
		m_Position = m_FocalPoint - GetForwardDirection() * m_Distance;
		UpdateCameraView();
	}

	void EditorCamera::SetOrbitState(const glm::vec3& focalPoint, float distance, float pitch, float yaw)
	{
		if (m_Mode2D)
		{
			m_FocalPoint = { focalPoint.x, focalPoint.y, 0.0f };
			m_OrthoHeight = glm::clamp(distance, k_MinOrthoHeight, k_MaxOrthoHeight);
			m_Position = m_FocalPoint - GetForwardDirection() * m_Distance;
			RebuildProjection();
			UpdateCameraView();
			return;
		}

		m_FocalPoint = focalPoint;
		m_Distance = glm::max(distance, 0.01f);
		m_Pitch = pitch;
		m_Yaw = yaw;
		m_PitchDelta = 0.0f;
		m_YawDelta = 0.0f;
		m_PositionDelta = glm::vec3(0.0f);
		m_CameraMode = CameraMode::ARCBALL;
		m_Position = m_FocalPoint - GetForwardDirection() * m_Distance;
		UpdateCameraView();
	}

	std::pair<float, float> EditorCamera::PanSpeed() const
	{
		const float x = glm::min(float(m_ViewportRight - m_ViewportLeft) / 1000.0f, 2.4f); // max = 2.4f
		const float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;

		const float y = glm::min(float(m_ViewportBottom - m_ViewportTop) / 1000.0f, 2.4f); // max = 2.4f
		const float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;

		return { xFactor, yFactor };
	}

	float EditorCamera::RotationSpeed() const
	{
		return 0.3f;
	}

	float EditorCamera::ZoomSpeed() const
	{
		float distance = m_Distance * 0.2f;
		distance = glm::max(distance, 0.0f);
		float speed = distance * distance;
		speed = glm::min(speed, 50.0f); // max speed = 50
		return speed;
	}

	void EditorCamera::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<MouseScrolledEvent>([this](MouseScrolledEvent& e) { return OnMouseScroll(e); });
	}

	bool EditorCamera::OnMouseScroll(MouseScrolledEvent& e)
	{
		if (m_Mode2D)
		{
			m_OrthoHeight = glm::clamp(m_OrthoHeight * std::pow(0.85f, e.GetYOffset()), k_MinOrthoHeight, k_MaxOrthoHeight);
			RebuildProjection();
			return true;
		}

		if (Input::IsMouseButtonDown(MouseButton::Right))
		{
			m_NormalSpeed += e.GetYOffset() * 0.3f * m_NormalSpeed;
			m_NormalSpeed = std::clamp(m_NormalSpeed, MIN_SPEED, MAX_SPEED);
		}
		else
		{
			MouseZoom(e.GetYOffset() * 0.1f);
			UpdateCameraView();
		}

		return true;
	}

	void EditorCamera::MousePan(const glm::vec2& delta)
	{
		auto [xSpeed, ySpeed] = PanSpeed();
		m_FocalPoint -= GetRightDirection() * delta.x * xSpeed * m_Distance;
		m_FocalPoint += GetUpDirection() * delta.y * ySpeed * m_Distance;
	}

	void EditorCamera::MouseRotate(const glm::vec2& delta)
	{
		const float yawSign = GetUpDirection().y < 0.0f ? -1.0f : 1.0f;
		m_YawDelta += yawSign * delta.x * RotationSpeed();
		m_PitchDelta += delta.y * RotationSpeed();
	}

	void EditorCamera::MouseZoom(float delta)
	{
		m_Distance -= delta * ZoomSpeed();
		const glm::vec3 forwardDir = GetForwardDirection();
		m_Position = m_FocalPoint - forwardDir * m_Distance;
		if (m_Distance < 1.0f)
		{
			m_FocalPoint += forwardDir * m_Distance;
			m_Distance = 1.0f;
		}
		m_PositionDelta += delta * ZoomSpeed() * forwardDir;
	}

	glm::vec3 EditorCamera::GetUpDirection() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	glm::vec3 EditorCamera::GetRightDirection() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(1.f, 0.f, 0.f));
	}

	glm::vec3 EditorCamera::GetForwardDirection() const
	{
		return glm::rotate(GetOrientation(), glm::vec3(0.0f, 0.0f, -1.0f));
	}

	glm::vec3 EditorCamera::CalculatePosition() const
	{
		return m_FocalPoint - GetForwardDirection() * m_Distance + m_PositionDelta;
	}

	glm::quat EditorCamera::GetOrientation() const
	{
		return glm::quat(glm::vec3(-m_Pitch - m_PitchDelta, -m_Yaw - m_YawDelta, 0.0f));
	}
}
