// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Ref.h"
#include "Lux/Physics/PhysicsTypes.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Lux {

	class CharacterController : public RefCounted
	{
	public:
		virtual ~CharacterController() = default;

		virtual void SetGravityEnabled(bool enableGravity) = 0;
		virtual bool IsGravityEnabled() const = 0;
		virtual void SetSlopeLimit(float slopeLimit) = 0;
		virtual void SetStepOffset(float stepOffset) = 0;
		virtual void SetTranslation(const glm::vec3& translation) = 0;
		virtual void SetRotation(const glm::quat& rotation) = 0;
		virtual bool IsGrounded() const = 0;
		virtual void SetControlMovementInAir(bool controlMovementInAir) = 0;
		virtual bool CanControlMovementInAir() const = 0;
		virtual void SetControlRotationInAir(bool controlRotationInAir) = 0;
		virtual bool CanControlRotationInAir() const = 0;
		virtual ECollisionFlags GetCollisionFlags() const = 0;
		virtual void Move(const glm::vec3& displacement) = 0;
		virtual void Rotate(const glm::quat& rotation) = 0;
		virtual void Jump(float jumpPower) = 0;
		virtual glm::vec3 GetLinearVelocity() const = 0;
		virtual void SetLinearVelocity(const glm::vec3& velocity) = 0;

	private:
		// One fixed step, in Jolt's recommended order: Simulate moves the character before the world
		// steps (so a platform's velocity this step is the one it carries the character with), and
		// PostSimulate writes the result to the entity afterwards.
		virtual void Simulate(float deltaTime) = 0;
		virtual void PostSimulate() {}

		friend class PhysicsScene;
	};

}
