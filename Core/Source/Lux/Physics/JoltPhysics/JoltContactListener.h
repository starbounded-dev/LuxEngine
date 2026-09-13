#pragma once

#include "Lux/Physics/PhysicsContactEvent.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <mutex>
#include <set>
#include <vector>

namespace Lux
{
	// Worker callbacks read only the supplied locked bodies. No ECS, FMOD or body locking here.
	class JoltContactListener final : public JPH::ContactListener
	{
	public:
		JoltContactListener();
		void OnContactAdded(const JPH::Body&, const JPH::Body&, const JPH::ContactManifold&, JPH::ContactSettings&) override;
		void OnContactPersisted(const JPH::Body&, const JPH::Body&, const JPH::ContactManifold&, JPH::ContactSettings&) override;
		void OnContactRemoved(const JPH::SubShapeIDPair&) override;
		void Drain(std::vector<PhysicsContactEvent>& events);
		float MinimumRestitutionVelocity = 1.0f;
	private:
		void Capture(const JPH::Body&, const JPH::Body&, const JPH::ContactManifold&, const JPH::ContactSettings&);
		std::mutex m_Mutex;
		std::vector<PhysicsContactEvent> m_Events;
		std::set<std::array<uint32_t, 4>> m_RealContacts;
	};
}
