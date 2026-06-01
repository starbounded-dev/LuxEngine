#pragma once

#include "Lux/Core/UUID.h"
#include "Lux/Physics/PhysicsShapes.h"
#include "Lux/Physics/PhysicsTypes.h"

#include <glm/glm.hpp>

#include <unordered_set>

namespace Lux {

	struct SceneQueryHit
	{
		UUID HitEntity = 0;
		glm::vec3 Position = glm::vec3(std::numeric_limits<float>::max());
		glm::vec3 Normal = glm::vec3(std::numeric_limits<float>::max());
		float Distance = std::numeric_limits<float>::max();
		Ref<PhysicsShape> HitCollider = nullptr;

		void Clear()
		{
			HitEntity = 0;
			Position = glm::vec3(std::numeric_limits<float>::max());
			Normal = glm::vec3(std::numeric_limits<float>::max());
			Distance = std::numeric_limits<float>::max();
			HitCollider = nullptr;
		}
	};

	using ExcludedEntityMap = std::unordered_set<UUID>;

	struct RayCastInfo
	{
		glm::vec3 Origin = glm::vec3(0.0f);
		glm::vec3 Direction = glm::vec3(0.0f, -1.0f, 0.0f);
		float MaxDistance = 1000.0f;
		ExcludedEntityMap ExcludedEntities;
	};

	enum class ShapeCastType { Box, Sphere, Capsule };

	struct ShapeCastInfo
	{
		explicit ShapeCastInfo(ShapeCastType type)
			: Type(type) {}

		glm::vec3 Origin = glm::vec3(0.0f);
		glm::vec3 Direction = glm::vec3(0.0f, -1.0f, 0.0f);
		float MaxDistance = 1000.0f;
		ExcludedEntityMap ExcludedEntities;

		ShapeCastType GetCastType() const { return Type; }

	private:
		ShapeCastType Type;
	};

	struct BoxCastInfo : public ShapeCastInfo
	{
		BoxCastInfo() : ShapeCastInfo(ShapeCastType::Box) {}
		glm::vec3 HalfExtent = glm::vec3(0.5f);
	};

	struct SphereCastInfo : public ShapeCastInfo
	{
		SphereCastInfo() : ShapeCastInfo(ShapeCastType::Sphere) {}
		float Radius = 0.5f;
	};

	struct CapsuleCastInfo : public ShapeCastInfo
	{
		CapsuleCastInfo() : ShapeCastInfo(ShapeCastType::Capsule) {}
		float HalfHeight = 0.5f;
		float Radius = 0.5f;
	};

	struct ShapeOverlapInfo
	{
		explicit ShapeOverlapInfo(ShapeCastType type)
			: Type(type) {}

		glm::vec3 Origin = glm::vec3(0.0f);
		ExcludedEntityMap ExcludedEntities;

		ShapeCastType GetCastType() const { return Type; }

	private:
		ShapeCastType Type;
	};

	struct BoxOverlapInfo : public ShapeOverlapInfo
	{
		BoxOverlapInfo() : ShapeOverlapInfo(ShapeCastType::Box) {}
		glm::vec3 HalfExtent = glm::vec3(0.5f);
	};

	struct SphereOverlapInfo : public ShapeOverlapInfo
	{
		SphereOverlapInfo() : ShapeOverlapInfo(ShapeCastType::Sphere) {}
		float Radius = 0.5f;
	};

	struct CapsuleOverlapInfo : public ShapeOverlapInfo
	{
		CapsuleOverlapInfo() : ShapeOverlapInfo(ShapeCastType::Capsule) {}
		float HalfHeight = 0.5f;
		float Radius = 0.5f;
	};

}
