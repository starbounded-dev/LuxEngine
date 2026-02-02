#pragma once

#include "StarEngine/Scene/Components.h"

#include "box2d/b2_body.h"

// Box2D
#include "box2d/box2d.h"
#include "box2d/b2_world.h"
#include "box2d/b2_body.h"
#include "box2d/b2_fixture.h"
#include "box2d/b2_polygon_shape.h"
#include "box2d/b2_circle_shape.h"

namespace StarEngine {

	namespace Utils {

		inline b2BodyType RigidBody2DTypeToBox2DBody(RigidBody2DComponent::BodyType bodyType)
		{
			switch (bodyType)
			{
			case RigidBody2DComponent::BodyType::Static:    return b2_staticBody;
			case RigidBody2DComponent::BodyType::Dynamic:   return b2_dynamicBody;
			case RigidBody2DComponent::BodyType::Kinematic: return b2_kinematicBody;
			}

			SE_CORE_ASSERT(false, "Unknown body type");
			return b2_staticBody;
		}

		inline RigidBody2DComponent::BodyType RigidBody2DTypeFromBox2DBody(b2BodyType bodyType)
		{
			switch (bodyType)
			{
			case b2_staticBody:    return RigidBody2DComponent::BodyType::Static;
			case b2_dynamicBody:   return RigidBody2DComponent::BodyType::Dynamic;
			case b2_kinematicBody: return RigidBody2DComponent::BodyType::Kinematic;
			}

			SE_CORE_ASSERT(false, "Unknown body type");
			return RigidBody2DComponent::BodyType::Static;
		}

	}

	class ContactListener2D : public b2ContactListener
	{
	public:
		virtual void BeginContact(b2Contact* contact) override;

		/// Called when two fixtures cease to touch.
		virtual void EndContact(b2Contact* contact) override;

		/// This is called after a contact is updated. This allows you to inspect a
		/// contact before it goes to the solver. If you are careful, you can modify the
		/// contact manifold (e.g. disable contact).
		/// A copy of the old manifold is provided so that you can detect changes.
		/// Note: this is called only for awake bodies.
		/// Note: this is called even when the number of contact points is zero.
		/// Note: this is not called for sensors.
		/// Note: if you set the number of contact points to zero, you will not
		/// get an EndContact callback. However, you may get a BeginContact callback
		/// the next step.
		virtual void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) override
		{
			B2_NOT_USED(contact);
			B2_NOT_USED(oldManifold);
		}

		/// This lets you inspect a contact after the solver is finished. This is useful
		/// for inspecting impulses.
		/// Note: the contact manifold does not include time of impact impulses, which can be
		/// arbitrarily large if the sub-step is small. Hence the impulse is provided explicitly
		/// in a separate data structure.
		/// Note: this is only called for contacts that are touching, solid, and awake.
		virtual void PostSolve(b2Contact* contact, const b2ContactImpulse* impulse) override
		{
			B2_NOT_USED(contact);
			B2_NOT_USED(impulse);
		}

		inline static bool m_IsPlaying = false;
	};

}
