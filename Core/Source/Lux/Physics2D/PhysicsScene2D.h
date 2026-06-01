#pragma once

#include "Lux/Core/Timestep.h"

class b2World;

namespace Lux {

	class Scene;

	class PhysicsScene2D
	{
	public:
		explicit PhysicsScene2D(Scene* scene);
		~PhysicsScene2D();

		void Start();
		void Stop();
		void Simulate(Timestep timestep);

		static void SetPlaying(bool playing);

	private:
		Scene* m_Scene = nullptr;
		b2World* m_PhysicsWorld = nullptr;
	};

}
