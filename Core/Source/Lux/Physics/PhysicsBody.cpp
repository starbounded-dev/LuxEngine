#include "lpch.h"
#include "PhysicsBody.h"

#include "Lux/Scene/Components.h"

namespace Lux {

	PhysicsBody::PhysicsBody(Entity entity)
		: m_Entity(entity)
	{
		if (entity && entity.HasComponent<RigidBodyComponent>())
			m_LockedAxes = entity.GetComponent<RigidBodyComponent>().LockedAxes;
	}

	uint32_t PhysicsBody::GetShapeCount(ShapeType type) const
	{
		auto it = m_Shapes.find(type);
		return it == m_Shapes.end() ? 0 : (uint32_t)it->second.size();
	}

	const ShapeArray& PhysicsBody::GetShapes(ShapeType type) const
	{
		auto it = m_Shapes.find(type);
		return it == m_Shapes.end() ? s_EmptyShapes : it->second;
	}

	Ref<PhysicsShape> PhysicsBody::GetShape(ShapeType type, uint32_t index) const
	{
		auto it = m_Shapes.find(type);
		if (it == m_Shapes.end() || index >= it->second.size())
			return nullptr;
		return it->second[index];
	}

	void PhysicsBody::SetAxisLock(EActorAxis axis, bool locked, bool forceWake)
	{
		if (locked)
			m_LockedAxes |= axis;
		else
			m_LockedAxes = static_cast<EActorAxis>((uint32_t)m_LockedAxes & ~(uint32_t)axis);

		OnAxisLockUpdated(forceWake);
	}

	bool PhysicsBody::IsAxisLocked(EActorAxis axis) const
	{
		return (m_LockedAxes & axis) != EActorAxis::None;
	}

	bool PhysicsBody::IsAllRotationLocked() const
	{
		return IsAxisLocked(EActorAxis::RotationX) && IsAxisLocked(EActorAxis::RotationY) && IsAxisLocked(EActorAxis::RotationZ);
	}

	void PhysicsBody::CreateCollisionShapesForEntity(Entity entity, bool)
	{
		if (!entity)
			return;

		const float mass = entity.HasComponent<RigidBodyComponent>() ? entity.GetComponent<RigidBodyComponent>().Mass : 0.0f;
		if (entity.HasComponent<BoxColliderComponent>())
			m_Shapes[ShapeType::Box].push_back(BoxShape::Create(entity, mass));
		if (entity.HasComponent<SphereColliderComponent>())
			m_Shapes[ShapeType::Sphere].push_back(SphereShape::Create(entity, mass));
		if (entity.HasComponent<CapsuleColliderComponent>())
			m_Shapes[ShapeType::Capsule].push_back(CapsuleShape::Create(entity, mass));
		if (entity.HasComponent<MeshColliderComponent>())
			m_Shapes[ShapeType::TriangleMesh].push_back(TriangleMeshShape::Create(entity));
	}

}
