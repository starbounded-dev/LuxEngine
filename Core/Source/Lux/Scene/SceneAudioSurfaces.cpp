#include "lpch.h"
#include "Scene.h"

#include "Lux/Physics/PhysicsScene.h"
#include "Lux/Physics/SceneQueries.h"
#include <cmath>

namespace Lux
{
	namespace
	{
		constexpr float k_MinGroundNormalY = 0.5f;
		constexpr float k_TeleportStrideCount = 4.0f;
	}

	AudioSurfaceSounds Scene::GetSurfaceSounds(Entity entity, AcousticMaterial& material) const
	{
		material = AcousticMaterial::Default;
		const auto* surface = entity.TryGetComponent<AudioSurfaceComponent>();
		if (surface)
			material = surface->Material;
		else if (const auto* mesh = entity.TryGetComponent<MeshColliderComponent>())
			material = mesh->Acoustic;
		if (!IsValidAcousticMaterial(material))
			material = AcousticMaterial::Default;
		AudioSurfaceSounds sounds;
		if (m_AudioSurfaceTable)
		{
			sounds = m_AudioSurfaceTable->Surfaces[static_cast<size_t>(material)];
			const auto& fallback = m_AudioSurfaceTable->Surfaces[0];
			if (!sounds.Footstep.IsValid())
				sounds.Footstep = fallback.Footstep;
			if (!sounds.Impact.IsValid())
				sounds.Impact = fallback.Impact;
			if (!sounds.Scrape.IsValid())
				sounds.Scrape = fallback.Scrape;
			if (!sounds.Roll.IsValid())
				sounds.Roll = fallback.Roll;
		}
		if (surface)
		{
			if (surface->FootstepOverride.IsValid())
				sounds.Footstep = surface->FootstepOverride;
			if (surface->ImpactOverride.IsValid())
				sounds.Impact = surface->ImpactOverride;
		}
		return sounds;
	}

	bool Scene::PlayFootstep(UUID id, float speed, float weight, float probeDistance)
	{
		if (!m_IsRunning || m_IsPaused || !m_PhysicsScene)
			return false;
		if (!std::isfinite(speed) || speed < 0.0f || !std::isfinite(weight) || weight <= 0.0f
			|| !std::isfinite(probeDistance) || probeDistance <= 0.0f)
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid footstep speed, weight or ground probe distance for entity {}", static_cast<uint64_t>(id));
			return false;
		}
		Entity entity = TryGetEntityWithUUID(id);
		if (!entity)
			return false;
		RayCastInfo ray;
		ray.Origin = GetWorldSpaceTransform(entity).Translation;
		ray.MaxDistance = probeDistance;
		ray.ExcludedEntities.insert(id);
		SceneQueryHit hit;
		if (!m_PhysicsScene->CastRay(&ray, hit) || hit.Normal.y < k_MinGroundNormalY)
			return false;
		Entity ground = TryGetEntityWithUUID(hit.HitEntity);
		if (!ground)
			return false;
		AcousticMaterial material;
		const auto sounds = GetSurfaceSounds(ground, material);
		return m_PhysicsAudio.Footstep(id, sounds.Footstep, material, hit.Position, speed, weight);
	}

	void Scene::UpdatePhysicsAudio(float timestep)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		static const AudioSurfaceTable defaults;
		const auto& settings = m_AudioSurfaceTable ? *m_AudioSurfaceTable : defaults;
		if (m_PhysicsScene)
			m_PhysicsScene->DrainContactEvents(m_PhysicsContactEvents);
		else
			m_PhysicsContactEvents.clear();
		for (const auto& event : m_PhysicsContactEvents)
		{
			PhysicsAudioContact contact;
			contact.Key = event.Key;
			contact.End = event.State == PhysicsContactEvent::Type::End;
			if (!contact.End)
			{
				// A dynamic body owns cooldowns; the opposing collider supplies its surface sound.
				const bool first = event.Mass1 > 0.0f;
				Entity owner = TryGetEntityWithUUID(first ? event.Entity1 : event.Entity2);
				Entity other = TryGetEntityWithUUID(first ? event.Entity2 : event.Entity1);
				const auto* surface = owner ? owner.TryGetComponent<AudioSurfaceComponent>() : nullptr;
				if (!owner || !other || (event.Mass1 <= 0.0f && event.Mass2 <= 0.0f) || (surface && !surface->PhysicsSounds))
					contact.End = true;
				else
				{
					contact.Owner = owner.GetUUID();
					contact.Other = other.GetUUID();
					contact.Sounds = GetSurfaceSounds(other, contact.Material);
					if (surface && surface->ImpactOverride.IsValid())
						contact.Sounds.Impact = surface->ImpactOverride;
					contact.Position = event.Position;
					contact.Impulse = event.EstimatedImpulse;
					contact.SlipSpeed = event.SlipSpeed;
					contact.RollSpeed = event.RollSpeed;
				}
			}
			m_PhysicsAudio.Contact(contact, settings);
		}
		if (!m_IsPaused && std::isfinite(timestep) && timestep > 0.0f)
		{
			for (auto handle : m_Registry.view<TransformComponent, AudioSurfaceComponent>())
			{
				Entity entity{ handle, this };
				const auto& surface = entity.GetComponent<AudioSurfaceComponent>();
				if (!surface.AutoFootsteps)
				{
					m_Footsteps.erase(entity.GetUUID());
					continue;
				}
				const auto position = GetWorldSpaceTransform(entity).Translation;
				auto& state = m_Footsteps[entity.GetUUID()];
				const auto delta = position - state.Position;
				const float distance = glm::length(glm::vec2(delta.x, delta.z));
				const bool validStride = std::isfinite(surface.StrideLength) && surface.StrideLength > 0.0f;
				// Reset on teleports and first observation; never burst a backlog of footsteps.
				if (state.Initialized && validStride && distance <= surface.StrideLength * k_TeleportStrideCount)
				{
					state.Distance += distance;
					if (state.Distance >= surface.StrideLength)
					{
						PlayFootstep(entity.GetUUID(), distance / timestep, surface.FootstepWeight, surface.GroundProbeDistance);
						state.Distance = std::fmod(state.Distance, surface.StrideLength);
					}
				}
				else
					state.Distance = 0.0f;
				state.Position = position;
				state.Initialized = true;
			}
		}
		m_PhysicsAudio.Update(timestep, m_IsPaused, settings);
	}
}
