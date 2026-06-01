#include "lpch.h"
#include "PhysicsSystem.h"

#include "PhysicsScene.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/RegisterTypes.h>

namespace Lux {

	namespace {

		struct PhysicsSystemData
		{
			Scope<JPH::TempAllocatorImpl> TempAllocator;
			Scope<JPH::JobSystemThreadPool> JobSystem;
			Ref<MeshCookingFactory> CookingFactory;
			Ref<PhysicsCaptureManager> CaptureManager;
			MeshColliderCache ColliderCache;
			std::string LastErrorMessage;
			bool Initialized = false;
		};

		PhysicsSystemData s_Data;

		void JoltTraceCallback(const char* format, ...)
		{
			va_list args;
			va_start(args, format);
			char buffer[1024];
			vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);

			s_Data.LastErrorMessage = buffer;
			LUX_CORE_TRACE_TAG("Physics", "{}", buffer);
		}

#ifdef JPH_ENABLE_ASSERTS
		bool JoltAssertFailedCallback(const char* expression, const char* message, const char* file, JPH::uint line)
		{
			LUX_CORE_ERROR_TAG("Physics", "{}:{}: ({}) {}", file, line, expression, message ? message : "");
			return true;
		}
#endif

	}

	void PhysicsSystem::Init()
	{
		if (s_Data.Initialized)
			return;

		JPH::RegisterDefaultAllocator();
		JPH::Trace = JoltTraceCallback;

#ifdef JPH_ENABLE_ASSERTS
		JPH::AssertFailed = JoltAssertFailedCallback;
#endif

		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();

		s_Data.TempAllocator = CreateScope<JPH::TempAllocatorImpl>(32 * 1024 * 1024);
		s_Data.CookingFactory = Ref<MeshCookingFactory>::Create();
		s_Data.CaptureManager = Ref<PhysicsCaptureManager>::Create();
		s_Data.ColliderCache.Init();

		const uint32_t hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
		const uint32_t workerThreads = hardwareThreads > 1 ? hardwareThreads - 1 : 1;
		s_Data.JobSystem = CreateScope<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workerThreads);
		s_Data.Initialized = true;

		LUX_CORE_TRACE_TAG("Physics", "Jolt Physics initialized with {} worker threads", workerThreads);
	}

	void PhysicsSystem::Shutdown()
	{
		if (!s_Data.Initialized)
			return;

		s_Data.ColliderCache.Clear();
		s_Data.CaptureManager = nullptr;
		s_Data.CookingFactory = nullptr;
		s_Data.JobSystem.reset();
		s_Data.TempAllocator.reset();

		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;

		s_Data.Initialized = false;
		s_Data.LastErrorMessage.clear();
	}

	bool PhysicsSystem::IsInitialized()
	{
		return s_Data.Initialized;
	}

	const std::string& PhysicsSystem::GetLastErrorMessage()
	{
		return s_Data.LastErrorMessage;
	}

	Ref<PhysicsScene> PhysicsSystem::CreateScene(Scene* scene)
	{
		if (!s_Data.Initialized)
			Init();

		return Ref<PhysicsScene>::Create(scene);
	}

	Ref<MeshCookingFactory> PhysicsSystem::GetMeshCookingFactory()
	{
		return s_Data.CookingFactory;
	}

	Ref<PhysicsCaptureManager> PhysicsSystem::GetCaptureManager()
	{
		return s_Data.CaptureManager;
	}

	MeshColliderCache& PhysicsSystem::GetMeshColliderCache()
	{
		return s_Data.ColliderCache;
	}

	JPH::TempAllocator* PhysicsSystem::GetTempAllocator()
	{
		return s_Data.TempAllocator.get();
	}

	JPH::JobSystem* PhysicsSystem::GetJobSystem()
	{
		return s_Data.JobSystem.get();
	}

}
