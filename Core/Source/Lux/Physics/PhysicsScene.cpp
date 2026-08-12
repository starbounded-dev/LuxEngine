#include "lpch.h"
#include "PhysicsScene.h"

#include "PhysicsSystem.h"

#include "Lux/Asset/AssetManager.h"
#include "Lux/Physics/JoltPhysics/JoltBody.h"
#include "Lux/Physics/JoltPhysics/JoltCharacterController.h"
#include "Lux/Physics/JoltPhysics/JoltUtils.h"
#include "Lux/Physics/PhysicsBody.h"
#include "Lux/Project/Project.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"

#include <Jolt/Jolt.h>
#include <Jolt/Geometry/Triangle.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace Lux {

	struct RuntimeBody
	{
		JPH::BodyID BodyID;
		EBodyType BodyType = EBodyType::Static;
		EActorAxis LockedAxes = EActorAxis::None;
		JPH::SixDOFConstraint* AxisLockConstraint = nullptr;
		Ref<JoltBody> Body;
	};

	namespace {

		static constexpr uint32_t s_MaxCollisionSteps = 8;

		static JPH::Vec3 ToJoltVector(const glm::vec3& value)
		{
			return { value.x, value.y, value.z };
		}

		static JPH::Quat ToJoltQuat(const glm::quat& value)
		{
			return { value.x, value.y, value.z, value.w };
		}

		static glm::vec3 FromJoltVector(const JPH::Vec3& value)
		{
			return { value.GetX(), value.GetY(), value.GetZ() };
		}

		static glm::vec3 FromJoltRVector(const JPH::RVec3& value)
		{
			return { (float)value.GetX(), (float)value.GetY(), (float)value.GetZ() };
		}

		static glm::quat FromJoltQuat(const JPH::Quat& value)
		{
			return { value.GetW(), value.GetX(), value.GetY(), value.GetZ() };
		}

		static JPH::EMotionType ToJoltMotionType(EBodyType bodyType)
		{
			switch (bodyType)
			{
				case EBodyType::Static: return JPH::EMotionType::Static;
				case EBodyType::Dynamic: return JPH::EMotionType::Dynamic;
				case EBodyType::Kinematic: return JPH::EMotionType::Kinematic;
			}

			LUX_CORE_ASSERT(false, "Unknown 3D physics body type");
			return JPH::EMotionType::Static;
		}

		static JPH::EMotionQuality ToJoltMotionQuality(ECollisionDetectionType collisionDetection)
		{
			return collisionDetection == ECollisionDetectionType::Continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
		}

		static bool IsAllRotationLocked(EActorAxis lockedAxes)
		{
			return (lockedAxes & EActorAxis::RotationX) != EActorAxis::None
				&& (lockedAxes & EActorAxis::RotationY) != EActorAxis::None
				&& (lockedAxes & EActorAxis::RotationZ) != EActorAxis::None;
		}

		static void ApplyShapeDensity(JPH::ConvexShapeSettings& settings, float totalBodyMass, float volume, float materialDensity)
		{
			if (totalBodyMass > 0.0f && volume > 0.0f)
				settings.SetDensity(totalBodyMass / volume);
			else
				settings.SetDensity(materialDensity);
		}

		struct PhysicsLayerTable
		{
			std::vector<ProjectPhysicsLayer> Layers;
			std::unordered_map<std::string, uint32_t> LayerLookup;

			PhysicsLayerTable()
			{
				if (Ref<Project> project = Project::GetActive())
					Layers = project->GetConfig().Physics.Layers;

				if (Layers.empty())
				{
					ProjectPhysicsLayer defaultLayer;
					defaultLayer.Name = "Default";
					defaultLayer.CollidesWithSelf = true;
					Layers.emplace_back(std::move(defaultLayer));
				}

				for (uint32_t i = 0; i < Layers.size(); i++)
					LayerLookup[Layers[i].Name] = i;
			}

			uint32_t GetLayerCount() const
			{
				return std::max(1u, (uint32_t)Layers.size());
			}

			JPH::ObjectLayer Sanitize(JPH::ObjectLayer layer) const
			{
				if (layer >= Layers.size())
					return 0;

				return layer;
			}

			bool ShouldCollide(JPH::ObjectLayer layerA, JPH::ObjectLayer layerB) const
			{
				layerA = Sanitize(layerA);
				layerB = Sanitize(layerB);

				const auto& a = Layers[layerA];
				const auto& b = Layers[layerB];

				if (layerA == layerB)
					return a.CollidesWithSelf;

				auto containsLayer = [this](const ProjectPhysicsLayer& layer, const std::string& name)
				{
					return std::find(layer.CollidesWith.begin(), layer.CollidesWith.end(), name) != layer.CollidesWith.end();
				};

				return containsLayer(a, b.Name) || containsLayer(b, a.Name);
			}
		};

		class LuxBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
		{
		public:
			explicit LuxBroadPhaseLayerInterface(const PhysicsLayerTable& layerTable)
				: m_LayerTable(layerTable)
			{
			}

			JPH::uint GetNumBroadPhaseLayers() const override { return m_LayerTable.GetLayerCount(); }

			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
			{
				return JPH::BroadPhaseLayer((JPH::BroadPhaseLayer::Type)m_LayerTable.Sanitize(layer));
			}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
			{
				const uint32_t index = std::min<uint32_t>(layer.GetValue(), (uint32_t)m_LayerTable.Layers.size() - 1);
				return m_LayerTable.Layers[index].Name.c_str();
			}
#endif

		private:
			const PhysicsLayerTable& m_LayerTable;
		};

		class LuxObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			explicit LuxObjectVsBroadPhaseLayerFilter(const PhysicsLayerTable& layerTable)
				: m_LayerTable(layerTable)
			{
			}

			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
			{
				return m_LayerTable.ShouldCollide(layer, broadPhaseLayer.GetValue());
			}

		private:
			const PhysicsLayerTable& m_LayerTable;
		};

		class LuxObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
		{
		public:
			explicit LuxObjectLayerPairFilter(const PhysicsLayerTable& layerTable)
				: m_LayerTable(layerTable)
			{
			}

			bool ShouldCollide(JPH::ObjectLayer layerA, JPH::ObjectLayer layerB) const override
			{
				return m_LayerTable.ShouldCollide(layerA, layerB);
			}

		private:
			const PhysicsLayerTable& m_LayerTable;
		};

		class LuxBodyFilter final : public JPH::BodyFilter
		{
		public:
			explicit LuxBodyFilter(const ExcludedEntityMap& excludedEntities)
				: m_ExcludedEntities(excludedEntities)
			{
			}

			bool ShouldCollide(const JPH::BodyID&) const override { return true; }

			bool ShouldCollideLocked(const JPH::Body& body) const override
			{
				if (body.IsSensor())
					return false;

				return m_ExcludedEntities.find((UUID)body.GetUserData()) == m_ExcludedEntities.end();
			}

		private:
			const ExcludedEntityMap& m_ExcludedEntities;
		};

		struct ShapePart
		{
			JPH::RefConst<JPH::Shape> Shape;
			JPH::Vec3 Offset = JPH::Vec3::sZero();
			JPH::Quat Rotation = JPH::Quat::sIdentity();
			ColliderMaterial Material;
			bool RequiresStaticBody = false;
		};

		static JPH::RefConst<JPH::Shape> CreateShape(const JPH::ShapeSettings& settings, const char* label)
		{
			JPH::ShapeSettings::ShapeResult result = settings.Create();
			if (result.HasError())
			{
				LUX_CORE_ERROR_TAG("Physics", "Failed to create {} shape: {}", label, result.GetError().c_str());
				return nullptr;
			}

			return result.Get();
		}

		static void AppendBoxShape(Entity entity, const TransformComponent& transform, float totalBodyMass, std::vector<ShapePart>& shapes)
		{
			if (!entity.HasComponent<BoxColliderComponent>())
				return;

			const auto& collider = entity.GetComponent<BoxColliderComponent>();
			const glm::vec3 scale = glm::max(glm::abs(transform.Scale), glm::vec3(0.001f));
			const glm::vec3 halfSize = glm::max(collider.HalfSize * scale, glm::vec3(0.001f));

			JPH::BoxShapeSettings settings(ToJoltVector(halfSize));
			const float volume = (halfSize.x * 2.0f) * (halfSize.y * 2.0f) * (halfSize.z * 2.0f);
			ApplyShapeDensity(settings, totalBodyMass, volume, collider.Material.Density);
			JPH::RefConst<JPH::Shape> shape = CreateShape(settings, "box collider");
			if (!shape)
				return;

			auto& part = shapes.emplace_back();
			part.Shape = shape;
			part.Offset = ToJoltVector(collider.Offset * scale);
			part.Material = collider.Material;
		}

		static void AppendSphereShape(Entity entity, const TransformComponent& transform, float totalBodyMass, std::vector<ShapePart>& shapes)
		{
			if (!entity.HasComponent<SphereColliderComponent>())
				return;

			const auto& collider = entity.GetComponent<SphereColliderComponent>();
			const glm::vec3 scale = glm::max(glm::abs(transform.Scale), glm::vec3(0.001f));
			const float radius = std::max(0.001f, collider.Radius * std::max({ scale.x, scale.y, scale.z }));

			JPH::SphereShapeSettings settings(radius);
			const float volume = (4.0f / 3.0f) * glm::pi<float>() * radius * radius * radius;
			ApplyShapeDensity(settings, totalBodyMass, volume, collider.Material.Density);
			JPH::RefConst<JPH::Shape> shape = CreateShape(settings, "sphere collider");
			if (!shape)
				return;

			auto& part = shapes.emplace_back();
			part.Shape = shape;
			part.Offset = ToJoltVector(collider.Offset * scale);
			part.Material = collider.Material;
		}

		static void AppendCapsuleShape(Entity entity, const TransformComponent& transform, float totalBodyMass, std::vector<ShapePart>& shapes)
		{
			if (!entity.HasComponent<CapsuleColliderComponent>())
				return;

			const auto& collider = entity.GetComponent<CapsuleColliderComponent>();
			const glm::vec3 scale = glm::max(glm::abs(transform.Scale), glm::vec3(0.001f));
			const float radius = std::max(0.001f, collider.Radius * std::max(scale.x, scale.z));
			const float halfHeight = std::max(0.001f, collider.HalfHeight * scale.y);

			JPH::CapsuleShapeSettings settings(halfHeight, radius);
			const float cylinderVolume = glm::pi<float>() * radius * radius * (halfHeight * 2.0f);
			const float sphereVolume = (4.0f / 3.0f) * glm::pi<float>() * radius * radius * radius;
			const float volume = cylinderVolume + sphereVolume;
			ApplyShapeDensity(settings, totalBodyMass, volume, collider.Material.Density);
			JPH::RefConst<JPH::Shape> shape = CreateShape(settings, "capsule collider");
			if (!shape)
				return;

			auto& part = shapes.emplace_back();
			part.Shape = shape;
			part.Offset = ToJoltVector(collider.Offset * scale);
			part.Material = collider.Material;
		}

		static AssetHandle ResolveMeshColliderHandle(Entity entity, const MeshColliderComponent& collider)
		{
			if (collider.ColliderAsset)
				return collider.ColliderAsset;

			if (entity.HasComponent<StaticMeshComponent>())
				return entity.GetComponent<StaticMeshComponent>().StaticMesh;

			return 0;
		}

		static void AppendMeshTrianglesFromSubmesh(const MeshSource& meshSource, const Submesh& submesh, const glm::vec3& scale, JPH::TriangleList& triangles)
		{
			// Position accessors work whether the mesh kept its full CPU vertex
			// array (editor) or was compacted to positions-only (runtime).
			const size_t vertexCount = meshSource.GetVertexCount();
			const auto& indices = meshSource.GetIndices();
			if (vertexCount == 0 || indices.empty())
				return;

			const uint32_t firstTriangle = submesh.BaseIndex / 3;
			const uint32_t triangleCount = submesh.IndexCount / 3;
			const uint32_t lastTriangle = std::min<uint32_t>(firstTriangle + triangleCount, (uint32_t)indices.size());

			for (uint32_t triangleIndex = firstTriangle; triangleIndex < lastTriangle; triangleIndex++)
			{
				const Index& index = indices[triangleIndex];
				const uint32_t i0 = submesh.BaseVertex + index.V1;
				const uint32_t i1 = submesh.BaseVertex + index.V2;
				const uint32_t i2 = submesh.BaseVertex + index.V3;
				if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
					continue;

				const glm::vec3 p0 = glm::vec3(submesh.Transform * glm::vec4(meshSource.GetVertexPosition(i0), 1.0f)) * scale;
				const glm::vec3 p1 = glm::vec3(submesh.Transform * glm::vec4(meshSource.GetVertexPosition(i1), 1.0f)) * scale;
				const glm::vec3 p2 = glm::vec3(submesh.Transform * glm::vec4(meshSource.GetVertexPosition(i2), 1.0f)) * scale;

				triangles.push_back(JPH::Triangle(ToJoltVector(p0), ToJoltVector(p1), ToJoltVector(p2)));
			}
		}

		static void AppendMeshShape(Entity entity, const TransformComponent& transform, std::vector<ShapePart>& shapes)
		{
			if (!entity.HasComponent<MeshColliderComponent>())
				return;

			const auto& collider = entity.GetComponent<MeshColliderComponent>();
			const AssetHandle colliderHandle = ResolveMeshColliderHandle(entity, collider);
			if (!colliderHandle)
				return;

			Ref<MeshSource> meshSource;
			if (AssetManager::GetAssetType(colliderHandle) == AssetType::MeshSource)
			{
				meshSource = AssetManager::GetAsset<MeshSource>(colliderHandle);
			}
			else if (Ref<StaticMesh> staticMesh = StaticMesh::GetOrCreateRuntime(colliderHandle))
			{
				meshSource = AssetManager::GetAsset<MeshSource>(staticMesh->GetMeshSource());
			}

			if (!meshSource)
			{
				LUX_CORE_WARN_TAG("Physics", "Mesh collider on entity '{}' has no valid mesh source", entity.Name());
				return;
			}

			const auto& submeshes = meshSource->GetSubmeshes();
			if (submeshes.empty())
				return;

			const glm::vec3 scale = glm::max(glm::abs(transform.Scale), glm::vec3(0.001f));
			JPH::TriangleList triangles;
			if (collider.SubmeshIndex < submeshes.size())
			{
				AppendMeshTrianglesFromSubmesh(*meshSource, submeshes[collider.SubmeshIndex], scale, triangles);
			}
			else
			{
				for (const Submesh& submesh : submeshes)
					AppendMeshTrianglesFromSubmesh(*meshSource, submesh, scale, triangles);
			}

			if (triangles.empty())
			{
				LUX_CORE_WARN_TAG("Physics", "Mesh collider on entity '{}' produced no triangles", entity.Name());
				return;
			}

			JPH::MeshShapeSettings settings(triangles);
			JPH::RefConst<JPH::Shape> shape = CreateShape(settings, "mesh collider");
			if (!shape)
				return;

			auto& part = shapes.emplace_back();
			part.Shape = shape;
			part.Material = collider.Material;
			part.RequiresStaticBody = true;
		}

		static int CountPrimitiveColliders(Entity entity)
		{
			int count = 0;
			if (entity.HasComponent<BoxColliderComponent>()) count++;
			if (entity.HasComponent<SphereColliderComponent>()) count++;
			if (entity.HasComponent<CapsuleColliderComponent>()) count++;
			return count;
		}

		static JPH::RefConst<JPH::Shape> CreateEntityShape(Entity entity, const TransformComponent& transform, float totalBodyMass, ColliderMaterial& outMaterial, bool& outRequiresStaticBody)
		{
			const int primitiveCount = CountPrimitiveColliders(entity);
			const bool hasMesh = entity.HasComponent<MeshColliderComponent>();
			const float shapeMass = (primitiveCount == 1 && !hasMesh) ? totalBodyMass : 0.0f;

			std::vector<ShapePart> shapes;
			AppendBoxShape(entity, transform, shapeMass, shapes);
			AppendSphereShape(entity, transform, shapeMass, shapes);
			AppendCapsuleShape(entity, transform, shapeMass, shapes);
			AppendMeshShape(entity, transform, shapes);

			if (shapes.empty())
				return nullptr;

			outMaterial = shapes.front().Material;
			outRequiresStaticBody = false;

			if (shapes.size() == 1 && shapes.front().Offset.IsNearZero() && shapes.front().Rotation == JPH::Quat::sIdentity())
			{
				outRequiresStaticBody = shapes.front().RequiresStaticBody;
				return shapes.front().Shape;
			}

			JPH::StaticCompoundShapeSettings compoundSettings;
			for (const ShapePart& part : shapes)
			{
				compoundSettings.AddShape(part.Offset, part.Rotation, part.Shape);
				outMaterial.Friction = std::max(outMaterial.Friction, part.Material.Friction);
				outMaterial.Restitution = std::max(outMaterial.Restitution, part.Material.Restitution);
				outRequiresStaticBody |= part.RequiresStaticBody;
			}

			return CreateShape(compoundSettings, "compound collider");
		}

		static void CreateAxisLockConstraint(JPH::PhysicsSystem& system, JPH::Body& body, RuntimeBody& runtimeBody)
		{
			if (runtimeBody.LockedAxes == EActorAxis::None)
				return;

			JPH::SixDOFConstraintSettings constraintSettings;
			constraintSettings.mPosition1 = constraintSettings.mPosition2 = body.GetCenterOfMassPosition();

			if ((runtimeBody.LockedAxes & EActorAxis::TranslationX) != EActorAxis::None)
				constraintSettings.MakeFixedAxis(JPH::SixDOFConstraintSettings::EAxis::TranslationX);
			if ((runtimeBody.LockedAxes & EActorAxis::TranslationY) != EActorAxis::None)
				constraintSettings.MakeFixedAxis(JPH::SixDOFConstraintSettings::EAxis::TranslationY);
			if ((runtimeBody.LockedAxes & EActorAxis::TranslationZ) != EActorAxis::None)
				constraintSettings.MakeFixedAxis(JPH::SixDOFConstraintSettings::EAxis::TranslationZ);
			if ((runtimeBody.LockedAxes & EActorAxis::RotationX) != EActorAxis::None)
				constraintSettings.MakeFixedAxis(JPH::SixDOFConstraintSettings::EAxis::RotationX);
			if ((runtimeBody.LockedAxes & EActorAxis::RotationY) != EActorAxis::None)
				constraintSettings.MakeFixedAxis(JPH::SixDOFConstraintSettings::EAxis::RotationY);
			if ((runtimeBody.LockedAxes & EActorAxis::RotationZ) != EActorAxis::None)
				constraintSettings.MakeFixedAxis(JPH::SixDOFConstraintSettings::EAxis::RotationZ);

			runtimeBody.AxisLockConstraint = static_cast<JPH::SixDOFConstraint*>(constraintSettings.Create(JPH::Body::sFixedToWorld, body));
			system.AddConstraint(runtimeBody.AxisLockConstraint);
		}

		static void DestroyAxisLockConstraint(JPH::PhysicsSystem& system, RuntimeBody& runtimeBody)
		{
			if (!runtimeBody.AxisLockConstraint)
				return;

			system.RemoveConstraint(runtimeBody.AxisLockConstraint);
			runtimeBody.AxisLockConstraint = nullptr;
		}

	}

	struct PhysicsScene::Impl
	{
		PhysicsLayerTable LayerTable;
		LuxBroadPhaseLayerInterface BroadPhaseLayerInterface{ LayerTable };
		LuxObjectVsBroadPhaseLayerFilter ObjectVsBroadPhaseLayerFilter{ LayerTable };
		LuxObjectLayerPairFilter ObjectLayerPairFilter{ LayerTable };
		JPH::PhysicsSystem System;
		std::unordered_map<UUID, RuntimeBody> Bodies;
		std::unordered_map<UUID, Ref<CharacterController>> CharacterControllers;
		std::vector<SceneQueryHit> OverlapHitBuffer;
		float Accumulator = 0.0f;
		float FixedTimestep = 1.0f / 60.0f;
		uint32_t CollisionSteps = 0;
	};

	namespace {

		static void SubStepStrategy(PhysicsScene::Impl& impl, float timestep)
		{
			if (impl.Accumulator > impl.FixedTimestep)
				impl.Accumulator = 0.0f;

			impl.Accumulator += timestep;
			if (impl.Accumulator < impl.FixedTimestep)
			{
				impl.CollisionSteps = 0;
				return;
			}

			impl.CollisionSteps = (uint32_t)(impl.Accumulator / impl.FixedTimestep);
			impl.CollisionSteps = std::min(impl.CollisionSteps, s_MaxCollisionSteps);
			impl.Accumulator -= (float)impl.CollisionSteps * impl.FixedTimestep;
		}

		static void PreSimulate(Scene* scene, PhysicsScene::Impl& impl, float timestep)
		{
			// MoveKinematic() sets velocity = (target - current) / timestep, so a zero/negative step
			// divides by zero and drives the body to NaN - which hard-crashes Jolt on the next Update()
			// because it is built with floating-point exceptions enabled in every non-Dist config.
			if (timestep <= 0.0f)
				return;

			JPH::BodyInterface& bodyInterface = impl.System.GetBodyInterface();
			const JPH::BodyLockInterface& bodyLockInterface = impl.System.GetBodyLockInterface();

			for (auto& [entityID, runtimeBody] : impl.Bodies)
			{
				if (runtimeBody.BodyType != EBodyType::Kinematic)
					continue;

				Entity entity = scene->TryGetEntityWithUUID(entityID);
				if (!entity)
					continue;

				const TransformComponent worldTransform = scene->GetWorldSpaceTransform(entity);
				const glm::vec3 targetTranslation = worldTransform.Translation;
				glm::quat targetRotation = glm::normalize(worldTransform.GetRotation());

				JPH::BodyLockRead readLock(bodyLockInterface, runtimeBody.BodyID);
				if (!readLock.Succeeded())
					continue;

				const JPH::Body& body = readLock.GetBody();
				const glm::vec3 currentTranslation = FromJoltRVector(body.GetPosition());
				const glm::quat currentRotation = FromJoltQuat(body.GetRotation());

				if (glm::dot(currentRotation, targetRotation) < 0.0f)
					targetRotation = -targetRotation;

				if (glm::length(currentTranslation - targetTranslation) <= 0.00001f
					&& glm::length(currentRotation - targetRotation) <= 0.00001f)
				{
					continue;
				}

				if (!body.IsActive())
					bodyInterface.ActivateBody(runtimeBody.BodyID);

				readLock.ReleaseLock();

				JPH::BodyLockWrite writeLock(bodyLockInterface, runtimeBody.BodyID);
				if (!writeLock.Succeeded())
					continue;

				JPH::Body& kinematicBody = writeLock.GetBody();
				kinematicBody.MoveKinematic(ToJoltVector(targetTranslation), ToJoltQuat(targetRotation), timestep);

				// Safety net: clamp the velocity MoveKinematic() just set to the body's configured max
				// linear/angular velocity. If a bad target or timestep ever produces a huge chase velocity,
				// the body lags toward its target instead of exploding to NaN (and crashing Jolt). Normal
				// per-frame tracking stays well under these limits, so this is a no-op in the common case.
				kinematicBody.SetLinearVelocityClamped(kinematicBody.GetLinearVelocity());
				kinematicBody.SetAngularVelocityClamped(kinematicBody.GetAngularVelocity());
			}
		}

		static void SyncActiveBodies(Scene* scene, PhysicsScene::Impl& impl)
		{
			const JPH::BodyLockInterface& bodyLockInterface = impl.System.GetBodyLockInterface();
			JPH::BodyIDVector activeBodies;
			impl.System.GetActiveBodies(JPH::EBodyType::RigidBody, activeBodies);

			if (activeBodies.empty())
				return;

			JPH::BodyLockMultiRead activeBodiesLock(bodyLockInterface, activeBodies.data(), (int)activeBodies.size());
			for (int i = 0; i < (int)activeBodies.size(); i++)
			{
				const JPH::Body* body = activeBodiesLock.GetBody(i);
				if (!body || body->IsKinematic())
					continue;

				const UUID entityID = (UUID)body->GetUserData();
				auto bodyIt = impl.Bodies.find(entityID);
				if (bodyIt == impl.Bodies.end())
					continue;

				Entity entity = scene->TryGetEntityWithUUID(entityID);
				if (!entity)
					continue;

				auto& transform = entity.GetComponent<TransformComponent>();
				const glm::vec3 localScale = transform.Scale;
				const glm::vec3 worldTranslation = FromJoltRVector(body->GetPosition());
				glm::quat worldRotation = transform.GetRotation();

				if (!IsAllRotationLocked(bodyIt->second.LockedAxes))
					worldRotation = FromJoltQuat(body->GetRotation());

				glm::mat4 worldTransform = glm::translate(glm::mat4(1.0f), worldTranslation)
					* glm::toMat4(worldRotation)
					* glm::scale(glm::mat4(1.0f), localScale);

				if (Entity parent = entity.GetParent())
					worldTransform = glm::inverse(scene->GetWorldSpaceTransformMatrix(parent)) * worldTransform;

				transform.SetTransform(worldTransform);
				transform.Scale = localScale;
			}
		}

	}

	PhysicsScene::PhysicsScene(Scene* scene)
		: m_Scene(scene)
	{
	}

	PhysicsScene::~PhysicsScene()
	{
		Stop();
	}

	void PhysicsScene::Start()
	{
		if (!m_Scene)
			return;

		if (!PhysicsSystem::IsInitialized())
			PhysicsSystem::Init();

		Stop();
		m_Impl = CreateScope<Impl>();

		ProjectPhysicsSettings settings;
		if (Ref<Project> project = Project::GetActive())
			settings = project->GetConfig().Physics;

		m_Impl->FixedTimestep = std::max(0.001f, settings.FixedTimestep);

		JPH::PhysicsSettings joltSettings;
		joltSettings.mNumPositionSteps = std::max(1u, settings.PositionSolverIterations);
		joltSettings.mNumVelocitySteps = std::max(1u, settings.VelocitySolverIterations);
		m_Impl->System.SetPhysicsSettings(joltSettings);
		m_Impl->System.Init(
			std::max(1u, settings.MaxBodies),
			0,
			std::max(1024u, settings.MaxBodies * 2),
			std::max(1024u, settings.MaxBodies * 2),
			m_Impl->BroadPhaseLayerInterface,
			m_Impl->ObjectVsBroadPhaseLayerFilter,
			m_Impl->ObjectLayerPairFilter);
		m_Impl->System.SetGravity(ToJoltVector(settings.Gravity));

		auto view = m_Scene->GetAllEntitiesWith<TransformComponent>();
		for (auto entityHandle : view)
		{
			Entity entity = { entityHandle, m_Scene };
			if (!entity.HasAny<BoxColliderComponent, SphereColliderComponent, CapsuleColliderComponent, MeshColliderComponent>())
				continue;

			TransformComponent transform = m_Scene->GetWorldSpaceTransform(entity);
			const RigidBodyComponent* rigidBody = entity.TryGetComponent<RigidBodyComponent>();

			ColliderMaterial material;
			bool requiresStaticBody = false;
			const float bodyMass = (rigidBody && rigidBody->BodyType == EBodyType::Dynamic) ? rigidBody->Mass : 0.0f;
			JPH::RefConst<JPH::Shape> shape = CreateEntityShape(entity, transform, bodyMass, material, requiresStaticBody);
			if (!shape)
				continue;

			EBodyType bodyType = rigidBody ? rigidBody->BodyType : EBodyType::Static;
			if (requiresStaticBody && bodyType != EBodyType::Static)
			{
				LUX_CORE_WARN_TAG("Physics", "Mesh collider on entity '{}' requires a static body; forcing static motion type", entity.Name());
				bodyType = EBodyType::Static;
			}

			const uint32_t requestedLayer = rigidBody ? rigidBody->LayerID : 0;
			const JPH::ObjectLayer layer = m_Impl->LayerTable.Sanitize((JPH::ObjectLayer)requestedLayer);

			JPH::BodyCreationSettings bodySettings(shape, ToJoltVector(transform.Translation), ToJoltQuat(glm::normalize(transform.GetRotation())), ToJoltMotionType(bodyType), layer);
			bodySettings.mUserData = (JPH::uint64)entity.GetUUID();
			bodySettings.mFriction = material.Friction;
			bodySettings.mRestitution = material.Restitution;

			if (rigidBody)
			{
				if (rigidBody->BodyType == EBodyType::Dynamic && rigidBody->Mass > 0.0f)
				{
					bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
					bodySettings.mMassPropertiesOverride.mMass = rigidBody->Mass;
				}

				bodySettings.mLinearDamping = rigidBody->LinearDrag;
				bodySettings.mAngularDamping = rigidBody->AngularDrag;
				bodySettings.mGravityFactor = rigidBody->DisableGravity ? 0.0f : 1.0f;
				bodySettings.mMotionQuality = ToJoltMotionQuality(rigidBody->CollisionDetection);
				bodySettings.mLinearVelocity = ToJoltVector(rigidBody->InitialLinearVelocity);
				bodySettings.mAngularVelocity = ToJoltVector(rigidBody->InitialAngularVelocity);
				bodySettings.mMaxLinearVelocity = rigidBody->MaxLinearVelocity;
				bodySettings.mMaxAngularVelocity = rigidBody->MaxAngularVelocity;
				bodySettings.mIsSensor = rigidBody->IsTrigger;
			}

			JPH::BodyInterface& bodyInterface = m_Impl->System.GetBodyInterface();
			JPH::BodyID bodyID = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::Activate);
			if (bodyID.IsInvalid())
			{
				LUX_CORE_ERROR_TAG("Physics", "Failed to create Jolt body for entity '{}'", entity.Name());
				continue;
			}

			RuntimeBody runtimeBody;
			runtimeBody.BodyID = bodyID;
			runtimeBody.BodyType = bodyType;
			runtimeBody.Body = Ref<JoltBody>::Create(entity, bodyInterface, m_Impl->System.GetBodyLockInterface(), bodyID);
			if (rigidBody)
				runtimeBody.LockedAxes = rigidBody->LockedAxes;

			if (bodyType != EBodyType::Static && runtimeBody.LockedAxes != EActorAxis::None)
			{
				JPH::BodyLockWrite bodyLock(m_Impl->System.GetBodyLockInterface(), bodyID);
				if (bodyLock.Succeeded())
					CreateAxisLockConstraint(m_Impl->System, bodyLock.GetBody(), runtimeBody);
			}

			m_Impl->Bodies[entity.GetUUID()] = runtimeBody;
			if (rigidBody)
				const_cast<RigidBodyComponent*>(rigidBody)->RuntimeBody = (void*)(uintptr_t)bodyID.GetIndexAndSequenceNumber();
		}

		auto characterView = m_Scene->GetAllEntitiesWith<TransformComponent, CharacterControllerComponent>();
		for (auto entityHandle : characterView)
		{
			Entity entity = { entityHandle, m_Scene };
			m_Impl->CharacterControllers[entity.GetUUID()] = Ref<JoltCharacterController>::Create(entity);
		}

		m_Impl->System.OptimizeBroadPhase();
	}

	void PhysicsScene::Stop()
	{
		if (!m_Impl)
			return;

		JPH::BodyInterface& bodyInterface = m_Impl->System.GetBodyInterface();
		for (auto& [entityID, runtimeBody] : m_Impl->Bodies)
		{
			(void)entityID;
			DestroyAxisLockConstraint(m_Impl->System, runtimeBody);

			if (!runtimeBody.BodyID.IsInvalid())
			{
				bodyInterface.RemoveBody(runtimeBody.BodyID);
				bodyInterface.DestroyBody(runtimeBody.BodyID);
			}
		}

		m_Impl.reset();
	}

	void PhysicsScene::Simulate(Timestep timestep)
	{
		if (!m_Impl || !m_Scene)
			return;

		SubStepStrategy(*m_Impl, timestep);
		const float physicsTimestep = m_Impl->FixedTimestep * (float)m_Impl->CollisionSteps;
		PreSimulate(m_Scene, *m_Impl, physicsTimestep > 0.0f ? physicsTimestep : (float)timestep);

		if (m_Impl->CollisionSteps > 0)
		{
			for (auto& [entityID, controller] : m_Impl->CharacterControllers)
			{
				(void)entityID;
				controller->PreSimulate(physicsTimestep);
			}

			m_Impl->System.Update(
				physicsTimestep,
				(int)m_Impl->CollisionSteps,
				PhysicsSystem::GetTempAllocator(),
				PhysicsSystem::GetJobSystem());

			for (auto& [entityID, controller] : m_Impl->CharacterControllers)
			{
				(void)entityID;
				controller->Simulate(physicsTimestep);
				controller->PostSimulate();
			}
		}

		SyncActiveBodies(m_Scene, *m_Impl);
	}

	void PhysicsScene::Destroy()
	{
		Stop();
	}

	void PhysicsScene::SimulateStep(float fixedTimestep)
	{
		if (!m_Impl || !m_Scene)
			return;

		PreSimulate(m_Scene, *m_Impl, fixedTimestep);
		for (auto& [entityID, controller] : m_Impl->CharacterControllers)
		{
			(void)entityID;
			controller->PreSimulate(fixedTimestep);
		}

		m_Impl->System.Update(fixedTimestep, 1, PhysicsSystem::GetTempAllocator(), PhysicsSystem::GetJobSystem());

		for (auto& [entityID, controller] : m_Impl->CharacterControllers)
		{
			(void)entityID;
			controller->Simulate(fixedTimestep);
			controller->PostSimulate();
		}

		SyncActiveBodies(m_Scene, *m_Impl);
	}

	glm::vec3 PhysicsScene::GetGravity() const
	{
		return m_Impl ? FromJoltVector(m_Impl->System.GetGravity()) : glm::vec3(0.0f, -9.81f, 0.0f);
	}

	void PhysicsScene::SetGravity(const glm::vec3& gravity)
	{
		if (m_Impl)
			m_Impl->System.SetGravity(ToJoltVector(gravity));
	}

	Ref<PhysicsBody> PhysicsScene::GetBodyByEntityID(UUID entityID) const
	{
		if (!m_Impl)
			return nullptr;

		auto it = m_Impl->Bodies.find(entityID);
		return it == m_Impl->Bodies.end() ? nullptr : it->second.Body.As<PhysicsBody>();
	}

	Ref<PhysicsBody> PhysicsScene::GetBody(Entity entity) const
	{
		return entity ? GetBodyByEntityID(entity.GetUUID()) : nullptr;
	}

	Ref<PhysicsBody> PhysicsScene::CreateBody(Entity entity)
	{
		if (!entity || !m_Impl)
			return nullptr;

		if (Ref<PhysicsBody> existingBody = GetBody(entity))
			return existingBody;

		if (!entity.HasAny<BoxColliderComponent, SphereColliderComponent, CapsuleColliderComponent, MeshColliderComponent>())
		{
			LUX_CORE_WARN_TAG("Physics", "Cannot create physics body for entity '{}' without a 3D collider", entity.Name());
			return nullptr;
		}

		TransformComponent transform = m_Scene->GetWorldSpaceTransform(entity);
		const RigidBodyComponent* rigidBody = entity.TryGetComponent<RigidBodyComponent>();

		ColliderMaterial material;
		bool requiresStaticBody = false;
		const float bodyMass = (rigidBody && rigidBody->BodyType == EBodyType::Dynamic) ? rigidBody->Mass : 0.0f;
		JPH::RefConst<JPH::Shape> shape = CreateEntityShape(entity, transform, bodyMass, material, requiresStaticBody);
		if (!shape)
			return nullptr;

		EBodyType bodyType = rigidBody ? rigidBody->BodyType : EBodyType::Static;
		if (requiresStaticBody && bodyType != EBodyType::Static)
		{
			LUX_CORE_WARN_TAG("Physics", "Mesh collider on entity '{}' requires a static body; forcing static motion type", entity.Name());
			bodyType = EBodyType::Static;
		}

		const uint32_t requestedLayer = rigidBody ? rigidBody->LayerID : 0;
		const JPH::ObjectLayer layer = m_Impl->LayerTable.Sanitize((JPH::ObjectLayer)requestedLayer);
		JPH::BodyCreationSettings bodySettings(shape, ToJoltVector(transform.Translation), ToJoltQuat(glm::normalize(transform.GetRotation())), ToJoltMotionType(bodyType), layer);
		bodySettings.mUserData = (JPH::uint64)entity.GetUUID();
		bodySettings.mFriction = material.Friction;
		bodySettings.mRestitution = material.Restitution;

		if (rigidBody)
		{
			if (rigidBody->BodyType == EBodyType::Dynamic && rigidBody->Mass > 0.0f)
			{
				bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
				bodySettings.mMassPropertiesOverride.mMass = rigidBody->Mass;
			}

			bodySettings.mLinearDamping = rigidBody->LinearDrag;
			bodySettings.mAngularDamping = rigidBody->AngularDrag;
			bodySettings.mGravityFactor = rigidBody->DisableGravity ? 0.0f : 1.0f;
			bodySettings.mMotionQuality = ToJoltMotionQuality(rigidBody->CollisionDetection);
			bodySettings.mLinearVelocity = ToJoltVector(rigidBody->InitialLinearVelocity);
			bodySettings.mAngularVelocity = ToJoltVector(rigidBody->InitialAngularVelocity);
			bodySettings.mMaxLinearVelocity = rigidBody->MaxLinearVelocity;
			bodySettings.mMaxAngularVelocity = rigidBody->MaxAngularVelocity;
			bodySettings.mIsSensor = rigidBody->IsTrigger;
		}

		JPH::BodyInterface& bodyInterface = m_Impl->System.GetBodyInterface();
		JPH::BodyID bodyID = bodyInterface.CreateAndAddBody(bodySettings, JPH::EActivation::Activate);
		if (bodyID.IsInvalid())
		{
			LUX_CORE_ERROR_TAG("Physics", "Failed to create Jolt body for entity '{}'", entity.Name());
			return nullptr;
		}

		RuntimeBody runtimeBody;
		runtimeBody.BodyID = bodyID;
		runtimeBody.BodyType = bodyType;
		runtimeBody.Body = Ref<JoltBody>::Create(entity, bodyInterface, m_Impl->System.GetBodyLockInterface(), bodyID);
		if (rigidBody)
			runtimeBody.LockedAxes = rigidBody->LockedAxes;

		if (bodyType != EBodyType::Static && runtimeBody.LockedAxes != EActorAxis::None)
		{
			JPH::BodyLockWrite bodyLock(m_Impl->System.GetBodyLockInterface(), bodyID);
			if (bodyLock.Succeeded())
				CreateAxisLockConstraint(m_Impl->System, bodyLock.GetBody(), runtimeBody);
		}

		m_Impl->Bodies[entity.GetUUID()] = runtimeBody;
		if (rigidBody)
			const_cast<RigidBodyComponent*>(rigidBody)->RuntimeBody = (void*)(uintptr_t)bodyID.GetIndexAndSequenceNumber();

		return runtimeBody.Body.As<PhysicsBody>();
	}

	void PhysicsScene::DestroyBody(Entity entity)
	{
		if (!entity || !m_Impl)
			return;

		auto it = m_Impl->Bodies.find(entity.GetUUID());
		if (it == m_Impl->Bodies.end())
			return;

		DestroyAxisLockConstraint(m_Impl->System, it->second);
		JPH::BodyInterface& bodyInterface = m_Impl->System.GetBodyInterface();
		if (!it->second.BodyID.IsInvalid())
		{
			bodyInterface.RemoveBody(it->second.BodyID);
			bodyInterface.DestroyBody(it->second.BodyID);
		}

		if (entity.HasComponent<RigidBodyComponent>())
			entity.GetComponent<RigidBodyComponent>().RuntimeBody = nullptr;

		m_Impl->Bodies.erase(it);
	}

	void PhysicsScene::SetBodyType(Entity entity, EBodyType bodyType)
	{
		if (!entity || !m_Impl)
			return;

		auto it = m_Impl->Bodies.find(entity.GetUUID());
		if (it == m_Impl->Bodies.end())
			return;

		JPH::EActivation activation = bodyType == EBodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;
		m_Impl->System.GetBodyInterface().SetMotionType(it->second.BodyID, ToJoltMotionType(bodyType), activation);
		it->second.BodyType = bodyType;
		if (entity.HasComponent<RigidBodyComponent>())
			entity.GetComponent<RigidBodyComponent>().BodyType = bodyType;
	}

	Ref<CharacterController> PhysicsScene::GetCharacterControllerByEntityID(UUID entityID) const
	{
		if (!m_Impl)
			return nullptr;

		auto it = m_Impl->CharacterControllers.find(entityID);
		return it == m_Impl->CharacterControllers.end() ? nullptr : it->second;
	}

	Ref<CharacterController> PhysicsScene::GetCharacterController(Entity entity) const
	{
		return entity ? GetCharacterControllerByEntityID(entity.GetUUID()) : nullptr;
	}

	Ref<CharacterController> PhysicsScene::CreateCharacterController(Entity entity)
	{
		if (!entity || !m_Impl)
			return nullptr;

		if (Ref<CharacterController> existingController = GetCharacterController(entity))
			return existingController;

		Ref<CharacterController> controller = Ref<JoltCharacterController>::Create(entity);
		m_Impl->CharacterControllers[entity.GetUUID()] = controller;
		return controller;
	}

	void PhysicsScene::DestroyCharacterController(Entity entity)
	{
		if (entity && m_Impl)
			m_Impl->CharacterControllers.erase(entity.GetUUID());
	}

	bool PhysicsScene::CastRay(const RayCastInfo* rayCastInfo, SceneQueryHit& outHit)
	{
		outHit.Clear();
		if (!m_Impl || !rayCastInfo)
			return false;

		const float rayDirectionLength = glm::length(rayCastInfo->Direction);
		if (rayDirectionLength <= 0.0001f)
			return false;
		glm::vec3 direction = rayCastInfo->Direction / rayDirectionLength;

		JPH::RayCast ray;
		ray.mOrigin = ToJoltVector(rayCastInfo->Origin);
		ray.mDirection = ToJoltVector(direction) * rayCastInfo->MaxDistance;

		JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
		JPH::RayCastSettings raySettings;
		m_Impl->System.GetNarrowPhaseQuery().CastRay(JPH::RRayCast(ray), raySettings, collector, {}, {}, LuxBodyFilter(rayCastInfo->ExcludedEntities));

		if (!collector.HadHit())
			return false;

		JPH::BodyLockRead bodyLock(m_Impl->System.GetBodyLockInterface(), collector.mHit.mBodyID);
		if (!bodyLock.Succeeded())
			return false;

		const JPH::Body& body = bodyLock.GetBody();
		const JPH::Vec3 hitPosition = ray.GetPointOnRay(collector.mHit.mFraction);
		outHit.HitEntity = (UUID)body.GetUserData();
		outHit.Position = FromJoltVector(hitPosition);
		outHit.Normal = FromJoltVector(body.GetWorldSpaceSurfaceNormal(collector.mHit.mSubShapeID2, hitPosition));
		outHit.Distance = glm::distance(rayCastInfo->Origin, outHit.Position);
		return true;
	}

	bool PhysicsScene::CastShape(const ShapeCastInfo* shapeCastInfo, SceneQueryHit& outHit)
	{
		outHit.Clear();
		if (!m_Impl || !shapeCastInfo)
			return false;

		JPH::Ref<JPH::Shape> shape = nullptr;
		switch (shapeCastInfo->GetCastType())
		{
			case ShapeCastType::Box:
			{
				const auto* boxCastInfo = static_cast<const BoxCastInfo*>(shapeCastInfo);
				shape = new JPH::BoxShape(ToJoltVector(boxCastInfo->HalfExtent));
				break;
			}
			case ShapeCastType::Sphere:
			{
				const auto* sphereCastInfo = static_cast<const SphereCastInfo*>(shapeCastInfo);
				shape = new JPH::SphereShape(sphereCastInfo->Radius);
				break;
			}
			case ShapeCastType::Capsule:
			{
				const auto* capsuleCastInfo = static_cast<const CapsuleCastInfo*>(shapeCastInfo);
				shape = new JPH::CapsuleShape(capsuleCastInfo->HalfHeight, capsuleCastInfo->Radius);
				break;
			}
		}

		if (!shape)
			return false;

		const float shapeDirectionLength = glm::length(shapeCastInfo->Direction);
		if (shapeDirectionLength <= 0.0001f)
			return false;
		glm::vec3 direction = shapeCastInfo->Direction / shapeDirectionLength;

		JPH::ShapeCast shapeCast = JPH::ShapeCast::sFromWorldTransform(
			shape,
			JPH::Vec3::sReplicate(1.0f),
			JPH::Mat44::sTranslation(ToJoltVector(shapeCastInfo->Origin)),
			ToJoltVector(direction) * shapeCastInfo->MaxDistance);

		JPH::ShapeCastSettings settings;
		JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
		m_Impl->System.GetNarrowPhaseQuery().CastShape(JPH::RShapeCast(shapeCast), settings, JPH::RVec3::sZero(), collector, {}, {}, LuxBodyFilter(shapeCastInfo->ExcludedEntities));

		if (!collector.HadHit())
			return false;

		JPH::BodyLockRead bodyLock(m_Impl->System.GetBodyLockInterface(), collector.mHit.mBodyID2);
		if (!bodyLock.Succeeded())
			return false;

		const JPH::Body& body = bodyLock.GetBody();
		outHit.HitEntity = (UUID)body.GetUserData();
		outHit.Position = shapeCastInfo->Origin + direction * shapeCastInfo->MaxDistance * collector.mHit.mFraction;
		outHit.Normal = FromJoltVector(body.GetWorldSpaceSurfaceNormal(collector.mHit.mSubShapeID2, ToJoltVector(outHit.Position)));
		outHit.Distance = glm::distance(shapeCastInfo->Origin, outHit.Position);
		return true;
	}

	int32_t PhysicsScene::OverlapShape(const ShapeOverlapInfo* shapeOverlapInfo, SceneQueryHit** outHits)
	{
		if (!m_Impl || !shapeOverlapInfo || !outHits)
			return 0;

		m_Impl->OverlapHitBuffer.clear();
		JPH::Ref<JPH::Shape> shape = nullptr;
		switch (shapeOverlapInfo->GetCastType())
		{
			case ShapeCastType::Box:
			{
				const auto* boxOverlapInfo = static_cast<const BoxOverlapInfo*>(shapeOverlapInfo);
				shape = new JPH::BoxShape(ToJoltVector(boxOverlapInfo->HalfExtent));
				break;
			}
			case ShapeCastType::Sphere:
			{
				const auto* sphereOverlapInfo = static_cast<const SphereOverlapInfo*>(shapeOverlapInfo);
				shape = new JPH::SphereShape(sphereOverlapInfo->Radius);
				break;
			}
			case ShapeCastType::Capsule:
			{
				const auto* capsuleOverlapInfo = static_cast<const CapsuleOverlapInfo*>(shapeOverlapInfo);
				shape = new JPH::CapsuleShape(capsuleOverlapInfo->HalfHeight, capsuleOverlapInfo->Radius);
				break;
			}
		}

		if (!shape)
			return 0;

		JPH::CollideShapeSettings settings;
		JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
		m_Impl->System.GetNarrowPhaseQuery().CollideShape(
			shape,
			JPH::Vec3::sReplicate(1.0f),
			JPH::RMat44::sTranslation(JoltUtils::ToJoltRVec3(shapeOverlapInfo->Origin)),
			settings,
			JPH::RVec3::sZero(),
			collector,
			{}, {},
			LuxBodyFilter(shapeOverlapInfo->ExcludedEntities));

		for (const auto& hit : collector.mHits)
		{
			JPH::BodyLockRead bodyLock(m_Impl->System.GetBodyLockInterface(), hit.mBodyID2);
			if (!bodyLock.Succeeded())
				continue;

			const JPH::Body& body = bodyLock.GetBody();
			SceneQueryHit& queryHit = m_Impl->OverlapHitBuffer.emplace_back();
			queryHit.HitEntity = (UUID)body.GetUserData();
			queryHit.Position = FromJoltVector(hit.mContactPointOn2);
			queryHit.Normal = FromJoltVector(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hit.mContactPointOn2));
			queryHit.Distance = glm::distance(shapeOverlapInfo->Origin, queryHit.Position);
		}

		*outHits = m_Impl->OverlapHitBuffer.data();
		return (int32_t)m_Impl->OverlapHitBuffer.size();
	}

	void PhysicsScene::Teleport(Entity entity, const glm::vec3& targetPosition, const glm::quat& targetRotation, bool force)
	{
		if (!entity || !m_Impl)
			return;

		if (Ref<PhysicsBody> body = GetBody(entity))
		{
			auto joltBody = body.As<JoltBody>();
			const JPH::EActivation activation = force ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;
			m_Impl->System.GetBodyInterface().SetPositionAndRotation(joltBody->GetBodyID(), JoltUtils::ToJoltRVec3(targetPosition), ToJoltQuat(targetRotation), activation);
			return;
		}

		if (Ref<CharacterController> controller = GetCharacterController(entity))
		{
			controller->SetTranslation(targetPosition);
			controller->SetRotation(targetRotation);
		}
	}

	void PhysicsScene::AddRadialImpulse(const glm::vec3& origin, float radius, float strength, EFalloffMode falloff, bool velocityChange)
	{
		if (!m_Impl)
			return;

		for (auto& [entityID, runtimeBody] : m_Impl->Bodies)
		{
			(void)entityID;
			if (runtimeBody.Body && runtimeBody.Body->IsDynamic())
				runtimeBody.Body->AddRadialImpulse(origin, radius, strength, falloff, velocityChange);
		}
	}

	void PhysicsScene::OnScenePostStart()
	{
	}

}
