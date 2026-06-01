#include "lpch.h"
#include "JoltBody.h"

#include "JoltUtils.h"

#include "Lux/Scene/Components.h"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>

namespace Lux {

	namespace {

		static JPH::EMotionType ToJoltMotionType(EBodyType bodyType)
		{
			switch (bodyType)
			{
				case EBodyType::Static: return JPH::EMotionType::Static;
				case EBodyType::Dynamic: return JPH::EMotionType::Dynamic;
				case EBodyType::Kinematic: return JPH::EMotionType::Kinematic;
			}
			return JPH::EMotionType::Static;
		}

	}

	JoltBody::JoltBody(Entity entity, JPH::BodyInterface& bodyInterface, const JPH::BodyLockInterface& bodyLockInterface, JPH::BodyID bodyID)
		: PhysicsBody(entity), m_BodyInterface(&bodyInterface), m_BodyLockInterface(&bodyLockInterface), m_BodyID(bodyID)
	{
		if (entity && entity.HasComponent<RigidBodyComponent>())
			m_IsTrigger = entity.GetComponent<RigidBodyComponent>().IsTrigger;
		CreateCollisionShapesForEntity(entity);
	}

	JoltBody::~JoltBody()
	{
		DestroyAxisLockConstraint();
	}

	void JoltBody::SetCollisionLayer(uint32_t layerID)
	{
		if (m_BodyInterface && !m_BodyID.IsInvalid())
			m_BodyInterface->SetObjectLayer(m_BodyID, (JPH::ObjectLayer)layerID);
	}

	bool JoltBody::IsStatic() const
	{
		JPH::BodyLockRead lock(*m_BodyLockInterface, m_BodyID);
		return lock.Succeeded() && lock.GetBody().IsStatic();
	}

	bool JoltBody::IsDynamic() const
	{
		JPH::BodyLockRead lock(*m_BodyLockInterface, m_BodyID);
		return lock.Succeeded() && lock.GetBody().IsDynamic();
	}

	bool JoltBody::IsKinematic() const
	{
		JPH::BodyLockRead lock(*m_BodyLockInterface, m_BodyID);
		return lock.Succeeded() && lock.GetBody().IsKinematic();
	}

	void JoltBody::MoveKinematic(const glm::vec3& targetPosition, const glm::quat& targetRotation, float deltaSeconds)
	{
		if (m_BodyInterface)
			m_BodyInterface->MoveKinematic(m_BodyID, JoltUtils::ToJoltRVec3(targetPosition), JoltUtils::ToJoltQuat(targetRotation), deltaSeconds);
	}

	void JoltBody::Rotate(const glm::vec3& rotationDelta)
	{
		SetRotation(glm::normalize(GetRotation() * glm::quat(rotationDelta)));
	}

	void JoltBody::SetGravityEnabled(bool enabled)
	{
		JPH::BodyLockWrite lock(*m_BodyLockInterface, m_BodyID);
		if (lock.Succeeded() && lock.GetBody().IsDynamic())
			lock.GetBody().GetMotionProperties()->SetGravityFactor(enabled ? 1.0f : 0.0f);
	}

	void JoltBody::AddForce(const glm::vec3& force, EForceMode forceMode, bool forceWake)
	{
		if (!m_BodyInterface)
			return;
		if (forceWake)
			m_BodyInterface->ActivateBody(m_BodyID);

		const JPH::Vec3 value = JoltUtils::ToJoltVector(force);
		if (forceMode == EForceMode::Impulse || forceMode == EForceMode::VelocityChange)
			m_BodyInterface->AddImpulse(m_BodyID, value);
		else
			m_BodyInterface->AddForce(m_BodyID, value);
	}

	void JoltBody::AddForce(const glm::vec3& force, const glm::vec3& location, EForceMode forceMode, bool forceWake)
	{
		if (!m_BodyInterface)
			return;
		if (forceWake)
			m_BodyInterface->ActivateBody(m_BodyID);

		const JPH::Vec3 value = JoltUtils::ToJoltVector(force);
		if (forceMode == EForceMode::Impulse || forceMode == EForceMode::VelocityChange)
			m_BodyInterface->AddImpulse(m_BodyID, value, JoltUtils::ToJoltRVec3(location));
		else
			m_BodyInterface->AddForce(m_BodyID, value, JoltUtils::ToJoltRVec3(location));
	}

	void JoltBody::AddTorque(const glm::vec3& torque, bool forceWake)
	{
		if (!m_BodyInterface)
			return;
		if (forceWake)
			m_BodyInterface->ActivateBody(m_BodyID);
		m_BodyInterface->AddTorque(m_BodyID, JoltUtils::ToJoltVector(torque));
	}

	void JoltBody::AddRadialImpulse(const glm::vec3& origin, float radius, float strength, EFalloffMode falloff, bool velocityChange)
	{
		const glm::vec3 translation = GetTranslation();
		glm::vec3 direction = translation - origin;
		const float distance = glm::length(direction);
		if (distance <= 0.0001f || distance > radius)
			return;

		direction /= distance;
		const float attenuation = falloff == EFalloffMode::Linear ? 1.0f - glm::clamp(distance / radius, 0.0f, 1.0f) : 1.0f;
		AddForce(direction * strength * attenuation, velocityChange ? EForceMode::VelocityChange : EForceMode::Impulse);
	}

	void JoltBody::ChangeTriggerState(bool isTrigger)
	{
		m_IsTrigger = isTrigger;
		if (m_BodyInterface && !m_BodyID.IsInvalid())
			m_BodyInterface->SetIsSensor(m_BodyID, isTrigger);
	}

	bool JoltBody::IsTrigger() const
	{
		return m_IsTrigger;
	}

	float JoltBody::GetMass() const
	{
		JPH::BodyLockRead lock(*m_BodyLockInterface, m_BodyID);
		if (!lock.Succeeded() || !lock.GetBody().IsDynamic())
			return 0.0f;
		return 1.0f / lock.GetBody().GetMotionProperties()->GetInverseMass();
	}

	void JoltBody::SetMass(float mass)
	{
		JPH::BodyLockWrite lock(*m_BodyLockInterface, m_BodyID);
		if (lock.Succeeded() && lock.GetBody().IsDynamic() && mass > 0.0f)
			lock.GetBody().GetMotionProperties()->SetInverseMass(1.0f / mass);
	}

	void JoltBody::SetLinearDrag(float linearDrag)
	{
		JPH::BodyLockWrite lock(*m_BodyLockInterface, m_BodyID);
		if (lock.Succeeded() && lock.GetBody().IsDynamic())
			lock.GetBody().GetMotionProperties()->SetLinearDamping(linearDrag);
	}

	void JoltBody::SetAngularDrag(float angularDrag)
	{
		JPH::BodyLockWrite lock(*m_BodyLockInterface, m_BodyID);
		if (lock.Succeeded() && lock.GetBody().IsDynamic())
			lock.GetBody().GetMotionProperties()->SetAngularDamping(angularDrag);
	}

	glm::vec3 JoltBody::GetLinearVelocity() const
	{
		return m_BodyInterface ? JoltUtils::FromJoltVector(m_BodyInterface->GetLinearVelocity(m_BodyID)) : glm::vec3(0.0f);
	}

	void JoltBody::SetLinearVelocity(const glm::vec3& velocity)
	{
		if (m_BodyInterface)
			m_BodyInterface->SetLinearVelocity(m_BodyID, JoltUtils::ToJoltVector(velocity));
	}

	glm::vec3 JoltBody::GetAngularVelocity() const
	{
		return m_BodyInterface ? JoltUtils::FromJoltVector(m_BodyInterface->GetAngularVelocity(m_BodyID)) : glm::vec3(0.0f);
	}

	void JoltBody::SetAngularVelocity(const glm::vec3& velocity)
	{
		if (m_BodyInterface)
			m_BodyInterface->SetAngularVelocity(m_BodyID, JoltUtils::ToJoltVector(velocity));
	}

	float JoltBody::GetMaxLinearVelocity() const
	{
		JPH::BodyLockRead lock(*m_BodyLockInterface, m_BodyID);
		return lock.Succeeded() && lock.GetBody().IsDynamic() ? lock.GetBody().GetMotionProperties()->GetMaxLinearVelocity() : 0.0f;
	}

	void JoltBody::SetMaxLinearVelocity(float velocity)
	{
		JPH::BodyLockWrite lock(*m_BodyLockInterface, m_BodyID);
		if (lock.Succeeded() && lock.GetBody().IsDynamic())
			lock.GetBody().GetMotionProperties()->SetMaxLinearVelocity(velocity);
	}

	float JoltBody::GetMaxAngularVelocity() const
	{
		JPH::BodyLockRead lock(*m_BodyLockInterface, m_BodyID);
		return lock.Succeeded() && lock.GetBody().IsDynamic() ? lock.GetBody().GetMotionProperties()->GetMaxAngularVelocity() : 0.0f;
	}

	void JoltBody::SetMaxAngularVelocity(float velocity)
	{
		JPH::BodyLockWrite lock(*m_BodyLockInterface, m_BodyID);
		if (lock.Succeeded() && lock.GetBody().IsDynamic())
			lock.GetBody().GetMotionProperties()->SetMaxAngularVelocity(velocity);
	}

	bool JoltBody::IsSleeping() const
	{
		return m_BodyInterface ? !m_BodyInterface->IsActive(m_BodyID) : true;
	}

	void JoltBody::SetSleepState(bool sleep)
	{
		if (!m_BodyInterface)
			return;
		if (sleep)
			m_BodyInterface->DeactivateBody(m_BodyID);
		else
			m_BodyInterface->ActivateBody(m_BodyID);
	}

	void JoltBody::SetCollisionDetectionMode(ECollisionDetectionType mode)
	{
		if (m_BodyInterface)
			m_BodyInterface->SetMotionQuality(m_BodyID, mode == ECollisionDetectionType::Continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete);
	}

	glm::vec3 JoltBody::GetTranslation() const
	{
		JPH::BodyLockRead lock(*m_BodyLockInterface, m_BodyID);
		return lock.Succeeded() ? JoltUtils::FromJoltRVector(lock.GetBody().GetPosition()) : glm::vec3(0.0f);
	}

	glm::quat JoltBody::GetRotation() const
	{
		JPH::BodyLockRead lock(*m_BodyLockInterface, m_BodyID);
		return lock.Succeeded() ? JoltUtils::FromJoltQuat(lock.GetBody().GetRotation()) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	}

	void JoltBody::SetTranslation(const glm::vec3& translation)
	{
		if (m_BodyInterface)
			m_BodyInterface->SetPosition(m_BodyID, JoltUtils::ToJoltRVec3(translation), JPH::EActivation::Activate);
	}

	void JoltBody::SetRotation(const glm::quat& rotation)
	{
		if (m_BodyInterface)
			m_BodyInterface->SetRotation(m_BodyID, JoltUtils::ToJoltQuat(rotation), JPH::EActivation::Activate);
	}

	void JoltBody::OnAxisLockUpdated(bool forceWake)
	{
		DestroyAxisLockConstraint();
		CreateAxisLockConstraint(forceWake);
	}

	void JoltBody::DestroyAxisLockConstraint()
	{
		// Constraint ownership is handled by the scene in the current Lux implementation.
		m_AxisLockConstraint = nullptr;
	}

	void JoltBody::CreateAxisLockConstraint(bool forceWake)
	{
		if (forceWake && m_BodyInterface)
			m_BodyInterface->ActivateBody(m_BodyID);
	}

}
