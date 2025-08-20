#include "sepch.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "ContactListener2D.h"

#include "StarEngine/Scene/Entity.h"
#include "StarEngine/Scripting/ScriptEngine.h"

namespace StarEngine {

	void ContactListener2D::BeginContact(b2Contact* contact)
	{
		if (m_IsPlaying)
		{
			Entity& a = *(Entity*)contact->GetFixtureA()->GetBody()->GetUserData().pointer;
			Entity& b = *(Entity*)contact->GetFixtureB()->GetBody()->GetUserData().pointer;

			if (a.HasComponent<ScriptComponent>() && a.HasComponent<RigidBody2DComponent>())
			{
				auto& scriptComponent = a.GetComponent<ScriptComponent>();
				if (scriptComponent.Instance.IsValid()) // Ensure the instance is valid
				{
					scriptComponent.Instance.Invoke("OnCollisionBegin");
				}
			}

			if (b.HasComponent<ScriptComponent>() && b.HasComponent<RigidBody2DComponent>())
			{
				auto& scriptComponent = b.GetComponent<ScriptComponent>();
				if (scriptComponent.Instance.IsValid()) // Ensure the shared_ptr is not null
				{
					scriptComponent.Instance.Invoke("OnCollisionBegin");
				}
			}

		}
	}

	/// Called when two fixtures cease to touch.
	void ContactListener2D::EndContact(b2Contact* contact)
	{
		if (m_IsPlaying)
		{
			Entity& a = *(Entity*)contact->GetFixtureA()->GetBody()->GetUserData().pointer;
			Entity& b = *(Entity*)contact->GetFixtureB()->GetBody()->GetUserData().pointer;

			if (a.HasComponent<ScriptComponent>() && a.HasComponent<RigidBody2DComponent>())
			{
				auto& scriptComponent = a.GetComponent<ScriptComponent>();
				if (scriptComponent.Instance.IsValid()) // Ensure the instance is valid
				{
					scriptComponent.Instance.Invoke("OnCollisionEnd");
				}
			}

			if (b.HasComponent<ScriptComponent>() && b.HasComponent<RigidBody2DComponent>())
			{
				auto& scriptComponent = b.GetComponent<ScriptComponent>();
				if (scriptComponent.Instance.IsValid()) // Ensure the instance is valid
				{
					scriptComponent.Instance.Invoke("OnCollisionEnd");

				}
			}
		}
	}

}
