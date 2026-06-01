#pragma once

#include "Lux/Core/Base.h"

#include <glm/glm.hpp>

#include <limits>
#include <type_traits>

namespace Lux {

	enum class ECookingResult : uint8_t
	{
		None = 0,
		Success,
		Failed
	};

	enum class EForceMode : uint8_t
	{
		Force = 0,
		Impulse,
		VelocityChange,
		Acceleration
	};

	enum class EBodyType : uint8_t
	{
		Static = 0,
		Dynamic,
		Kinematic
	};

	enum class ECollisionDetectionType : uint32_t
	{
		Discrete = 0,
		Continuous
	};

	enum class ECollisionComplexity : uint8_t
	{
		Default = 0,
		UseSimpleAsComplex,
		UseComplexAsSimple
	};

	enum class MeshColliderType : uint8_t
	{
		Triangle = 0,
		Convex,
		None
	};

	enum class ECollisionFlags : uint8_t
	{
		None = 0,
		Sides = BIT(0),
		Above = BIT(1),
		Below = BIT(2)
	};

	enum class EFalloffMode : uint8_t
	{
		Constant = 0,
		Linear
	};

	enum class EActorAxis : uint32_t
	{
		None = 0,
		TranslationX = BIT(0),
		TranslationY = BIT(1),
		TranslationZ = BIT(2),
		Translation = BIT(0) | BIT(1) | BIT(2),
		RotationX = BIT(3),
		RotationY = BIT(4),
		RotationZ = BIT(5),
		Rotation = BIT(3) | BIT(4) | BIT(5)
	};

	inline constexpr EActorAxis operator|(EActorAxis lhs, EActorAxis rhs)
	{
		using Underlying = std::underlying_type_t<EActorAxis>;
		return static_cast<EActorAxis>(static_cast<Underlying>(lhs) | static_cast<Underlying>(rhs));
	}

	inline constexpr EActorAxis operator&(EActorAxis lhs, EActorAxis rhs)
	{
		using Underlying = std::underlying_type_t<EActorAxis>;
		return static_cast<EActorAxis>(static_cast<Underlying>(lhs) & static_cast<Underlying>(rhs));
	}

	inline EActorAxis& operator|=(EActorAxis& lhs, EActorAxis rhs)
	{
		lhs = lhs | rhs;
		return lhs;
	}

	inline EActorAxis& operator&=(EActorAxis& lhs, EActorAxis rhs)
	{
		lhs = lhs & rhs;
		return lhs;
	}

	inline constexpr ECollisionFlags operator|(ECollisionFlags lhs, ECollisionFlags rhs)
	{
		using Underlying = std::underlying_type_t<ECollisionFlags>;
		return static_cast<ECollisionFlags>(static_cast<Underlying>(lhs) | static_cast<Underlying>(rhs));
	}

	inline constexpr ECollisionFlags operator&(ECollisionFlags lhs, ECollisionFlags rhs)
	{
		using Underlying = std::underlying_type_t<ECollisionFlags>;
		return static_cast<ECollisionFlags>(static_cast<Underlying>(lhs) & static_cast<Underlying>(rhs));
	}

	struct ColliderMaterial
	{
		float Density = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;
	};

}
