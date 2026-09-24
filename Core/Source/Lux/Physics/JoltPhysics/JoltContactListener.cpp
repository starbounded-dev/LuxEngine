// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "JoltContactListener.h"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/EstimateCollisionResponse.h>

namespace Lux
{
	namespace
	{
		constexpr float k_MinApproachSpeed = 0.01f;
	}

	JoltContactListener::JoltContactListener()
	{
		m_Events.reserve(1024);
	}

	void JoltContactListener::Capture(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold,
		const JPH::ContactSettings& settings)
	{
		PhysicsContactEvent event;
		event.Key = { a.GetID().GetIndexAndSequenceNumber(), b.GetID().GetIndexAndSequenceNumber(), manifold.mSubShapeID1.GetValue(), manifold.mSubShapeID2.GetValue() };
		{
			std::scoped_lock lock(m_Mutex);
			if (settings.mIsSensor || manifold.mRelativeContactPointsOn1.empty() || manifold.mPenetrationDepth < 0.0f)
			{
				if (m_RealContacts.erase(event.Key))
				{
					event.State = PhysicsContactEvent::Type::End;
					m_Events.push_back(event);
				}
				return;
			}
			event.State = m_RealContacts.insert(event.Key).second ? PhysicsContactEvent::Type::Begin : PhysicsContactEvent::Type::Persist;
		}

		event.Entity1 = a.GetUserData();
		event.Entity2 = b.GetUserData();
		const auto position = manifold.GetWorldSpaceContactPointOn1(0);
		event.Position = { position.GetX(), position.GetY(), position.GetZ() };
		const auto normal = manifold.mWorldSpaceNormal;
		const auto relativePointVelocity = a.GetPointVelocity(position) - b.GetPointVelocity(position);
		const auto relativeLinearVelocity = a.GetLinearVelocity() - b.GetLinearVelocity();
		event.SlipSpeed = (relativePointVelocity - normal * relativePointVelocity.Dot(normal)).Length();
		const float tangentialSpeed = (relativeLinearVelocity - normal * relativeLinearVelocity.Dot(normal)).Length();
		// Pure rolling has moving centres with little contact-point slip. Sliding contributes to scrape.
		event.RollSpeed = std::max(0.0f, tangentialSpeed - event.SlipSpeed);
		const auto mass = [](const JPH::Body& body)
		{
			const float inverse = body.IsDynamic() ? body.GetMotionProperties()->GetInverseMass() : 0.0f;
			return inverse > 0.0f ? 1.0f / inverse : 0.0f;
		};
		event.Mass1 = mass(a);
		event.Mass2 = mass(b);
		// A speculative Begin can become a real contact only on Persist. Estimate on approach,
		// and only on the first real contact; resting contacts must not run another solver per frame.
		if (event.State == PhysicsContactEvent::Type::Begin && relativePointVelocity.Dot(normal) > k_MinApproachSpeed && (a.IsDynamic() || b.IsDynamic()))
		{
			JPH::CollisionEstimationResult estimate;
			JPH::EstimateCollisionResponse(a, b, manifold, estimate, settings.mCombinedFriction,
				settings.mCombinedRestitution, MinimumRestitutionVelocity);
			for (const auto& impulse : estimate.mImpulses)
				event.EstimatedImpulse += impulse.mContactImpulse;
		}
		// The estimate above runs outside the lock. If the contact was removed meanwhile, its End is
		// already queued; appending now would recreate a contact that no longer exists.
		std::scoped_lock lock(m_Mutex);
		if (!m_RealContacts.contains(event.Key))
			return;
		m_Events.push_back(event);
	}

	void JoltContactListener::OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings)
	{
		Capture(a, b, manifold, settings);
	}

	void JoltContactListener::OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings)
	{
		Capture(a, b, manifold, settings);
	}

	void JoltContactListener::OnContactRemoved(const JPH::SubShapeIDPair& pair)
	{
		PhysicsContactEvent event;
		event.State = PhysicsContactEvent::Type::End;
		event.Key = { pair.GetBody1ID().GetIndexAndSequenceNumber(), pair.GetBody2ID().GetIndexAndSequenceNumber(), pair.GetSubShapeID1().GetValue(), pair.GetSubShapeID2().GetValue() };
		std::scoped_lock lock(m_Mutex);
		if (m_RealContacts.erase(event.Key))
			m_Events.push_back(event);
	}

	void JoltContactListener::Drain(std::vector<PhysicsContactEvent>& events)
	{
		events.clear();
		std::scoped_lock lock(m_Mutex);
		m_Events.swap(events);
	}
}
