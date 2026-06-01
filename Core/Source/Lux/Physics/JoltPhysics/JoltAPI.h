#pragma once

#include "Lux/Physics/PhysicsAPI.h"

namespace Lux {

	class JoltAPI final : public PhysicsAPI
	{
	public:
		virtual ~JoltAPI() = default;

		virtual void Init() override;
		virtual void Shutdown() override;
		virtual Ref<MeshCookingFactory> GetMeshCookingFactory() const override { return m_CookingFactory; }
		virtual Ref<PhysicsScene> CreateScene(Scene* scene) const override;
		virtual Ref<PhysicsCaptureManager> GetCaptureManager() const override { return m_CaptureManager; }
		virtual const std::string& GetLastErrorMessage() const override;

	private:
		Ref<MeshCookingFactory> m_CookingFactory;
		Ref<PhysicsCaptureManager> m_CaptureManager;
	};

}
