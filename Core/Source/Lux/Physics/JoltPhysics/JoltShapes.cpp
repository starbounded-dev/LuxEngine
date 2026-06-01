#include "lpch.h"
#include "JoltShapes.h"

#include "JoltUtils.h"
#include "Lux/Asset/AssetManager.h"
#include "Lux/Renderer/Mesh.h"
#include "Lux/Scene/Components.h"
#include "Lux/Scene/Scene.h"

#include <Jolt/Geometry/Triangle.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <glm/gtc/constants.hpp>

namespace Lux {

	namespace {

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

		static void ApplyDensity(JPH::ConvexShapeSettings& settings, float bodyMass, float volume, float materialDensity)
		{
			if (bodyMass > 0.0f && volume > 0.0f)
				settings.SetDensity(bodyMass / volume);
			else
				settings.SetDensity(materialDensity);
		}

		static AssetHandle ResolveMeshColliderHandle(Entity entity, const MeshColliderComponent& collider)
		{
			if (collider.ColliderAsset)
				return collider.ColliderAsset;

			if (entity.HasComponent<StaticMeshComponent>())
				return entity.GetComponent<StaticMeshComponent>().StaticMesh;

			return 0;
		}

		static void AppendSubmeshTriangles(const MeshSource& meshSource, const Submesh& submesh, const glm::vec3& scale, JPH::TriangleList& triangles)
		{
			const auto& vertices = meshSource.GetVertices();
			const auto& indices = meshSource.GetIndices();
			const uint32_t firstTriangle = submesh.BaseIndex / 3;
			const uint32_t triangleCount = submesh.IndexCount / 3;
			const uint32_t lastTriangle = std::min<uint32_t>(firstTriangle + triangleCount, (uint32_t)indices.size());

			for (uint32_t triangleIndex = firstTriangle; triangleIndex < lastTriangle; triangleIndex++)
			{
				const Index& index = indices[triangleIndex];
				const uint32_t i0 = submesh.BaseVertex + index.V1;
				const uint32_t i1 = submesh.BaseVertex + index.V2;
				const uint32_t i2 = submesh.BaseVertex + index.V3;
				if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
					continue;

				const glm::vec3 p0 = glm::vec3(submesh.Transform * glm::vec4(vertices[i0].Position, 1.0f)) * scale;
				const glm::vec3 p1 = glm::vec3(submesh.Transform * glm::vec4(vertices[i1].Position, 1.0f)) * scale;
				const glm::vec3 p2 = glm::vec3(submesh.Transform * glm::vec4(vertices[i2].Position, 1.0f)) * scale;
				triangles.emplace_back(JoltUtils::ToJoltVector(p0), JoltUtils::ToJoltVector(p1), JoltUtils::ToJoltVector(p2));
			}
		}

	}

	JoltBoxShape::JoltBoxShape(Entity entity, float totalBodyMass)
		: BoxShape(entity)
	{
		const auto& transform = entity.GetScene()->GetWorldSpaceTransform(entity);
		const auto& collider = entity.GetComponent<BoxColliderComponent>();
		const glm::vec3 scale = glm::max(glm::abs(transform.Scale), glm::vec3(0.001f));
		m_HalfSize = glm::max(collider.HalfSize * scale, glm::vec3(0.001f));
		m_Material = collider.Material;

		JPH::BoxShapeSettings settings(JoltUtils::ToJoltVector(m_HalfSize));
		const float volume = (m_HalfSize.x * 2.0f) * (m_HalfSize.y * 2.0f) * (m_HalfSize.z * 2.0f);
		ApplyDensity(settings, totalBodyMass, volume, m_Material.Density);
		m_Shape = CreateShape(settings, "box collider");
	}

	JoltSphereShape::JoltSphereShape(Entity entity, float totalBodyMass)
		: SphereShape(entity)
	{
		const auto& transform = entity.GetScene()->GetWorldSpaceTransform(entity);
		const auto& collider = entity.GetComponent<SphereColliderComponent>();
		const glm::vec3 scale = glm::max(glm::abs(transform.Scale), glm::vec3(0.001f));
		m_Radius = std::max(0.001f, collider.Radius * std::max({ scale.x, scale.y, scale.z }));
		m_Material = collider.Material;

		JPH::SphereShapeSettings settings(m_Radius);
		const float volume = (4.0f / 3.0f) * glm::pi<float>() * m_Radius * m_Radius * m_Radius;
		ApplyDensity(settings, totalBodyMass, volume, m_Material.Density);
		m_Shape = CreateShape(settings, "sphere collider");
	}

	JoltCapsuleShape::JoltCapsuleShape(Entity entity, float totalBodyMass)
		: CapsuleShape(entity)
	{
		const auto& transform = entity.GetScene()->GetWorldSpaceTransform(entity);
		const auto& collider = entity.GetComponent<CapsuleColliderComponent>();
		const glm::vec3 scale = glm::max(glm::abs(transform.Scale), glm::vec3(0.001f));
		m_Radius = std::max(0.001f, collider.Radius * std::max(scale.x, scale.z));
		m_HalfHeight = std::max(0.001f, collider.HalfHeight * scale.y);
		m_Material = collider.Material;

		JPH::CapsuleShapeSettings settings(m_HalfHeight, m_Radius);
		const float cylinderVolume = glm::pi<float>() * m_Radius * m_Radius * (m_HalfHeight * 2.0f);
		const float sphereVolume = (4.0f / 3.0f) * glm::pi<float>() * m_Radius * m_Radius * m_Radius;
		ApplyDensity(settings, totalBodyMass, cylinderVolume + sphereVolume, m_Material.Density);
		m_Shape = CreateShape(settings, "capsule collider");
	}

	JoltTriangleMeshShape::JoltTriangleMeshShape(Entity entity)
		: TriangleMeshShape(entity)
	{
		const auto& collider = entity.GetComponent<MeshColliderComponent>();
		const AssetHandle handle = ResolveMeshColliderHandle(entity, collider);
		if (!handle)
			return;

		Ref<MeshSource> meshSource;
		if (AssetManager::GetAssetType(handle) == AssetType::MeshSource)
			meshSource = AssetManager::GetAsset<MeshSource>(handle);
		else if (Ref<StaticMesh> staticMesh = StaticMesh::GetOrCreateRuntime(handle))
			meshSource = AssetManager::GetAsset<MeshSource>(staticMesh->GetMeshSource());

		if (!meshSource)
			return;

		const TransformComponent transform = entity.GetScene()->GetWorldSpaceTransform(entity);
		const glm::vec3 scale = glm::max(glm::abs(transform.Scale), glm::vec3(0.001f));
		JPH::TriangleList triangles;
		const auto& submeshes = meshSource->GetSubmeshes();
		if (collider.SubmeshIndex < submeshes.size())
			AppendSubmeshTriangles(*meshSource, submeshes[collider.SubmeshIndex], scale, triangles);
		else
		{
			for (const Submesh& submesh : submeshes)
				AppendSubmeshTriangles(*meshSource, submesh, scale, triangles);
		}

		if (triangles.empty())
			return;

		JPH::MeshShapeSettings settings(triangles);
		m_Material = collider.Material;
		m_Shape = CreateShape(settings, "mesh collider");
	}

}
