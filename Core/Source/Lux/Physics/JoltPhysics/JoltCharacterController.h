// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Physics/CharacterController.h"
#include "Lux/Physics/PhysicsContactEvent.h"
#include "Lux/Scene/Entity.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <optional>
#include <unordered_map>
#include <vector>

namespace Lux {

	// A JPH::CharacterVirtual: collides, stands on and walks up slopes and steps, rides moving
	// platforms and pushes dynamic bodies. Its inner body (same shape and layer, user data = the
	// entity) is what rigid bodies, triggers and scene queries see.
	class JoltCharacterController final : public CharacterController, private JPH::CharacterContactListener
	{
	public:
		// `contactEvents` receives Begin/End as the character starts and stops touching bodies (PhysicsScene
		// owns it; the rigid-body contact listener never sees the character's own sweeps).
		JoltCharacterController(Entity entity, JPH::PhysicsSystem& system, JPH::RefConst<JPH::Shape> shape, JPH::ObjectLayer layer,
			std::vector<PhysicsContactEvent>* contactEvents);
		~JoltCharacterController();

		// Removes the character and its inner body from the system, ending its contacts; call before the
		// system goes.
		void Destroy();

		virtual void SetGravityEnabled(bool enableGravity) override;
		virtual bool IsGravityEnabled() const override { return m_GravityEnabled; }
		virtual void SetSlopeLimit(float slopeLimit) override;
		virtual void SetStepOffset(float stepOffset) override { m_StepOffset = stepOffset; }
		virtual void SetTranslation(const glm::vec3& translation) override;
		virtual void SetRotation(const glm::quat& rotation) override;
		virtual bool IsGrounded() const override { return m_Character && m_Character->IsSupported(); }
		virtual void SetControlMovementInAir(bool controlMovementInAir) override { m_ControlMovementInAir = controlMovementInAir; }
		virtual bool CanControlMovementInAir() const override { return m_ControlMovementInAir; }
		virtual void SetControlRotationInAir(bool controlRotationInAir) override { m_ControlRotationInAir = controlRotationInAir; }
		virtual bool CanControlRotationInAir() const override { return m_ControlRotationInAir; }
		virtual ECollisionFlags GetCollisionFlags() const override { return m_CollisionFlags; }
		virtual void Move(const glm::vec3& displacement) override;
		virtual void Rotate(const glm::quat& rotation) override;
		virtual void Jump(float jumpPower) override;
		virtual glm::vec3 GetLinearVelocity() const override;
		virtual void SetLinearVelocity(const glm::vec3& velocity) override { m_VelocityOverride = velocity; }

	private:
		virtual void Simulate(float deltaTime) override;
		virtual void PostSimulate() override;

		// JPH::CharacterContactListener: an idle character does not creep down a walkable slope.
		void OnContactSolve(const JPH::CharacterVirtual* character, const JPH::BodyID& bodyID2, const JPH::SubShapeID& subShapeID2,
			JPH::RVec3Arg contactPosition, JPH::Vec3Arg contactNormal, JPH::Vec3Arg contactVelocity, const JPH::PhysicsMaterial* contactMaterial,
			JPH::Vec3Arg characterVelocity, JPH::Vec3& ioNewCharacterVelocity) override;

	private:
		Entity m_Entity;
		JPH::PhysicsSystem& m_System;
		JPH::Ref<JPH::CharacterVirtual> m_Character;
		JPH::ObjectLayer m_Layer = 0;

		// Requests from scripts, applied at the next step.
		glm::vec3 m_Displacement = glm::vec3(0.0f);
		glm::quat m_Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		std::optional<glm::vec3> m_VelocityOverride;
		float m_JumpPower = 0.0f;

		float m_StepOffset = 0.5f;
		bool m_GravityEnabled = true;
		bool m_ControlMovementInAir = false;
		bool m_ControlRotationInAir = false;
		bool m_AllowSliding = false;
		ECollisionFlags m_CollisionFlags = ECollisionFlags::None;

		std::vector<PhysicsContactEvent>* m_ContactEvents = nullptr;
		struct Touch
		{
			UUID Entity = 0;
			uint32_t StepsApart = 0;
		};
		std::unordered_map<uint32_t, Touch> m_Touching; // by body index and sequence
	};

}
