// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Scene/Components.h"

#include "box2d/b2_body.h"

// Box2D
#include "box2d/box2d.h"
#include "box2d/b2_world.h"
#include "box2d/b2_body.h"
#include "box2d/b2_fixture.h"
#include "box2d/b2_polygon_shape.h"
#include "box2d/b2_circle_shape.h"

#include "Lux/Physics/PhysicsContactEvent.h"

#include <vector>

namespace Lux {

	namespace Utils {

		inline b2BodyType RigidBody2DTypeToBox2DBody(RigidBody2DComponent::Type bodyType)
		{
			switch (bodyType)
			{
			case RigidBody2DComponent::Type::Static:    return b2_staticBody;
			case RigidBody2DComponent::Type::Dynamic:   return b2_dynamicBody;
			case RigidBody2DComponent::Type::Kinematic: return b2_kinematicBody;
			}

			LUX_CORE_ASSERT(false, "Unknown body type");
			return b2_staticBody;
		}

		inline RigidBody2DComponent::Type RigidBody2DTypeFromBox2DBody(b2BodyType bodyType)
		{
			switch (bodyType)
			{
			case b2_staticBody:    return RigidBody2DComponent::Type::Static;
			case b2_dynamicBody:   return RigidBody2DComponent::Type::Dynamic;
			case b2_kinematicBody: return RigidBody2DComponent::Type::Kinematic;
			}

			LUX_CORE_ASSERT(false, "Unknown body type");
			return RigidBody2DComponent::Type::Static;
		}

	}

	// Turns Box2D contact begin/end into PhysicsContactEvents (a pair of entities, trigger or not) for the scene to
	// dispatch to scripts after the step. Nothing is called back into C# from inside the world step.
	class ContactListener2D : public b2ContactListener
	{
	public:
		// Events go to `events` while `record` is true.
		ContactListener2D(std::vector<PhysicsContactEvent>& events, const bool& record)
			: m_Events(events), m_Record(record) {}

		void BeginContact(b2Contact* contact) override;
		void EndContact(b2Contact* contact) override;

	private:
		void Record(b2Contact* contact, PhysicsContactEvent::Type state);

		std::vector<PhysicsContactEvent>& m_Events;
		const bool& m_Record;
	};

}
