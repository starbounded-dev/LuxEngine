// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "PhysicsAudioSystem.h"
#include "AudioEngine.h"

#include <cmath>

namespace Lux
{
	namespace
	{
		void Place(Ref<AudioEventInstance>& instance, const glm::vec3& position)
		{
			instance->Set3DAttributes(position, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		}
	}

	Ref<AudioEventInstance> PhysicsAudioSystem::Create(const AudioEventRef& event, bool oneShot)
	{
		if (!event.IsValid())
			return nullptr;
		const auto revision = AudioEngine::GetBankRevision();
		const auto key = std::make_pair(event.Guid, oneShot);
		const auto failed = m_FailedEvents.find(key);
		if (failed != m_FailedEvents.end() && failed->second == revision)
			return nullptr;
		auto instance = AudioEventInstance::Create(event.Guid);
		if (!instance || instance->IsSnapshot() || instance->IsOneShot() != oneShot)
		{
			if (instance)
				LUX_CORE_ERROR_TAG("Audio", "Surface event '{}' must be a {} event, not a snapshot", event.Guid, oneShot ? "one-shot" : "continuous");
			m_FailedEvents[key] = revision;
			return nullptr;
		}
		m_FailedEvents.erase(key);
		return instance;
	}

	void PhysicsAudioSystem::Contact(const PhysicsAudioContact& contact, const AudioSurfaceTable& settings)
	{
		if (contact.End)
		{
			m_Contacts.erase(contact.Key);
			return;
		}
		if (!contact.Owner || !IsValidAcousticMaterial(contact.Material))
			return;
		const bool began = !m_Contacts.contains(contact.Key);
		m_Contacts[contact.Key] = contact;
		if (!began || !std::isfinite(contact.Impulse) || contact.Impulse < settings.MinimumImpulse || contact.Impulse <= 0.0f)
			return;
		if (m_Cooldowns.contains(contact.Owner))
			return;
		if (auto instance = Create(contact.Sounds.Impact, true))
		{
			Place(instance, contact.Position);
			instance->SetParameter("Impulse", contact.Impulse);
			instance->SetParameter("Surface", static_cast<float>(contact.Material));
			instance->Start();
			m_Transient.push_back({ contact.Owner, contact.Other, contact.Sounds.Impact.Guid, instance });
			m_Cooldowns[contact.Owner] = { settings.ImpactCooldown, true };
		}
	}

	bool PhysicsAudioSystem::Footstep(UUID owner, const AudioEventRef& event, AcousticMaterial material,
		const glm::vec3& position, float speed, float weight)
	{
		if (!std::isfinite(speed) || speed < 0.0f || !std::isfinite(weight) || weight <= 0.0f || !IsValidAcousticMaterial(material))
			return false;
		auto instance = Create(event, true);
		if (!instance)
			return false;
		Place(instance, position);
		instance->SetParameter("Speed", speed);
		instance->SetParameter("Weight", weight);
		instance->SetParameter("Surface", static_cast<float>(material));
		instance->Start();
		m_Transient.push_back({ owner, 0, event.Guid, instance });
		return true;
	}

	void PhysicsAudioSystem::Retire(Voice& voice)
	{
		if (voice.Instance && voice.Instance->IsValid())
		{
			voice.Instance->Stop(true);
			m_Transient.push_back(std::move(voice));
		}
		voice = {};
	}

	void PhysicsAudioSystem::UpdateMotion(Voice& voice, const AudioEventRef& event, const PhysicsAudioContact& contact, float speed, float threshold)
	{
		if (!event.IsValid() || !std::isfinite(speed) || speed < threshold || speed <= 0.0f)
		{
			Retire(voice);
			return;
		}
		if (voice.Guid != event.Guid || (voice.Instance && !voice.Instance->IsValid()))
			Retire(voice);
		bool start = false;
		if (!voice.Instance)
		{
			voice.Instance = Create(event, false);
			if (!voice.Instance)
				return;
			voice.Guid = event.Guid;
			voice.Owner = contact.Owner;
			voice.Other = contact.Other;
			start = true;
		}
		Place(voice.Instance, contact.Position);
		voice.Instance->SetParameter("Speed", speed);
		voice.Instance->SetParameter("Surface", static_cast<float>(contact.Material));
		if (start)
			voice.Instance->Start();
	}

	void PhysicsAudioSystem::Update(float timestep, bool paused, const AudioSurfaceTable& settings)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		const float dt = !paused && std::isfinite(timestep) ? std::max(0.0f, timestep) : 0.0f;
		for (auto it = m_Cooldowns.begin(); it != m_Cooldowns.end();)
		{
			if (!it->second.Fresh)
				it->second.Remaining -= dt;
			it->second.Fresh = false;
			if (it->second.Remaining <= 0.0f)
				it = m_Cooldowns.erase(it);
			else
				++it;
		}
		if (!paused)
		{
			for (auto& [pair, motion] : m_Motion)
				motion.Seen = false;
			// Compound manifolds share one scrape and roll voice per body pair.
			for (auto it = m_Contacts.begin(); it != m_Contacts.end();)
			{
				const std::array<uint32_t, 2> pair{ it->first[0], it->first[1] };
				const PhysicsAudioContact* scrape = &it->second;
				const PhysicsAudioContact* roll = &it->second;
				do
				{
					if (it->second.SlipSpeed > scrape->SlipSpeed)
						scrape = &it->second;
					if (it->second.RollSpeed > roll->RollSpeed)
						roll = &it->second;
					++it;
				} while (it != m_Contacts.end() && it->first[0] == pair[0] && it->first[1] == pair[1]);
				auto& motion = m_Motion[pair];
				motion.Seen = true;
				UpdateMotion(motion.Scrape, scrape->Sounds.Scrape, *scrape, scrape->SlipSpeed, settings.MinimumMotionSpeed);
				UpdateMotion(motion.Roll, roll->Sounds.Roll, *roll, roll->RollSpeed, settings.MinimumMotionSpeed);
			}
			for (auto it = m_Motion.begin(); it != m_Motion.end();)
			{
				if (!it->second.Seen)
				{
					Retire(it->second.Scrape);
					Retire(it->second.Roll);
					it = m_Motion.erase(it);
				}
				else
					++it;
			}
		}
		for (auto& [pair, motion] : m_Motion)
		{
			if (motion.Scrape.Instance)
				motion.Scrape.Instance->SetScenePaused(paused);
			if (motion.Roll.Instance)
				motion.Roll.Instance->SetScenePaused(paused);
		}
		for (auto& voice : m_Transient)
			voice.Instance->SetScenePaused(paused);
		std::erase_if(m_Transient, [](const Voice& voice) { return !voice.Instance->IsValid() || !voice.Instance->IsPlaying(); });
	}

	void PhysicsAudioSystem::Remove(UUID entity)
	{
		std::erase_if(m_Contacts, [entity](const auto& entry) { return entry.second.Owner == entity || entry.second.Other == entity; });
		std::erase_if(m_Motion, [entity](const auto& entry)
		{
			const auto& motion = entry.second;
			return motion.Scrape.Owner == entity || motion.Scrape.Other == entity || motion.Roll.Owner == entity || motion.Roll.Other == entity;
		});
		std::erase_if(m_Transient, [entity](const Voice& voice) { return voice.Owner == entity || voice.Other == entity; });
		m_Cooldowns.erase(entity);
	}

	void PhysicsAudioSystem::Clear()
	{
		m_Contacts.clear();
		m_Motion.clear();
		m_Transient.clear();
		m_Cooldowns.clear();
		m_FailedEvents.clear();
	}
}
