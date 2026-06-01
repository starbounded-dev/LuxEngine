#pragma once

#include "Lux/Physics/PhysicsBody.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

namespace JPH {
	class BodyInterface;
	class BodyLockInterface;
	class SixDOFConstraint;
}

namespace Lux {

	class JoltBody final : public PhysicsBody
	{
	public:
		JoltBody(Entity entity, JPH::BodyInterface& bodyInterface, const JPH::BodyLockInterface& bodyLockInterface, JPH::BodyID bodyID);
		~JoltBody();

		virtual void SetCollisionLayer(uint32_t layerID) override;
		virtual bool IsStatic() const override;
		virtual bool IsDynamic() const override;
		virtual bool IsKinematic() const override;
		virtual void MoveKinematic(const glm::vec3& targetPosition, const glm::quat& targetRotation, float deltaSeconds) override;
		virtual void Rotate(const glm::vec3& rotationDelta) override;
		virtual void SetGravityEnabled(bool enabled) override;
		virtual void AddForce(const glm::vec3& force, EForceMode forceMode = EForceMode::Force, bool forceWake = true) override;
		virtual void AddForce(const glm::vec3& force, const glm::vec3& location, EForceMode forceMode = EForceMode::Force, bool forceWake = true) override;
		virtual void AddTorque(const glm::vec3& torque, bool forceWake = true) override;
		virtual void AddRadialImpulse(const glm::vec3& origin, float radius, float strength, EFalloffMode falloff, bool velocityChange) override;
		virtual void ChangeTriggerState(bool isTrigger) override;
		virtual bool IsTrigger() const override;
		virtual float GetMass() const override;
		virtual void SetMass(float mass) override;
		virtual void SetLinearDrag(float linearDrag) override;
		virtual void SetAngularDrag(float angularDrag) override;
		virtual glm::vec3 GetLinearVelocity() const override;
		virtual void SetLinearVelocity(const glm::vec3& velocity) override;
		virtual glm::vec3 GetAngularVelocity() const override;
		virtual void SetAngularVelocity(const glm::vec3& velocity) override;
		virtual float GetMaxLinearVelocity() const override;
		virtual void SetMaxLinearVelocity(float velocity) override;
		virtual float GetMaxAngularVelocity() const override;
		virtual void SetMaxAngularVelocity(float velocity) override;
		virtual bool IsSleeping() const override;
		virtual void SetSleepState(bool sleep) override;
		virtual void SetCollisionDetectionMode(ECollisionDetectionType mode) override;
		virtual glm::vec3 GetTranslation() const override;
		virtual glm::quat GetRotation() const override;
		virtual void SetTranslation(const glm::vec3& translation) override;
		virtual void SetRotation(const glm::quat& rotation) override;

		JPH::BodyID GetBodyID() const { return m_BodyID; }

	private:
		virtual void OnAxisLockUpdated(bool forceWake) override;
		void DestroyAxisLockConstraint();
		void CreateAxisLockConstraint(bool forceWake);

	private:
		JPH::BodyInterface* m_BodyInterface = nullptr;
		const JPH::BodyLockInterface* m_BodyLockInterface = nullptr;
		JPH::BodyID m_BodyID;
		JPH::SixDOFConstraint* m_AxisLockConstraint = nullptr;
		bool m_IsTrigger = false;
	};

}
