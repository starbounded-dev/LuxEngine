// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "AudioEventInstance.h"
#include "AudioSurfaceTable.h"
#include "Lux/Core/UUID.h"
#include <map>
#include <unordered_map>

namespace Lux
{
	// Scene resolves physics IDs and surface tags before handing data to Audio.
	struct PhysicsAudioContact
	{
		std::array<uint32_t, 4> Key{};
		UUID Owner = 0, Other = 0;
		bool End = false;
		glm::vec3 Position{ 0.0f };
		float Impulse = 0.0f, SlipSpeed = 0.0f, RollSpeed = 0.0f;
		AcousticMaterial Material = AcousticMaterial::Default;
		AudioSurfaceSounds Sounds;
	};

	class PhysicsAudioSystem
	{
	public:
		void Contact(const PhysicsAudioContact& contact, const AudioSurfaceTable& settings);
		bool Footstep(UUID owner, const AudioEventRef& event, AcousticMaterial material,
			const glm::vec3& position, float speed, float weight);
		void Update(float timestep, bool paused, const AudioSurfaceTable& settings);
		void Remove(UUID entity);
		void Clear();
		size_t GetContactCount() const { return m_Contacts.size(); }
	private:
		struct Voice
		{
			UUID Owner = 0, Other = 0;
			std::string Guid;
			Ref<AudioEventInstance> Instance;
		};
		struct Motion
		{
			Voice Scrape, Roll;
			bool Seen = false;
		};
		Ref<AudioEventInstance> Create(const AudioEventRef& event, bool oneShot);
		void Retire(Voice& voice);
		void UpdateMotion(Voice& voice, const AudioEventRef& event, const PhysicsAudioContact& contact, float speed, float threshold);
		std::map<std::array<uint32_t, 4>, PhysicsAudioContact> m_Contacts;
		std::map<std::array<uint32_t, 2>, Motion> m_Motion;
		struct Cooldown
		{
			float Remaining = 0.0f;
			bool Fresh = true;
		};
		std::unordered_map<UUID, Cooldown> m_Cooldowns;
		std::map<std::pair<std::string, bool>, uint64_t> m_FailedEvents;
		std::vector<Voice> m_Transient;
	};
}
