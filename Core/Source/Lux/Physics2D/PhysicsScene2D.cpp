// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "PhysicsScene2D.h"

#include "ContactListener2D.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/SpriteSheet.h"
#include "Lux/Scene/Tilemap.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"

#include "box2d/b2_body.h"
#include "box2d/b2_chain_shape.h"
#include "box2d/b2_circle_shape.h"
#include "box2d/b2_collision.h"
#include "box2d/b2_distance_joint.h"
#include "box2d/b2_fixture.h"
#include "box2d/b2_polygon_shape.h"
#include "box2d/b2_prismatic_joint.h"
#include "box2d/b2_revolute_joint.h"
#include "box2d/b2_weld_joint.h"
#include "box2d/b2_world.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Lux {

	namespace {

		// The same cap as the 3D scene: a long hitch drops the extra steps instead of spiralling.
		constexpr uint32_t kMaxFixedSteps = 8;
		constexpr float kMinFixedTimestep = 0.001f;
		constexpr int32_t kVelocityIterations = 6;
		constexpr int32_t kPositionIterations = 2;
		constexpr float kDegenerateTolerance = 1e-4f;

		b2Vec2 ToB2(const glm::vec2& value)
		{
			return { value.x, value.y };
		}

		glm::vec2 ToGlm(const b2Vec2& value)
		{
			return { value.x, value.y };
		}

		// One fixture's feel and whether it blocks, from any 2D collider component.
		template<typename TCollider>
		b2FixtureDef MakeFixtureDef(const TCollider& collider, const b2Shape* shape, float density)
		{
			b2FixtureDef fixtureDef;
			fixtureDef.shape = shape;
			fixtureDef.density = density;
			fixtureDef.friction = collider.Friction;
			fixtureDef.restitution = collider.Restitution;
			fixtureDef.restitutionThreshold = collider.RestitutionThreshold;
			fixtureDef.isSensor = collider.IsTrigger;
			return fixtureDef;
		}

		// A polygon needs three points that are not on one line.
		bool HasArea(const std::vector<b2Vec2>& points)
		{
			if (points.size() < 3)
				return false;
			size_t farthest = 1;
			float farDistance = 0.0f;
			for (size_t i = 1; i < points.size(); i++)
			{
				const float distance = (points[i] - points[0]).LengthSquared();
				if (distance > farDistance)
				{
					farDistance = distance;
					farthest = i;
				}
			}
			if (farDistance < kDegenerateTolerance * kDegenerateTolerance)
				return false;
			const b2Vec2 line = points[farthest] - points[0];
			for (size_t i = 1; i < points.size(); i++)
			{
				if (std::abs(b2Cross(line, points[i] - points[0])) > kDegenerateTolerance * std::sqrt(farDistance))
					return true;
			}
			return false;
		}

		class ClosestRayCallback : public b2RayCastCallback
		{
		public:
			ClosestRayCallback(UUID ignore, bool includeTriggers) : m_Ignore(ignore), m_IncludeTriggers(includeTriggers) {}

			float ReportFixture(b2Fixture* fixture, const b2Vec2& point, const b2Vec2& normal, float fraction) override
			{
				if ((fixture->IsSensor() && !m_IncludeTriggers) || UUID(static_cast<uint64_t>(fixture->GetBody()->GetUserData().pointer)) == m_Ignore)
					return -1.0f; // pass through
				EntityID = UUID(static_cast<uint64_t>(fixture->GetBody()->GetUserData().pointer));
				Point = point;
				Normal = normal;
				Fraction = fraction;
				Hit = true;
				return fraction; // clip the ray: only something nearer counts now
			}

			bool Hit = false;
			UUID EntityID = 0;
			b2Vec2 Point{ 0.0f, 0.0f };
			b2Vec2 Normal{ 0.0f, 0.0f };
			float Fraction = 1.0f;

		private:
			UUID m_Ignore;
			bool m_IncludeTriggers;
		};

		class AllRaysCallback : public b2RayCastCallback
		{
		public:
			struct Hit { UUID EntityID; b2Vec2 Point; b2Vec2 Normal; float Fraction; };

			AllRaysCallback(UUID ignore, bool includeTriggers) : m_Ignore(ignore), m_IncludeTriggers(includeTriggers) {}

			float ReportFixture(b2Fixture* fixture, const b2Vec2& point, const b2Vec2& normal, float fraction) override
			{
				const UUID entity(static_cast<uint64_t>(fixture->GetBody()->GetUserData().pointer));
				if ((fixture->IsSensor() && !m_IncludeTriggers) || entity == m_Ignore)
					return -1.0f;
				Hits.push_back({ entity, point, normal, fraction });
				return 1.0f; // keep going to the end of the ray
			}

			std::vector<Hit> Hits;

		private:
			UUID m_Ignore;
			bool m_IncludeTriggers;
		};

		class FixtureCollector : public b2QueryCallback
		{
		public:
			bool ReportFixture(b2Fixture* fixture) override
			{
				Fixtures.push_back(fixture);
				return true;
			}

			std::vector<b2Fixture*> Fixtures;
		};

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

		// The project's step length and gravity; a scene outside a project falls back to 60 Hz and 9.81 m/s^2.
		glm::vec2 gravity{ 0.0f, -9.81f };
		m_FixedTimestep = 1.0f / 60.0f;
		if (const Ref<Project> project = Project::GetActive())
		{
			const ProjectPhysicsSettings& settings = project->GetConfig().Physics;
			gravity = { settings.Gravity.x, settings.Gravity.y };
			m_FixedTimestep = std::max(settings.FixedTimestep, kMinFixedTimestep);
		}
		m_Accumulator = 0.0f;

		m_PhysicsWorld = new b2World(ToB2(gravity));
		m_ContactListener = CreateScope<ContactListener2D>(m_Events, m_RecordContacts);
		m_PhysicsWorld->SetContactListener(m_ContactListener.get());

		for (auto e : m_Scene->GetAllEntitiesWith<RigidBody2DComponent>())
			CreateBody(Entity{ e, m_Scene });
		// Joints last: both of their bodies exist by now.
		for (auto e : m_Scene->GetAllEntitiesWith<Joint2DComponent>())
			CreateJoint(Entity{ e, m_Scene });
	}

	void PhysicsScene2D::Stop()
	{
		delete m_PhysicsWorld; // destroys the bodies, fixtures and joints with it
		m_PhysicsWorld = nullptr;
		m_ContactListener.reset();
		m_GroundBody = nullptr;
		m_Bodies.clear();
		m_TilemapFixtures.clear();
		m_Events.clear();
	}

	void PhysicsScene2D::CreateBody(Entity entity)
	{
		if (!m_PhysicsWorld || !entity.HasComponent<RigidBody2DComponent>() || m_Bodies.contains(entity.GetUUID()))
			return;

		const auto& transform = entity.GetComponent<TransformComponent>();
		auto& rb2d = entity.GetComponent<RigidBody2DComponent>();

		b2BodyDef bodyDef;
		bodyDef.type = Utils::RigidBody2DTypeToBox2DBody(rb2d.BodyType);
		bodyDef.position.Set(transform.Translation.x, transform.Translation.y);
		bodyDef.angle = transform.GetRotationEuler().z;
		bodyDef.linearDamping = rb2d.LinearDrag;
		bodyDef.angularDamping = rb2d.AngularDrag;
		bodyDef.gravityScale = rb2d.GravityScale;
		bodyDef.bullet = rb2d.IsBullet;
		// The contact listener and the queries resolve a body back to its entity through this UUID.
		bodyDef.userData.pointer = static_cast<uintptr_t>(static_cast<uint64_t>(entity.GetUUID()));

		b2Body* body = m_PhysicsWorld->CreateBody(&bodyDef);
		body->SetFixedRotation(rb2d.FixedRotation);
		rb2d.RuntimeBody = body;
		m_Bodies[entity.GetUUID()] = body;

		CreateColliders(entity, body);
	}

	void PhysicsScene2D::CreateColliders(Entity entity, b2Body* body)
	{
		const auto& transform = entity.GetComponent<TransformComponent>();
		const glm::vec2 scale(std::abs(transform.Scale.x), std::abs(transform.Scale.y));

		if (entity.HasComponent<BoxCollider2DComponent>())
		{
			auto& collider = entity.GetComponent<BoxCollider2DComponent>();
			b2PolygonShape shape;
			shape.SetAsBox(collider.Size.x * scale.x, collider.Size.y * scale.y, ToB2(collider.Offset), 0.0f);
			const b2FixtureDef fixtureDef = MakeFixtureDef(collider, &shape, collider.Density);
			collider.RuntimeFixture = body->CreateFixture(&fixtureDef);
		}

		if (entity.HasComponent<CircleCollider2DComponent>())
		{
			auto& collider = entity.GetComponent<CircleCollider2DComponent>();
			b2CircleShape shape;
			shape.m_p = ToB2(collider.Offset);
			shape.m_radius = scale.x * collider.Radius;
			const b2FixtureDef fixtureDef = MakeFixtureDef(collider, &shape, collider.Density);
			collider.RuntimeFixture = body->CreateFixture(&fixtureDef);
		}

		if (entity.HasComponent<PolygonCollider2DComponent>())
		{
			auto& collider = entity.GetComponent<PolygonCollider2DComponent>();
			std::vector<b2Vec2> points;
			for (size_t i = 0; i < collider.Points.size() && i < PolygonCollider2DComponent::MaxPoints; i++)
				points.emplace_back(collider.Points[i].x * scale.x + collider.Offset.x, collider.Points[i].y * scale.y + collider.Offset.y);
			if (!HasArea(points))
				LUX_CORE_WARN_TAG("Physics", "Polygon Collider 2D on '{}' needs three points that are not on one line; it has no collider", entity.Name());
			else
			{
				b2PolygonShape shape;
				shape.Set(points.data(), static_cast<int32_t>(points.size())); // keeps the convex hull
				const b2FixtureDef fixtureDef = MakeFixtureDef(collider, &shape, collider.Density);
				collider.RuntimeFixture = body->CreateFixture(&fixtureDef);
			}
		}

		if (entity.HasComponent<CapsuleCollider2DComponent>())
		{
			auto& collider = entity.GetComponent<CapsuleCollider2DComponent>();
			const bool vertical = collider.CapsuleDirection == CapsuleCollider2DComponent::Direction::Vertical;
			const float radius = std::max((vertical ? scale.x : scale.y) * collider.Radius, 0.0f);
			const float height = std::max((vertical ? scale.y : scale.x) * collider.Height, 2.0f * radius);
			const float straight = height * 0.5f - radius; // half the length of the flat sides
			const glm::vec2 axis = vertical ? glm::vec2(0.0f, 1.0f) : glm::vec2(1.0f, 0.0f);

			b2CircleShape cap;
			cap.m_radius = radius;
			for (const float side : { -1.0f, 1.0f })
			{
				cap.m_p = ToB2(collider.Offset + axis * (side * straight));
				const b2FixtureDef fixtureDef = MakeFixtureDef(collider, &cap, collider.Density);
				collider.RuntimeFixture = body->CreateFixture(&fixtureDef);
			}
			if (straight > 0.0f)
			{
				b2PolygonShape middle;
				middle.SetAsBox(vertical ? radius : straight, vertical ? straight : radius, ToB2(collider.Offset), 0.0f);
				const b2FixtureDef fixtureDef = MakeFixtureDef(collider, &middle, collider.Density);
				body->CreateFixture(&fixtureDef);
			}
		}

		if (entity.HasComponent<TilemapComponent>())
			CreateTilemapFixtures(entity, body);

		if (entity.HasComponent<EdgeCollider2DComponent>())
		{
			auto& collider = entity.GetComponent<EdgeCollider2DComponent>();
			std::vector<b2Vec2> points;
			for (const glm::vec2& point : collider.Points)
				points.emplace_back(point.x * scale.x + collider.Offset.x, point.y * scale.y + collider.Offset.y);
			const bool loop = collider.Loop && points.size() >= 3;
			if (points.size() < 2)
				LUX_CORE_WARN_TAG("Physics", "Edge Collider 2D on '{}' needs two points; it has no collider", entity.Name());
			else
			{
				// A Box2D chain blocks from one side only (the side to the right of its direction). An edge collider
				// blocks from both, like Unity's: the same line is made twice, once each way round, so a body
				// meets the one that faces it. Chains, not separate edges, keep a body from catching on the joints.
				for (int pass = 0; pass < 2; pass++)
				{
					std::vector<b2Vec2> ordered = points;
					if (pass == 1)
						std::reverse(ordered.begin(), ordered.end());

					b2ChainShape shape;
					if (loop)
						shape.CreateLoop(ordered.data(), static_cast<int32_t>(ordered.size()));
					else
					{
						// Ghost points past each end continue the line, so a body does not catch on the end.
						const b2Vec2 previous = ordered[0] + (ordered[0] - ordered[1]);
						const b2Vec2 next = ordered.back() + (ordered.back() - ordered[ordered.size() - 2]);
						shape.CreateChain(ordered.data(), static_cast<int32_t>(ordered.size()), previous, next);
					}
					const b2FixtureDef fixtureDef = MakeFixtureDef(collider, &shape, 0.0f);
					b2Fixture* fixture = body->CreateFixture(&fixtureDef);
					if (pass == 0)
						collider.RuntimeFixture = fixture;
				}
			}
		}
	}

	void PhysicsScene2D::CreateTilemapFixtures(Entity entity, b2Body* body)
	{
		const auto& transform = entity.GetComponent<TransformComponent>();
		const auto& tilemap = entity.GetComponent<TilemapComponent>();
		const Ref<SpriteSheet> tileSet = tilemap.TileSet ? AssetManager::GetAsset<SpriteSheet>(tilemap.TileSet) : nullptr;
		if (!tileSet)
			return;

		// Cell (x, y) spans (x, y) to (x + 1, y + 1) cells from the entity's origin; the entity's scale stretches the cells.
		const glm::vec2 cell = tilemap.CellSize * glm::vec2(std::abs(transform.Scale.x), std::abs(transform.Scale.y));
		std::vector<Tilemap::CellRect> blocks;
		std::vector<b2Fixture*>& fixtures = m_TilemapFixtures[entity.GetUUID()];
		for (const TilemapLayer& layer : tilemap.Layers)
		{
			if (!layer.Collides)
				continue;
			Tilemap::MergeSolid(layer, tileSet->Frames, blocks);
			for (const Tilemap::CellRect& block : blocks)
			{
				b2PolygonShape shape;
				shape.SetAsBox(0.5f * static_cast<float>(block.Width) * cell.x, 0.5f * static_cast<float>(block.Height) * cell.y,
					b2Vec2((static_cast<float>(block.X) + 0.5f * static_cast<float>(block.Width)) * cell.x, (static_cast<float>(block.Y) + 0.5f * static_cast<float>(block.Height)) * cell.y), 0.0f);
				const b2FixtureDef fixtureDef = MakeFixtureDef(tilemap, &shape, 0.0f);
				fixtures.push_back(body->CreateFixture(&fixtureDef));
			}
		}
	}

	void PhysicsScene2D::RebuildTilemap(Entity entity)
	{
		const auto body = m_Bodies.find(entity.GetUUID());
		if (body == m_Bodies.end() || !entity.HasComponent<TilemapComponent>())
			return;

		std::vector<b2Fixture*>& fixtures = m_TilemapFixtures[entity.GetUUID()];
		for (b2Fixture* fixture : fixtures)
			body->second->DestroyFixture(fixture);
		fixtures.clear();
		CreateTilemapFixtures(entity, body->second);
	}

	void PhysicsScene2D::CreateJoint(Entity entity)
	{
		if (!m_PhysicsWorld || !entity.HasComponent<Joint2DComponent>())
			return;

		auto& joint = entity.GetComponent<Joint2DComponent>();
		if (!joint.Enabled)
			return;

		const auto bodyA = m_Bodies.find(entity.GetUUID());
		if (bodyA == m_Bodies.end())
		{
			LUX_CORE_WARN_TAG("Physics", "Joint 2D on '{}' needs a Rigidbody 2D on the same entity; no joint made", entity.Name());
			return;
		}

		b2Body* a = bodyA->second;
		b2Body* b = nullptr;
		if (joint.ConnectedEntity)
		{
			const auto bodyB = m_Bodies.find(joint.ConnectedEntity);
			if (bodyB == m_Bodies.end() || bodyB->second == a)
			{
				LUX_CORE_WARN_TAG("Physics", "Joint 2D on '{}' is connected to an entity with no Rigidbody 2D (or to itself); no joint made", entity.Name());
				return;
			}
			b = bodyB->second;
		}
		else
		{
			if (!m_GroundBody)
			{
				const b2BodyDef groundDef;
				m_GroundBody = m_PhysicsWorld->CreateBody(&groundDef);
			}
			b = m_GroundBody;
		}

		const b2Vec2 anchorA = a->GetWorldPoint(ToB2(joint.Anchor));
		const b2Vec2 anchorB = joint.ConnectedEntity ? b->GetWorldPoint(ToB2(joint.ConnectedAnchor)) : ToB2(joint.ConnectedAnchor);

		switch (joint.JointType)
		{
			case Joint2DComponent::Type::Distance:
			{
				b2DistanceJointDef def;
				def.Initialize(a, b, anchorA, anchorB);
				if (!joint.AutoLength)
					def.length = std::max(joint.Length, b2_linearSlop);
				def.stiffness = joint.Stiffness;
				def.damping = joint.Damping;
				def.collideConnected = joint.CollideConnected;
				joint.RuntimeJoint = m_PhysicsWorld->CreateJoint(&def);
				break;
			}
			case Joint2DComponent::Type::Revolute:
			{
				b2RevoluteJointDef def;
				def.Initialize(a, b, anchorA);
				def.enableLimit = joint.EnableLimit;
				def.lowerAngle = glm::radians(std::min(joint.LowerLimit, joint.UpperLimit));
				def.upperAngle = glm::radians(std::max(joint.LowerLimit, joint.UpperLimit));
				def.enableMotor = joint.EnableMotor;
				def.motorSpeed = glm::radians(joint.MotorSpeed);
				def.maxMotorTorque = joint.MaxMotorForce;
				def.collideConnected = joint.CollideConnected;
				joint.RuntimeJoint = m_PhysicsWorld->CreateJoint(&def);
				break;
			}
			case Joint2DComponent::Type::Prismatic:
			{
				b2PrismaticJointDef def;
				const glm::vec2 axis = glm::length(joint.Axis) > 0.0f ? glm::normalize(joint.Axis) : glm::vec2(1.0f, 0.0f);
				def.Initialize(a, b, anchorA, a->GetWorldVector(ToB2(axis)));
				def.enableLimit = joint.EnableLimit;
				def.lowerTranslation = std::min(joint.LowerLimit, joint.UpperLimit);
				def.upperTranslation = std::max(joint.LowerLimit, joint.UpperLimit);
				def.enableMotor = joint.EnableMotor;
				def.motorSpeed = joint.MotorSpeed;
				def.maxMotorForce = joint.MaxMotorForce;
				def.collideConnected = joint.CollideConnected;
				joint.RuntimeJoint = m_PhysicsWorld->CreateJoint(&def);
				break;
			}
			case Joint2DComponent::Type::Weld:
			{
				b2WeldJointDef def;
				def.Initialize(a, b, anchorA);
				def.stiffness = joint.Stiffness;
				def.damping = joint.Damping;
				def.collideConnected = joint.CollideConnected;
				joint.RuntimeJoint = m_PhysicsWorld->CreateJoint(&def);
				break;
			}
		}
	}

	void PhysicsScene2D::DestroyBody(UUID entity)
	{
		const auto found = m_Bodies.find(entity);
		if (found == m_Bodies.end() || !m_PhysicsWorld)
			return;
		m_PhysicsWorld->DestroyBody(found->second);
		m_Bodies.erase(found);
		m_TilemapFixtures.erase(entity);
	}

	uint32_t PhysicsScene2D::AdvanceFixedClock(Timestep timestep)
	{
		if (!m_PhysicsWorld)
			return 0;

		// Same accumulator rule as the 3D scene, so a project steps both worlds alike.
		if (m_Accumulator > m_FixedTimestep)
			m_Accumulator = 0.0f;
		m_Accumulator += static_cast<float>(timestep);
		if (m_Accumulator < m_FixedTimestep)
			return 0;

		const uint32_t steps = std::min(static_cast<uint32_t>(m_Accumulator / m_FixedTimestep), kMaxFixedSteps);
		m_Accumulator -= static_cast<float>(steps) * m_FixedTimestep;
		return steps;
	}

	void PhysicsScene2D::SimulateStep(float fixedTimestep)
	{
		if (!m_PhysicsWorld || !m_Scene || fixedTimestep <= 0.0f)
			return;

		m_PhysicsWorld->Step(fixedTimestep, kVelocityIterations, kPositionIterations);

		auto view = m_Scene->GetAllEntitiesWith<RigidBody2DComponent>();
		for (auto e : view)
		{
			Entity entity = { e, m_Scene };
			auto& rb2d = entity.GetComponent<RigidBody2DComponent>();

			b2Body* body = static_cast<b2Body*>(rb2d.RuntimeBody);
			if (!body || body->GetType() == b2_staticBody)
				continue;

			auto& transform = entity.GetComponent<TransformComponent>();
			const b2Vec2& position = body->GetPosition();
			transform.Translation.x = position.x;
			transform.Translation.y = position.y;
			glm::vec3 rotation = transform.GetRotationEuler();
			rotation.z = body->GetAngle();
			transform.SetRotationEuler(rotation);
		}
	}

	void PhysicsScene2D::DrainContactEvents(std::vector<PhysicsContactEvent>& events)
	{
		events.insert(events.end(), m_Events.begin(), m_Events.end());
		m_Events.clear();
	}

	bool PhysicsScene2D::RayCast(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, UUID ignore, bool includeTriggers, RayHit& hit) const
	{
		if (!m_PhysicsWorld || !(maxDistance > 0.0f) || glm::dot(direction, direction) <= 0.0f)
			return false;

		const glm::vec2 unit = glm::normalize(direction);
		ClosestRayCallback callback(ignore, includeTriggers);
		m_PhysicsWorld->RayCast(&callback, ToB2(origin), ToB2(origin + unit * maxDistance));
		if (!callback.Hit)
			return false;

		hit = { callback.EntityID, ToGlm(callback.Point), ToGlm(callback.Normal), callback.Fraction * maxDistance };
		return true;
	}

	void PhysicsScene2D::RayCastAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, UUID ignore, bool includeTriggers, std::vector<RayHit>& hits) const
	{
		hits.clear();
		if (!m_PhysicsWorld || !(maxDistance > 0.0f) || glm::dot(direction, direction) <= 0.0f)
			return;

		const glm::vec2 unit = glm::normalize(direction);
		AllRaysCallback callback(ignore, includeTriggers);
		m_PhysicsWorld->RayCast(&callback, ToB2(origin), ToB2(origin + unit * maxDistance));

		// One hit per entity, the nearest, then nearest first.
		std::sort(callback.Hits.begin(), callback.Hits.end(), [](const auto& a, const auto& b) { return a.Fraction < b.Fraction; });
		std::unordered_set<UUID> seen;
		for (const auto& hit : callback.Hits)
		{
			if (seen.insert(hit.EntityID).second)
				hits.push_back({ hit.EntityID, ToGlm(hit.Point), ToGlm(hit.Normal), hit.Fraction * maxDistance });
		}
	}

	void PhysicsScene2D::Overlap(QueryShape shape, const glm::vec2& center, const glm::vec2& dimensions, float angle, UUID ignore, bool includeTriggers, std::vector<UUID>& entities) const
	{
		entities.clear();
		if (!m_PhysicsWorld)
			return;

		b2CircleShape circle;
		b2PolygonShape box;
		const b2Shape* queryShape = nullptr;
		b2Transform transform;
		transform.Set(ToB2(center), 0.0f);
		b2AABB aabb;
		if (shape == QueryShape::Circle)
		{
			circle.m_radius = std::max(dimensions.x, 0.0f);
			queryShape = &circle;
			aabb.lowerBound = ToB2(center - glm::vec2(circle.m_radius));
			aabb.upperBound = ToB2(center + glm::vec2(circle.m_radius));
		}
		else
		{
			box.SetAsBox(std::max(dimensions.x, 0.0f), std::max(dimensions.y, 0.0f));
			queryShape = &box;
			transform.Set(ToB2(center), angle);
			const float extentX = std::abs(std::cos(angle)) * dimensions.x + std::abs(std::sin(angle)) * dimensions.y;
			const float extentY = std::abs(std::sin(angle)) * dimensions.x + std::abs(std::cos(angle)) * dimensions.y;
			aabb.lowerBound = ToB2(center - glm::vec2(extentX, extentY));
			aabb.upperBound = ToB2(center + glm::vec2(extentX, extentY));
		}

		FixtureCollector collector;
		m_PhysicsWorld->QueryAABB(&collector, aabb);
		std::unordered_set<UUID> seen;
		for (b2Fixture* fixture : collector.Fixtures)
		{
			const UUID entity(static_cast<uint64_t>(fixture->GetBody()->GetUserData().pointer));
			if ((fixture->IsSensor() && !includeTriggers) || entity == ignore || seen.contains(entity))
				continue;
			for (int32_t child = 0; child < fixture->GetShape()->GetChildCount(); child++)
			{
				if (b2TestOverlap(queryShape, 0, fixture->GetShape(), child, transform, fixture->GetBody()->GetTransform()))
				{
					seen.insert(entity);
					entities.push_back(entity);
					break;
				}
			}
		}
	}

}
