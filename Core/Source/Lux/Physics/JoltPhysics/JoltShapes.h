#pragma once

#include "Lux/Physics/PhysicsShapes.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace Lux {

	class JoltShapeBase
	{
	public:
		JPH::RefConst<JPH::Shape> GetJoltShape() const { return m_Shape; }

	protected:
		JPH::RefConst<JPH::Shape> m_Shape;
	};

	class JoltBoxShape final : public BoxShape, public JoltShapeBase
	{
	public:
		JoltBoxShape(Entity entity, float totalBodyMass);
		virtual uint32_t GetNumShapes() const override { return m_Shape ? 1 : 0; }
		virtual void* GetNativeShape(uint32_t = 0) const override { return (void*)m_Shape.GetPtr(); }
		virtual void SetMaterial(const ColliderMaterial& material) override { m_Material = material; }
		virtual void Destroy() override { m_Shape = nullptr; }
		virtual glm::vec3 GetHalfSize() const override { return m_HalfSize; }

	private:
		glm::vec3 m_HalfSize = glm::vec3(0.5f);
		ColliderMaterial m_Material;
	};

	class JoltSphereShape final : public SphereShape, public JoltShapeBase
	{
	public:
		JoltSphereShape(Entity entity, float totalBodyMass);
		virtual uint32_t GetNumShapes() const override { return m_Shape ? 1 : 0; }
		virtual void* GetNativeShape(uint32_t = 0) const override { return (void*)m_Shape.GetPtr(); }
		virtual void SetMaterial(const ColliderMaterial& material) override { m_Material = material; }
		virtual void Destroy() override { m_Shape = nullptr; }
		virtual float GetRadius() const override { return m_Radius; }

	private:
		float m_Radius = 0.5f;
		ColliderMaterial m_Material;
	};

	class JoltCapsuleShape final : public CapsuleShape, public JoltShapeBase
	{
	public:
		JoltCapsuleShape(Entity entity, float totalBodyMass);
		virtual uint32_t GetNumShapes() const override { return m_Shape ? 1 : 0; }
		virtual void* GetNativeShape(uint32_t = 0) const override { return (void*)m_Shape.GetPtr(); }
		virtual void SetMaterial(const ColliderMaterial& material) override { m_Material = material; }
		virtual void Destroy() override { m_Shape = nullptr; }
		virtual float GetRadius() const override { return m_Radius; }
		virtual float GetHeight() const override { return m_HalfHeight * 2.0f; }

	private:
		float m_Radius = 0.5f;
		float m_HalfHeight = 0.5f;
		ColliderMaterial m_Material;
	};

	class JoltTriangleMeshShape final : public TriangleMeshShape, public JoltShapeBase
	{
	public:
		explicit JoltTriangleMeshShape(Entity entity);
		virtual uint32_t GetNumShapes() const override { return m_Shape ? 1 : 0; }
		virtual void* GetNativeShape(uint32_t = 0) const override { return (void*)m_Shape.GetPtr(); }
		virtual void SetMaterial(const ColliderMaterial& material) override { m_Material = material; }
		virtual void Destroy() override { m_Shape = nullptr; }

	private:
		ColliderMaterial m_Material;
	};

}
