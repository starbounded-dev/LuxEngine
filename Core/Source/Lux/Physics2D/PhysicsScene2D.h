// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "Lux/Core/Timestep.h"
#include "Lux/Core/UUID.h"
#include "Lux/Physics/PhysicsContactEvent.h"

#include <glm/glm.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

class b2Body;
class b2World;

namespace Lux {

	class ContactListener2D;
	class Entity;
	class Scene;

	// The Box2D world of a running scene: one body per entity with a RigidBody2DComponent and one fixture (or
	// a few) per 2D collider component on it, joints between bodies, and queries. It steps on the same fixed
	// clock as the 3D scene (Scene::StepPhysics) and writes body poses back to the entities after every step.
	// Main thread only.
	class PhysicsScene2D
	{
	public:
		struct RayHit
		{
			UUID EntityID = 0;
			glm::vec2 Point{ 0.0f };
			glm::vec2 Normal{ 0.0f };
			float Distance = 0.0f;
		};
		enum class QueryShape { Circle = 0, Box = 1 };

		explicit PhysicsScene2D(Scene* scene);
		~PhysicsScene2D();

		// Makes the world, every body and then every joint of the scene.
		void Start();
		void Stop();

		// For entities that appear while the scene runs (prefab spawns): the body and its colliders, then the joint.
		void CreateBody(Entity entity);
		void CreateJoint(Entity entity);
		void DestroyBody(UUID entity);

		// The step length, from the project (Project Settings > Physics > Fixed Timestep).
		float GetFixedTimestep() const { return m_FixedTimestep; }
		// How many fixed steps are due after `timestep` more seconds, at most 8; the rest is dropped.
		// The scene uses its 3D physics scene's clock when it has one and this one otherwise.
		uint32_t AdvanceFixedClock(Timestep timestep);
		void SimulateStep(float fixedTimestep);

		// Contact begin/end events are only kept while this is on (Play), and are taken by DrainContactEvents.
		void SetRecordContacts(bool record) { m_RecordContacts = record; }
		void DrainContactEvents(std::vector<PhysicsContactEvent>& events);

		// The nearest hit along a ray, or every hit nearest first. Triggers are skipped unless includeTriggers;
		// `ignore` is an entity the ray passes through (0 for none).
		bool RayCast(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, UUID ignore, bool includeTriggers, RayHit& hit) const;
		void RayCastAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, UUID ignore, bool includeTriggers, std::vector<RayHit>& hits) const;
		// The entities with a collider touching a circle (dimensions.x = radius) or a box (dimensions = half
		// extents, turned by `angle` radians), each once.
		void Overlap(QueryShape shape, const glm::vec2& center, const glm::vec2& dimensions, float angle, UUID ignore, bool includeTriggers, std::vector<UUID>& entities) const;

	private:
		void CreateColliders(Entity entity, b2Body* body);

		Scene* m_Scene = nullptr;
		b2World* m_PhysicsWorld = nullptr;
		b2Body* m_GroundBody = nullptr; // anchors a joint to the world
		std::unordered_map<UUID, b2Body*> m_Bodies;
		std::unique_ptr<ContactListener2D> m_ContactListener;
		std::vector<PhysicsContactEvent> m_Events;
		bool m_RecordContacts = false;
		float m_FixedTimestep = 1.0f / 60.0f;
		float m_Accumulator = 0.0f;
	};

}
