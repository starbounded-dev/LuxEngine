#pragma once

#include "Lux/Core/Base.h"
#include "Lux/Physics/MeshColliderCache.h"
#include "Lux/Physics/MeshCookingFactory.h"
#include "Lux/Physics/PhysicsCaptureManager.h"

#include <string>

namespace JPH {
	class JobSystem;
	class JobSystemThreadPool;
	class TempAllocator;
}

namespace Lux {

	class PhysicsScene;
	class Scene;

	class PhysicsSystem
	{
	public:
		static void Init();
		static void Shutdown();

		static bool IsInitialized();
		static const std::string& GetLastErrorMessage();

		static Ref<PhysicsScene> CreateScene(Scene* scene);
		static Ref<MeshCookingFactory> GetMeshCookingFactory();
		static Ref<PhysicsCaptureManager> GetCaptureManager();
		static MeshColliderCache& GetMeshColliderCache();

		static JPH::TempAllocator* GetTempAllocator();
		static JPH::JobSystem* GetJobSystem();
	};

}
