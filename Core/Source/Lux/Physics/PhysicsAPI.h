#pragma once

#include "Lux/Core/Ref.h"
#include "Lux/Physics/MeshCookingFactory.h"
#include "Lux/Physics/PhysicsCaptureManager.h"

#include <string>

namespace Lux {

	class PhysicsScene;
	class Scene;

	enum class PhysicsAPIBackend : uint8_t
	{
		Jolt = 0
	};

	class PhysicsAPI
	{
	public:
		virtual ~PhysicsAPI() = default;

		virtual void Init() = 0;
		virtual void Shutdown() = 0;
		virtual Ref<MeshCookingFactory> GetMeshCookingFactory() const = 0;
		virtual Ref<PhysicsScene> CreateScene(Scene* scene) const = 0;
		virtual Ref<PhysicsCaptureManager> GetCaptureManager() const = 0;
		virtual const std::string& GetLastErrorMessage() const = 0;

		static PhysicsAPIBackend Current() { return s_CurrentPhysicsAPI; }
		static void SetCurrentAPI(PhysicsAPIBackend api) { s_CurrentPhysicsAPI = api; }

	private:
		inline static PhysicsAPIBackend s_CurrentPhysicsAPI = PhysicsAPIBackend::Jolt;
	};

}
