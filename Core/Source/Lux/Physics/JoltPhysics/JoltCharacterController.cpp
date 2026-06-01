#include "lpch.h"
#include "JoltCharacterController.h"

#include "Lux/Scene/Components.h"

#include <glm/gtx/quaternion.hpp>

namespace Lux {

	JoltCharacterController::JoltCharacterController(Entity entity)
		: m_Entity(entity)
	{
		if (!entity || !entity.HasComponent<CharacterControllerComponent>())
			return;

		const auto& component = entity.GetComponent<CharacterControllerComponent>();
		m_SlopeLimitDeg = component.SlopeLimitDeg;
		m_StepOffset = component.StepOffset;
		m_GravityEnabled = !component.DisableGravity;
		m_ControlMovementInAir = component.ControlMovementInAir;
		m_ControlRotationInAir = component.ControlRotationInAir;
	}

	void JoltCharacterController::SetGravityEnabled(bool enableGravity)
	{
		m_GravityEnabled = enableGravity;
		if (m_Entity && m_Entity.HasComponent<CharacterControllerComponent>())
			m_Entity.GetComponent<CharacterControllerComponent>().DisableGravity = !enableGravity;
	}

	void JoltCharacterController::SetTranslation(const glm::vec3& translation)
	{
		if (m_Entity)
			m_Entity.Transform().Translation = translation;
	}

	void JoltCharacterController::SetRotation(const glm::quat& rotation)
	{
		if (m_Entity)
			m_Entity.Transform().SetRotation(rotation);
	}

	void JoltCharacterController::Move(const glm::vec3& displacement)
	{
		if (m_Entity)
			m_Entity.Transform().Translation += displacement;
	}

	void JoltCharacterController::Rotate(const glm::quat& rotation)
	{
		if (m_Entity)
			m_Entity.Transform().SetRotation(glm::normalize(m_Entity.Transform().GetRotation() * rotation));
	}

	void JoltCharacterController::Jump(float jumpPower)
	{
		if (m_IsGrounded || m_ControlMovementInAir)
		{
			m_LinearVelocity.y = jumpPower;
			m_IsGrounded = false;
			m_CollisionFlags = ECollisionFlags::None;
		}
	}

	void JoltCharacterController::Simulate(float deltaTime)
	{
		if (!m_Entity)
			return;

		if (m_GravityEnabled)
			m_LinearVelocity += glm::vec3(0.0f, -9.81f, 0.0f) * deltaTime;

		m_Entity.Transform().Translation += m_LinearVelocity * deltaTime;
		m_CollisionFlags = ECollisionFlags::None;
	}

}
