#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Core/Timestep.h"
#include "Lux/Physics/CharacterController.h"
#include "Lux/Physics/PhysicsBody.h"
#include "Lux/Physics/SceneQueries.h"

#include <glm/gtc/quaternion.hpp>

namespace Lux {

	class Scene;
	class Entity;

	class PhysicsScene : public RefCounted
	{
	public:
		struct Impl;

		explicit PhysicsScene(Scene* scene);
		~PhysicsScene();

		void Start();
		void Destroy();
		void Stop();
		void Simulate(Timestep timestep);
		void SimulateStep(float fixedTimestep);

		glm::vec3 GetGravity() const;
		void SetGravity(const glm::vec3& gravity);

		Ref<PhysicsBody> GetBodyByEntityID(UUID entityID) const;
		Ref<PhysicsBody> GetBody(Entity entity) const;
		Ref<PhysicsBody> CreateBody(Entity entity);
		void DestroyBody(Entity entity);
		void SetBodyType(Entity entity, EBodyType bodyType);

		Ref<CharacterController> GetCharacterControllerByEntityID(UUID entityID) const;
		Ref<CharacterController> GetCharacterController(Entity entity) const;
		Ref<CharacterController> CreateCharacterController(Entity entity);
		void DestroyCharacterController(Entity entity);

		bool CastRay(const RayCastInfo* rayCastInfo, SceneQueryHit& outHit);
		bool CastShape(const ShapeCastInfo* shapeCastInfo, SceneQueryHit& outHit);
		int32_t OverlapShape(const ShapeOverlapInfo* shapeOverlapInfo, SceneQueryHit** outHits);

		void Teleport(Entity entity, const glm::vec3& targetPosition, const glm::quat& targetRotation, bool force = false);
		void AddRadialImpulse(const glm::vec3& origin, float radius, float strength, EFalloffMode falloff, bool velocityChange);
		void OnScenePostStart();

	private:
		Scene* m_Scene = nullptr;
		Scope<Impl> m_Impl;
	};

}
