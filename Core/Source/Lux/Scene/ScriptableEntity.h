#pragma once

#include "StarEngine/Scene/Entity.h"

namespace StarEngine
{
	class ScriptableEntity
	{
	public:
		virtual ~ScriptableEntity() {}

		template<typename T>
		T& GetComponent()
		{
			return m_Entity.GetComponent<T>();
		}

		// Add the Invoke method
		virtual void Invoke(const std::string& methodName)
		{
			if (methodName == "OnCollisionBegin")
			{
				OnCollisionBegin();
			}
			else if (methodName == "OnCollisionEnd")
			{
				OnCollisionEnd();
			}
		}

	protected:
		virtual void OnCreate() {}
		virtual void OnDestroy() {}
		virtual void OnUpdate(Timestep ts) {}
		virtual void OnCollisionBegin() {} // Add this method
		virtual void OnCollisionEnd() {}   // Add this method

	private:
		Entity m_Entity;
		friend class Scene;
	};

}
