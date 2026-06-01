#pragma once

#include "Lux/Physics/CharacterController.h"
#include "Lux/Scene/Entity.h"

namespace Lux {

	class JoltCharacterController final : public CharacterController
	{
	public:
		explicit JoltCharacterController(Entity entity);

		virtual void SetGravityEnabled(bool enableGravity) override;
		virtual bool IsGravityEnabled() const override { return m_GravityEnabled; }
		virtual void SetSlopeLimit(float slopeLimit) override { m_SlopeLimitDeg = slopeLimit; }
		virtual void SetStepOffset(float stepOffset) override { m_StepOffset = stepOffset; }
		virtual void SetTranslation(const glm::vec3& translation) override;
		virtual void SetRotation(const glm::quat& rotation) override;
		virtual bool IsGrounded() const override { return m_IsGrounded; }
		virtual void SetControlMovementInAir(bool controlMovementInAir) override { m_ControlMovementInAir = controlMovementInAir; }
		virtual bool CanControlMovementInAir() const override { return m_ControlMovementInAir; }
		virtual void SetControlRotationInAir(bool controlRotationInAir) override { m_ControlRotationInAir = controlRotationInAir; }
		virtual bool CanControlRotationInAir() const override { return m_ControlRotationInAir; }
		virtual ECollisionFlags GetCollisionFlags() const override { return m_CollisionFlags; }
		virtual void Move(const glm::vec3& displacement) override;
		virtual void Rotate(const glm::quat& rotation) override;
		virtual void Jump(float jumpPower) override;
		virtual glm::vec3 GetLinearVelocity() const override { return m_LinearVelocity; }
		virtual void SetLinearVelocity(const glm::vec3& velocity) override { m_LinearVelocity = velocity; }

	private:
		virtual void Simulate(float deltaTime) override;

	private:
		Entity m_Entity;
		glm::vec3 m_LinearVelocity = glm::vec3(0.0f);
		float m_SlopeLimitDeg = 45.0f;
		float m_StepOffset = 0.5f;
		bool m_GravityEnabled = true;
		bool m_ControlMovementInAir = false;
		bool m_ControlRotationInAir = false;
		bool m_IsGrounded = false;
		ECollisionFlags m_CollisionFlags = ECollisionFlags::None;
	};

}
