// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "PhysicsShapes.h"

#include "Lux/Physics/JoltPhysics/JoltShapes.h"

namespace Lux {

	Ref<BoxShape> BoxShape::Create(Entity entity, float totalBodyMass)
	{
		return Ref<JoltBoxShape>::Create(entity, totalBodyMass);
	}

	Ref<SphereShape> SphereShape::Create(Entity entity, float totalBodyMass)
	{
		return Ref<JoltSphereShape>::Create(entity, totalBodyMass);
	}

	Ref<CapsuleShape> CapsuleShape::Create(Entity entity, float totalBodyMass)
	{
		return Ref<JoltCapsuleShape>::Create(entity, totalBodyMass);
	}

	Ref<TriangleMeshShape> TriangleMeshShape::Create(Entity entity)
	{
		return Ref<JoltTriangleMeshShape>::Create(entity);
	}

}
