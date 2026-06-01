#include "lpch.h"
#include "JoltAPI.h"

#include "Lux/Physics/PhysicsScene.h"
#include "Lux/Physics/PhysicsSystem.h"

namespace Lux {

	void JoltAPI::Init()
	{
		PhysicsSystem::Init();
		m_CookingFactory = PhysicsSystem::GetMeshCookingFactory();
		m_CaptureManager = PhysicsSystem::GetCaptureManager();
	}

	void JoltAPI::Shutdown()
	{
		m_CaptureManager = nullptr;
		m_CookingFactory = nullptr;
		PhysicsSystem::Shutdown();
	}

	Ref<PhysicsScene> JoltAPI::CreateScene(Scene* scene) const
	{
		return Ref<PhysicsScene>::Create(scene);
	}

	const std::string& JoltAPI::GetLastErrorMessage() const
	{
		return PhysicsSystem::GetLastErrorMessage();
	}

}
