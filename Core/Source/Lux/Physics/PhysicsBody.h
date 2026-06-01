#pragma once

#include "Lux/Physics/PhysicsShapes.h"

#include <unordered_map>

namespace Lux {

	using ShapeArray = std::vector<Ref<PhysicsShape>>;

	class PhysicsBody : public RefCounted
	{
	public:
		explicit PhysicsBody(Entity entity);
		virtual ~PhysicsBody() = default;

		Entity GetEntity() { return m_Entity; }
		const Entity& GetEntity() const { return m_Entity; }

		uint32_t GetShapeCount(ShapeType type) const;
		const ShapeArray& GetShapes(ShapeType type) const;
		Ref<PhysicsShape> GetShape(ShapeType type, uint32_t index) const;

		virtual void SetCollisionLayer(uint32_t layerID) = 0;

		virtual bool IsStatic() const = 0;
		virtual bool IsDynamic() const = 0;
		virtual bool IsKinematic() const = 0;

		virtual void MoveKinematic(const glm::vec3& targetPosition, const glm::quat& targetRotation, float deltaSeconds) = 0;
		virtual void Rotate(const glm::vec3& rotationDelta) = 0;

		virtual void SetGravityEnabled(bool enabled) = 0;
		virtual void AddForce(const glm::vec3& force, EForceMode forceMode = EForceMode::Force, bool forceWake = true) = 0;
		virtual void AddForce(const glm::vec3& force, const glm::vec3& location, EForceMode forceMode = EForceMode::Force, bool forceWake = true) = 0;
		virtual void AddTorque(const glm::vec3& torque, bool forceWake = true) = 0;
		virtual void AddRadialImpulse(const glm::vec3& origin, float radius, float strength, EFalloffMode falloff, bool velocityChange) = 0;

		virtual void ChangeTriggerState(bool isTrigger) = 0;
		virtual bool IsTrigger() const = 0;

		virtual float GetMass() const = 0;
		virtual void SetMass(float mass) = 0;
		virtual void SetLinearDrag(float linearDrag) = 0;
		virtual void SetAngularDrag(float angularDrag) = 0;

		virtual glm::vec3 GetLinearVelocity() const = 0;
		virtual void SetLinearVelocity(const glm::vec3& velocity) = 0;
		virtual glm::vec3 GetAngularVelocity() const = 0;
		virtual void SetAngularVelocity(const glm::vec3& velocity) = 0;

		virtual float GetMaxLinearVelocity() const = 0;
		virtual void SetMaxLinearVelocity(float velocity) = 0;
		virtual float GetMaxAngularVelocity() const = 0;
		virtual void SetMaxAngularVelocity(float velocity) = 0;

		virtual bool IsSleeping() const = 0;
		virtual void SetSleepState(bool sleep) = 0;
		virtual void SetCollisionDetectionMode(ECollisionDetectionType mode) = 0;

		void SetAxisLock(EActorAxis axis, bool locked, bool forceWake);
		bool IsAxisLocked(EActorAxis axis) const;
		EActorAxis GetLockedAxes() const { return m_LockedAxes; }
		bool IsAllRotationLocked() const;

		virtual glm::vec3 GetTranslation() const = 0;
		virtual glm::quat GetRotation() const = 0;
		virtual void SetTranslation(const glm::vec3& translation) = 0;
		virtual void SetRotation(const glm::quat& rotation) = 0;

	protected:
		void CreateCollisionShapesForEntity(Entity entity, bool ignoreCompoundShapes = false);

	private:
		virtual void OnAxisLockUpdated(bool forceWake) = 0;

	protected:
		Entity m_Entity;
		std::unordered_map<ShapeType, ShapeArray> m_Shapes;
		EActorAxis m_LockedAxes = EActorAxis::None;

	private:
		inline static ShapeArray s_EmptyShapes;
	};

}
