// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <glm/detail/type_quat.hpp>

#include "Lux/Renderer/Camera.h"
#include "Lux/Core/Timestep.h"
#include "Lux/Core/Events/KeyEvent.h"
#include "Lux/Core/Events/MouseEvent.h"

namespace Lux {

	enum class CameraMode
	{
		NONE, FLYCAM, ARCBALL
	};

	class EditorCamera : public Camera
	{
	public:
		EditorCamera(const float degFov, const float width, const float height, const float nearP, const float farP);
		void Init();

		void Focus(const glm::vec3& focusPoint);
		// Restore a full orbit pose (used by viewport camera bookmarks).
		void SetOrbitState(const glm::vec3& focalPoint, float distance, float pitch, float yaw);
		void OnUpdate(Timestep ts);
		void OnEvent(Event& e);

		bool IsActive() const { return m_IsActive; }
		void SetActive(bool active) { m_IsActive = active; }
		// True while a fly or orbit drag holds the mouse (cursor hidden, ImGui mouse input off).
		bool IsMouseCaptured() const { return m_CameraMouseCaptured; }

		CameraMode GetCurrentMode() const { return m_CameraMode; }

		// 2D view: orthographic, looking down -Z at the XY plane. Middle or right drag (or Alt + left drag)
		// pans, the wheel zooms. "Distance" is the visible height in world units in this mode.
		void Set2DMode(bool enabled);
		bool Is2DMode() const { return m_Mode2D; }
		// Height of the scene panel in mouse units, so a 2D pan follows the cursor exactly.
		void SetPanelHeight(float height) { m_PanelHeight = height; }

		inline float GetDistance() const { return m_Mode2D ? m_OrthoHeight : m_Distance; }
		inline void SetDistance(float distance) { if (m_Mode2D) { m_OrthoHeight = distance; RebuildProjection(); } else m_Distance = distance; }

		const glm::vec3& GetFocalPoint() const { return m_FocalPoint; }

		inline void SetViewportBounds(uint32_t left, uint32_t top, uint32_t right, uint32_t bottom)
		{
			if (m_ViewportLeft == left && m_ViewportTop == top && m_ViewportRight == right && m_ViewportBottom == bottom)
				return;

			const bool resized = (right - left) != (m_ViewportRight - m_ViewportLeft) || (bottom - top) != (m_ViewportBottom - m_ViewportTop);
			m_ViewportLeft = left;
			m_ViewportTop = top;
			m_ViewportRight = right;
			m_ViewportBottom = bottom;
			if (resized)
				RebuildProjection();
		}

		const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
		glm::mat4 GetViewProjection() const { return GetProjectionMatrix() * m_ViewMatrix; }
		glm::mat4 GetUnReversedViewProjection() const { return GetUnReversedProjectionMatrix() * m_ViewMatrix; }

		glm::vec3 GetUpDirection() const;
		glm::vec3 GetRightDirection() const;
		glm::vec3 GetForwardDirection() const;

		const glm::vec3& GetPosition() const { return m_Position; }

		glm::quat GetOrientation() const;

		[[nodiscard]] float GetVerticalFOV() const { return m_VerticalFOV; }
		[[nodiscard]] float GetAspectRatio() const { return m_AspectRatio; }
		[[nodiscard]] float GetNearClip() const { return m_NearClip; }
		[[nodiscard]] float GetFarClip() const { return m_FarClip; }
		[[nodiscard]] float GetPitch() const { return m_Pitch; }
		[[nodiscard]] float GetYaw() const { return m_Yaw; }
		[[nodiscard]] float GetCameraSpeed() const;
	private:
		void UpdateCameraView();
		// Perspective or the 2D orthographic projection for the current viewport size.
		void RebuildProjection();

		bool OnMouseScroll(MouseScrolledEvent& e);

		void MousePan(const glm::vec2& delta);
		void MouseRotate(const glm::vec2& delta);
		void MouseZoom(float delta);
		bool WrapMousePosition(glm::vec2& mouse) const;

		glm::vec3 CalculatePosition() const;

		std::pair<float, float> PanSpeed() const;
		float RotationSpeed() const;
		float ZoomSpeed() const;
	private:
		glm::mat4 m_ViewMatrix;
		glm::vec3 m_Position, m_Direction, m_FocalPoint;

		// Perspective projection params
		float m_VerticalFOV, m_AspectRatio, m_NearClip, m_FarClip;

		bool m_IsActive = false;
		bool m_Panning, m_Rotating;
		bool m_CameraMouseCaptured = false;
		glm::vec2 m_InitialMousePosition{};
		glm::vec3 m_InitialFocalPoint, m_InitialRotation;

		float m_Distance;
		float m_NormalSpeed{ 0.002f };

		float m_Pitch, m_Yaw;
		float m_PitchDelta{}, m_YawDelta{};
		glm::vec3 m_PositionDelta{};
		glm::vec3 m_RightDirection{};

		CameraMode m_CameraMode{ CameraMode::ARCBALL };

		float m_MinFocusDistance{ 100.0f };

		bool m_Mode2D = false;
		float m_OrthoHeight = 10.0f;
		float m_PanelHeight = 0.0f;
		float m_Saved3DDistance = 10.0f, m_Saved3DPitch = 0.0f, m_Saved3DYaw = 0.0f;

		uint32_t m_ViewportLeft = 0;
		uint32_t m_ViewportTop = 0;
		uint32_t m_ViewportRight = 1280;
		uint32_t m_ViewportBottom = 720;

		constexpr static float MIN_SPEED{ 0.0005f }, MAX_SPEED{ 2.0f };
		friend class EditorLayer;
	};

}
