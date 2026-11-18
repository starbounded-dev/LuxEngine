// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ContactListener2D.h"

namespace Lux {

	// PhysicsScene2D stores each body's entity UUID in its user data.
	static UUID GetBodyEntity(b2Fixture* fixture)
	{
		return UUID(static_cast<uint64_t>(fixture->GetBody()->GetUserData().pointer));
	}

	void ContactListener2D::Record(b2Contact* contact, PhysicsContactEvent::Type state)
	{
		if (!m_Record)
			return;

		b2Fixture* fixtureA = contact->GetFixtureA();
		b2Fixture* fixtureB = contact->GetFixtureB();

		PhysicsContactEvent event;
		event.State = state;
		// A contact lives from its Begin to its End, so its address pairs the two; the scene counts the
		// contacts of an entity pair (several fixtures touch at once) and tells scripts once.
		const uint64_t key = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(contact));
		event.Key = { static_cast<uint32_t>(key), static_cast<uint32_t>(key >> 32), 0, 0 };
		event.Entity1 = GetBodyEntity(fixtureA);
		event.Entity2 = GetBodyEntity(fixtureB);
		event.Trigger = fixtureA->IsSensor() || fixtureB->IsSensor();
		m_Events.push_back(event);
	}

	void ContactListener2D::BeginContact(b2Contact* contact)
	{
		Record(contact, PhysicsContactEvent::Type::Begin);
	}

	void ContactListener2D::EndContact(b2Contact* contact)
	{
		Record(contact, PhysicsContactEvent::Type::End);
	}

}
