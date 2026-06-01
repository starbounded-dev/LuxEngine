#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Vec3.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Lux::JoltUtils {

	inline JPH::Vec3 ToJoltVector(const glm::vec3& value)
	{
		return { value.x, value.y, value.z };
	}

	inline JPH::RVec3 ToJoltRVec3(const glm::vec3& value)
	{
		return { value.x, value.y, value.z };
	}

	inline JPH::Quat ToJoltQuat(const glm::quat& value)
	{
		return { value.x, value.y, value.z, value.w };
	}

	inline glm::vec3 FromJoltVector(const JPH::Vec3& value)
	{
		return { value.GetX(), value.GetY(), value.GetZ() };
	}

	inline glm::vec3 FromJoltRVector(const JPH::RVec3& value)
	{
		return { (float)value.GetX(), (float)value.GetY(), (float)value.GetZ() };
	}

	inline glm::quat FromJoltQuat(const JPH::Quat& value)
	{
		return { value.GetW(), value.GetX(), value.GetY(), value.GetZ() };
	}

}
