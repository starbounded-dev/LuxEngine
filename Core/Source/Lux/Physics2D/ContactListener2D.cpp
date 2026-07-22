#include "lpch.h"
#include "ContactListener2D.h"

#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scripting/ScriptEngine.h"

namespace Lux {

	// Invokes the managed collision bridge on `entity`'s live script instance, passing `other`.
	static void InvokeCollision(Entity entity, Entity other, const char* method)
	{
		Scene* scene = ScriptEngine::GetInstance().GetCurrentScene().Raw();
		if (!scene)
			return;

		auto& storage = scene->GetScriptStorage();
		auto it = storage.EntityStorage.find(entity.GetUUID());
		if (it == storage.EntityStorage.end() || !it->second.Instance)
			return;

		it->second.Instance->InvokeMethod(method, (uint64_t)other.GetUUID());
	}

	void ContactListener2D::BeginContact(b2Contact* contact)
	{
		if (!m_IsPlaying)
			return;

		Entity& a = *(Entity*)contact->GetFixtureA()->GetBody()->GetUserData().pointer;
		Entity& b = *(Entity*)contact->GetFixtureB()->GetBody()->GetUserData().pointer;

		InvokeCollision(a, b, "OnCollisionBeginInternal");
		InvokeCollision(b, a, "OnCollisionBeginInternal");
	}

	void ContactListener2D::EndContact(b2Contact* contact)
	{
		if (!m_IsPlaying)
			return;

		Entity& a = *(Entity*)contact->GetFixtureA()->GetBody()->GetUserData().pointer;
		Entity& b = *(Entity*)contact->GetFixtureB()->GetBody()->GetUserData().pointer;

		InvokeCollision(a, b, "OnCollisionEndInternal");
		InvokeCollision(b, a, "OnCollisionEndInternal");
	}

}
