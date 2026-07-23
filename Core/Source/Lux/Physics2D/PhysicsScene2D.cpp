#include "lpch.h"
#include "PhysicsScene2D.h"

#include "ContactListener2D.h"

#include "Lux/Scene/Scene.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"

#include "box2d/b2_body.h"
#include "box2d/b2_circle_shape.h"
#include "box2d/b2_fixture.h"
#include "box2d/b2_polygon_shape.h"
#include "box2d/b2_world.h"

namespace Lux {

	namespace {
		ContactListener2D s_Box2DContactListener;
	}

	PhysicsScene2D::PhysicsScene2D(Scene* scene)
		: m_Scene(scene)
	{
	}

	PhysicsScene2D::~PhysicsScene2D()
	{
		Stop();
	}

	void PhysicsScene2D::Start()
	{
		if (!m_Scene)
			return;

		Stop();
		m_PhysicsWorld = new b2World({ 0.0f, -9.8f });
		m_PhysicsWorld->SetContactListener(&s_Box2DContactListener);

		auto view = m_Scene->GetAllEntitiesWith<RigidBody2DComponent>();
		for (auto e : view)
		{
			Entity entity = { e, m_Scene };
			auto& transform = entity.GetComponent<TransformComponent>();
			auto& rb2d = entity.GetComponent<RigidBody2DComponent>();

			b2BodyDef bodyDef;
			bodyDef.type = Utils::RigidBody2DTypeToBox2DBody(rb2d.BodyType);
			bodyDef.position.Set(transform.Translation.x, transform.Translation.y);
			bodyDef.angle = transform.GetRotationEuler().z;
			bodyDef.linearDamping = rb2d.LinearDrag;
			bodyDef.angularDamping = rb2d.AngularDrag;
			bodyDef.gravityScale = rb2d.GravityScale;
			bodyDef.bullet = rb2d.IsBullet;

			b2Body* body = m_PhysicsWorld->CreateBody(&bodyDef);
			body->SetFixedRotation(rb2d.FixedRotation);
			rb2d.RuntimeBody = body;

			if (entity.HasComponent<BoxCollider2DComponent>())
			{
				auto& bc2d = entity.GetComponent<BoxCollider2DComponent>();

				b2PolygonShape boxShape;
				boxShape.SetAsBox(bc2d.Size.x * transform.Scale.x, bc2d.Size.y * transform.Scale.y, b2Vec2(bc2d.Offset.x, bc2d.Offset.y), 0.0f);

				b2FixtureDef fixtureDef;
				fixtureDef.shape = &boxShape;
				fixtureDef.density = bc2d.Density;
				fixtureDef.friction = bc2d.Friction;
				fixtureDef.restitution = bc2d.Restitution;
				fixtureDef.restitutionThreshold = bc2d.RestitutionThreshold;
				body->CreateFixture(&fixtureDef);
			}

			if (entity.HasComponent<CircleCollider2DComponent>())
			{
				auto& cc2d = entity.GetComponent<CircleCollider2DComponent>();

				b2CircleShape circleShape;
				circleShape.m_p.Set(cc2d.Offset.x, cc2d.Offset.y);
				circleShape.m_radius = transform.Scale.x * cc2d.Radius;

				b2FixtureDef fixtureDef;
				fixtureDef.shape = &circleShape;
				fixtureDef.density = cc2d.Density;
				fixtureDef.friction = cc2d.Friction;
				fixtureDef.restitution = cc2d.Restitution;
				fixtureDef.restitutionThreshold = cc2d.RestitutionThreshold;
				body->CreateFixture(&fixtureDef);
			}
		}
	}

	void PhysicsScene2D::Stop()
	{
		delete m_PhysicsWorld;
		m_PhysicsWorld = nullptr;
	}

	void PhysicsScene2D::Simulate(Timestep timestep)
	{
		if (!m_PhysicsWorld || !m_Scene)
			return;

		const int32_t velocityIterations = 6;
		const int32_t positionIterations = 2;
		m_PhysicsWorld->Step(timestep, velocityIterations, positionIterations);

		auto view = m_Scene->GetAllEntitiesWith<RigidBody2DComponent>();
		for (auto e : view)
		{
			Entity entity = { e, m_Scene };
			auto& transform = entity.GetComponent<TransformComponent>();
			auto& rb2d = entity.GetComponent<RigidBody2DComponent>();

			b2Body* body = (b2Body*)rb2d.RuntimeBody;
			if (!body)
				continue;

			const auto& position = body->GetPosition();
			transform.Translation.x = position.x;
			transform.Translation.y = position.y;
			glm::vec3 rotation = transform.GetRotationEuler();
			rotation.z = body->GetAngle();
			transform.SetRotationEuler(rotation);
		}
	}

	void PhysicsScene2D::SetPlaying(bool playing)
	{
		ContactListener2D::m_IsPlaying = playing;
	}

}
