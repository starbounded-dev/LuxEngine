// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ContactListener2D.h"

#include "Lux/Scene/Entity.h"
#include "Lux/Scene/Scene.h"
#include "Lux/Scripting/ScriptEngine.h"

namespace Lux {

	// Invokes the managed collision bridge on `entity`'s live script instance, passing `other`.
	static void InvokeCollision(UUID entity, UUID other, const char* method)
	{
		Scene* scene = ScriptEngine::GetInstance().GetCurrentScene().Raw();
		if (!scene)
			return;

		auto& storage = scene->GetScriptStorage();
		auto it = storage.EntityStorage.find(entity);
		if (it == storage.EntityStorage.end() || !it->second.Instance)
			return;

		it->second.Instance->InvokeMethod(method, (uint64_t)other);
	}

	// PhysicsScene2D::Start stores each body's entity UUID in its user data.
	static UUID GetBodyEntity(b2Fixture* fixture)
	{
		return (UUID)(uint64_t)fixture->GetBody()->GetUserData().pointer;
	}

	void ContactListener2D::BeginContact(b2Contact* contact)
	{
		if (!m_IsPlaying)
			return;

		const UUID a = GetBodyEntity(contact->GetFixtureA());
		const UUID b = GetBodyEntity(contact->GetFixtureB());

		InvokeCollision(a, b, "OnCollisionBeginInternal");
		InvokeCollision(b, a, "OnCollisionBeginInternal");
	}

	void ContactListener2D::EndContact(b2Contact* contact)
	{
		if (!m_IsPlaying)
			return;

		const UUID a = GetBodyEntity(contact->GetFixtureA());
		const UUID b = GetBodyEntity(contact->GetFixtureB());

		InvokeCollision(a, b, "OnCollisionEndInternal");
		InvokeCollision(b, a, "OnCollisionEndInternal");
	}

}
