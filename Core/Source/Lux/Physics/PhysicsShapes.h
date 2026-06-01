#pragma once

#include "Lux/Core/Ref.h"
#include "Lux/Physics/PhysicsTypes.h"
#include "Lux/Scene/Entity.h"

#include <glm/glm.hpp>

namespace Lux {

	enum class ShapeType
	{
		Box,
		Sphere,
		Capsule,
		ConvexMesh,
		TriangleMesh,
		CompoundShape,
		MutableCompoundShape,
		Last
	};

	class PhysicsShape : public RefCounted
	{
	public:
		virtual ~PhysicsShape() = default;

		virtual uint32_t GetNumShapes() const = 0;
		virtual void* GetNativeShape(uint32_t index = 0) const = 0;
		virtual void SetMaterial(const ColliderMaterial& material) = 0;
		virtual void Destroy() = 0;

		Entity GetEntity() const { return m_Entity; }
		ShapeType GetType() const { return m_Type; }

	protected:
		PhysicsShape(ShapeType type, Entity entity)
			: m_Entity(entity), m_Type(type) {}

	private:
		Entity m_Entity;
		ShapeType m_Type;
	};

	class CompoundShape : public PhysicsShape
	{
	public:
		CompoundShape(Entity entity, bool immutable)
			: PhysicsShape(immutable ? ShapeType::CompoundShape : ShapeType::MutableCompoundShape, entity) {}

		virtual void AddShape(const Ref<PhysicsShape>& shape) = 0;
		virtual void RemoveShape(const Ref<PhysicsShape>& shape) = 0;
		virtual void Create() = 0;
	};

	class BoxShape : public PhysicsShape
	{
	public:
		explicit BoxShape(Entity entity) : PhysicsShape(ShapeType::Box, entity) {}
		virtual glm::vec3 GetHalfSize() const = 0;
		static Ref<BoxShape> Create(Entity entity, float totalBodyMass);
	};

	class SphereShape : public PhysicsShape
	{
	public:
		explicit SphereShape(Entity entity) : PhysicsShape(ShapeType::Sphere, entity) {}
		virtual float GetRadius() const = 0;
		static Ref<SphereShape> Create(Entity entity, float totalBodyMass);
	};

	class CapsuleShape : public PhysicsShape
	{
	public:
		explicit CapsuleShape(Entity entity) : PhysicsShape(ShapeType::Capsule, entity) {}
		virtual float GetRadius() const = 0;
		virtual float GetHeight() const = 0;
		static Ref<CapsuleShape> Create(Entity entity, float totalBodyMass);
	};

	class TriangleMeshShape : public PhysicsShape
	{
	public:
		explicit TriangleMeshShape(Entity entity) : PhysicsShape(ShapeType::TriangleMesh, entity) {}
		static Ref<TriangleMeshShape> Create(Entity entity);
	};

}
