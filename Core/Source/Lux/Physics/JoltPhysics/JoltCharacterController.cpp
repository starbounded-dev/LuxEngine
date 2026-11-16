// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "JoltCharacterController.h"

#include "Lux/Physics/JoltPhysics/JoltUtils.h"
#include "Lux/Physics/PhysicsSystem.h"
#include "Lux/Scene/Scene.h"

#include <Jolt/Physics/PhysicsSystem.h>

#include <glm/gtx/quaternion.hpp>

namespace Lux {

	using namespace JoltUtils;

	JoltCharacterController::JoltCharacterController(Entity entity, JPH::PhysicsSystem& system, JPH::RefConst<JPH::Shape> shape, JPH::ObjectLayer layer,
		std::vector<PhysicsContactEvent>* contactEvents)
		: m_Entity(entity), m_System(system), m_Layer(layer), m_ContactEvents(contactEvents)
	{
		const auto& component = entity.GetComponent<CharacterControllerComponent>();
		m_StepOffset = component.StepOffset;
		m_GravityEnabled = !component.DisableGravity;
		m_ControlMovementInAir = component.ControlMovementInAir;
		m_ControlRotationInAir = component.ControlRotationInAir;

		JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
		settings->mShape = shape;
		settings->mMaxSlopeAngle = glm::radians(component.SlopeLimitDeg);
		// The inner body is what rigid bodies, triggers and raycasts collide with.
		settings->mInnerBodyShape = shape;
		settings->mInnerBodyLayer = layer;

		const TransformComponent world = entity.GetScene()->GetWorldSpaceTransform(entity);
		m_Character = new JPH::CharacterVirtual(settings, ToJoltRVec3(world.Translation), ToJoltQuat(glm::normalize(world.GetRotation())),
			(JPH::uint64)entity.GetUUID(), &system);
		m_Character->SetListener(this);
	}

	JoltCharacterController::~JoltCharacterController()
	{
		Destroy();
	}

	// Keyed apart from rigid-body contacts (sub-shape IDs ~0), so a dynamic body touching both the
	// character and its inner body counts as one entity pair, twice, and ends when both do.
	static PhysicsContactEvent CharacterContact(PhysicsContactEvent::Type state, uint32_t character, uint32_t body, UUID self, UUID other)
	{
		PhysicsContactEvent event;
		event.State = state;
		event.Key = { character, body, ~0u, ~0u };
		event.Entity1 = self;
		event.Entity2 = other;
		return event;
	}

	void JoltCharacterController::Destroy()
	{
		if (m_Character && m_ContactEvents)
		{
			const uint32_t self = m_Character->GetInnerBodyID().GetIndexAndSequenceNumber();
			for (const auto& [body, touch] : m_Touching)
				m_ContactEvents->push_back(CharacterContact(PhysicsContactEvent::Type::End, self, body, m_Entity ? m_Entity.GetUUID() : UUID(0), touch.Entity));
		}
		m_Touching.clear();
		m_Character = nullptr;
	}

	void JoltCharacterController::SetGravityEnabled(bool enableGravity)
	{
		m_GravityEnabled = enableGravity;
		if (m_Entity && m_Entity.HasComponent<CharacterControllerComponent>())
			m_Entity.GetComponent<CharacterControllerComponent>().DisableGravity = !enableGravity;
	}

	void JoltCharacterController::SetSlopeLimit(float slopeLimit)
	{
		if (m_Character)
			m_Character->SetMaxSlopeAngle(glm::radians(slopeLimit));
	}

	void JoltCharacterController::SetTranslation(const glm::vec3& translation)
	{
		if (m_Character)
			m_Character->SetPosition(ToJoltRVec3(translation));
	}

	void JoltCharacterController::SetRotation(const glm::quat& rotation)
	{
		if (m_Character)
			m_Character->SetRotation(ToJoltQuat(glm::normalize(rotation)));
	}

	void JoltCharacterController::Move(const glm::vec3& displacement)
	{
		if (IsGrounded() || m_ControlMovementInAir)
			m_Displacement += displacement;
	}

	void JoltCharacterController::Rotate(const glm::quat& rotation)
	{
		if (IsGrounded() || m_ControlRotationInAir)
			m_Rotation = glm::normalize(m_Rotation * rotation);
	}

	void JoltCharacterController::Jump(float jumpPower)
	{
		m_JumpPower = jumpPower;
	}

	glm::vec3 JoltCharacterController::GetLinearVelocity() const
	{
		return m_Character ? FromJoltVector(m_Character->GetLinearVelocity()) : glm::vec3(0.0f);
	}

	// Jolt's CharacterVirtual recipe (Samples/Tests/Character/CharacterVirtualTest.cpp): ground
	// velocity, jump, gravity and the player's movement make the velocity, then ExtendedUpdate moves the
	// character (sliding along walls, walking up steps, sticking to the floor going down).
	void JoltCharacterController::Simulate(float deltaTime)
	{
		if (!m_Character || deltaTime <= 0.0f)
			return;

		m_Character->UpdateGroundVelocity();

		const JPH::Vec3 gravity = m_System.GetGravity();
		const JPH::Vec3 up = m_Character->GetUp();
		// SetLinearVelocity from a script replaces the current velocity for this step.
		const JPH::Vec3 currentVelocity = m_VelocityOverride ? ToJoltVector(*m_VelocityOverride) : m_Character->GetLinearVelocity();
		const bool overridden = m_VelocityOverride.has_value();
		m_VelocityOverride.reset();

		const JPH::Vec3 verticalVelocity = currentVelocity.Dot(up) * up;
		const JPH::Vec3 horizontalVelocity = currentVelocity - verticalVelocity;
		const JPH::Vec3 groundVelocity = m_Character->GetGroundVelocity();
		const bool movingTowardsGround = (verticalVelocity - groundVelocity).Dot(up) < 0.1f;
		const bool standing = m_Character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround && movingTowardsGround
			&& !m_Character->IsSlopeTooSteep(m_Character->GetGroundNormal());

		JPH::Vec3 velocity;
		if (!m_GravityEnabled)
			velocity = overridden ? verticalVelocity : JPH::Vec3::sZero(); // flying: no fall, no drift
		else if (standing)
			velocity = groundVelocity + (m_JumpPower > 0.0f ? m_JumpPower * up : JPH::Vec3::sZero()); // ride the ground, jump from it
		else
			velocity = verticalVelocity + gravity * deltaTime;

		// Move() is this step's movement. Under gravity only its horizontal part counts (height comes
		// from jumping and falling); flying, all of it does.
		const JPH::Vec3 desired = ToJoltVector(m_Displacement / deltaTime);
		const JPH::Vec3 movement = m_GravityEnabled ? desired - desired.Dot(up) * up : desired;
		if (m_Character->IsSupported() || m_ControlMovementInAir)
			velocity += movement + (overridden ? horizontalVelocity : JPH::Vec3::sZero());
		else
			velocity += horizontalVelocity; // keep the horizontal velocity through the air
		m_Character->SetLinearVelocity(velocity);

		if (m_Rotation != glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
			m_Character->SetRotation((m_Character->GetRotation() * ToJoltQuat(m_Rotation)).Normalized());

		m_AllowSliding = !m_Character->IsSupported() || !desired.IsNearZero();

		JPH::CharacterVirtual::ExtendedUpdateSettings settings;
		settings.mWalkStairsStepUp = m_StepOffset * up;
		m_Character->ExtendedUpdate(deltaTime, m_GravityEnabled ? gravity : JPH::Vec3::sZero(), settings,
			m_System.GetDefaultBroadPhaseLayerFilter(m_Layer), m_System.GetDefaultLayerFilter(m_Layer), {}, {}, *PhysicsSystem::GetTempAllocator());

		m_CollisionFlags = ECollisionFlags::None;
		// Touching: hit this step, or resting within the padding. Pushing a crate otherwise reads as a
		// new contact every few steps, as the push opens a gap the next step closes.
		const float touchDistance = 2.0f * m_Character->GetCharacterPadding();
		std::unordered_map<uint32_t, UUID> touching;
		for (const JPH::CharacterContact& contact : m_Character->GetActiveContacts())
		{
			if (contact.mWasDiscarded || (!contact.mHadCollision && contact.mDistance > touchDistance))
				continue;
			if (contact.mHadCollision)
			{
				const float facing = contact.mContactNormal.Dot(up); // the normal points towards the character
				const ECollisionFlags flag = facing > 0.5f ? ECollisionFlags::Below : facing < -0.5f ? ECollisionFlags::Above : ECollisionFlags::Sides;
				m_CollisionFlags = (ECollisionFlags)((uint8_t)m_CollisionFlags | (uint8_t)flag);
			}
			if (!contact.mBodyB.IsInvalid() && !contact.mIsSensorB)
				touching.emplace(contact.mBodyB.GetIndexAndSequenceNumber(), (UUID)m_System.GetBodyInterface().GetUserData(contact.mBodyB));
		}

		// Begin when a body is first touched; End once it has gone untouched for a few steps (50 ms at
		// 60 Hz), so a contact that opens and closes while pushing stays one contact.
		constexpr uint32_t k_ContactGraceSteps = 3;
		const uint32_t self = m_Character->GetInnerBodyID().GetIndexAndSequenceNumber();
		const UUID selfEntity = m_Entity.GetUUID();
		for (const auto& [body, entity] : touching)
		{
			const auto [touch, began] = m_Touching.try_emplace(body, Touch{ entity, 0 });
			touch->second.StepsApart = 0;
			if (began && m_ContactEvents)
				m_ContactEvents->push_back(CharacterContact(PhysicsContactEvent::Type::Begin, self, body, selfEntity, entity));
		}
		for (auto touch = m_Touching.begin(); touch != m_Touching.end();)
		{
			if (touching.contains(touch->first) || ++touch->second.StepsApart <= k_ContactGraceSteps)
			{
				++touch;
				continue;
			}
			if (m_ContactEvents)
				m_ContactEvents->push_back(CharacterContact(PhysicsContactEvent::Type::End, self, touch->first, selfEntity, touch->second.Entity));
			touch = m_Touching.erase(touch);
		}

		// One step uses up the requests; ones made in the air without air control are dropped.
		m_Displacement = glm::vec3(0.0f);
		m_Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		m_JumpPower = 0.0f;
	}

	void JoltCharacterController::PostSimulate()
	{
		if (!m_Character || !m_Entity)
			return;

		auto& transform = m_Entity.Transform();
		const glm::vec3 localScale = transform.Scale;
		glm::mat4 worldTransform = glm::translate(glm::mat4(1.0f), FromJoltRVector(m_Character->GetPosition()))
			* glm::toMat4(FromJoltQuat(m_Character->GetRotation()))
			* glm::scale(glm::mat4(1.0f), localScale);
		if (Entity parent = m_Entity.GetParent())
			worldTransform = glm::inverse(m_Entity.GetScene()->GetWorldSpaceTransformMatrix(parent)) * worldTransform;

		transform.SetTransform(worldTransform);
		transform.Scale = localScale;
	}

	void JoltCharacterController::OnContactSolve(const JPH::CharacterVirtual* character, const JPH::BodyID&, const JPH::SubShapeID&,
		JPH::RVec3Arg, JPH::Vec3Arg contactNormal, JPH::Vec3Arg contactVelocity, const JPH::PhysicsMaterial*, JPH::Vec3Arg, JPH::Vec3& ioNewCharacterVelocity)
	{
		if (!m_AllowSliding && contactVelocity.IsNearZero() && !character->IsSlopeTooSteep(contactNormal))
			ioNewCharacterVelocity = JPH::Vec3::sZero();
	}

}
