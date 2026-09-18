#pragma once

#include "AudioEventInstance.h"
#include "AudioListener.h"
#include "AudioZoneSettings.h"

#include <span>
#include <unordered_map>
#include <unordered_set>

namespace Lux {

	struct AudioZoneComponent;
	struct AudioPortalComponent;

	// Resolved on the scene thread. Collider zones support one box, sphere or capsule collider.
	struct AudioZoneVolume
	{
		enum class Shape { Box, Sphere, Capsule };
		Shape Type = Shape::Box;
		glm::mat4 Transform{ 1.0f };
		glm::vec3 HalfExtents{ 1.0f };
		float Radius = 1.0f;
		float HalfHeight = 0.0f;
		bool Valid = true;
	};

	struct AudioZoneInput
	{
		UUID ID = 0;
		const AudioZoneComponent* Component = nullptr;
		AudioZoneVolume Volume;
		float Target = 0.0f;
		float ListenerTarget = 0.0f, PortalDelta = 0.0f, PortalOutgoing = 0.0f;
	};

	struct AudioPortalInput
	{
		UUID ID = 0, ZoneA = 0, ZoneB = 0;
		glm::mat4 Transform{ 1.0f };
		glm::vec3 HalfExtents{ 0.5f, 1.0f, 0.05f };
		float Open = 0.0f, BlendDistance = 5.0f;
		bool Enabled = true;
	};

	// Scene-owned, main-thread only. No dependency on physics, rendering or scripting.
	class AudioZoneSystem
	{
	public:
		static bool Validate(const AudioZoneComponent& component);
		static bool Validate(const AudioPortalComponent& component);
		static bool ValidatePortal(const AudioPortalInput& portal);
		static float Evaluate(const AudioZoneVolume& volume, const glm::vec3& position, float blendDistance);
		static void Blend(std::span<AudioZoneInput> zones, const AudioListener::States& listeners, std::span<const AudioPortalInput> portals = {});
		void Update(std::span<AudioZoneInput> zones, const AudioListener::States& listeners, float timestep,
			bool paused, AudioZoneReverbMode mode, bool raytracedReverbValid, std::span<const AudioPortalInput> portals = {});
		void Remove(UUID id);
		void Clear();
		float GetWeight(UUID id) const;
		float GetRaytracedReverbGain() const { return m_RaytracedReverbGain; }

	private:
		struct Voice
		{
			std::string Reference;
			Ref<AudioEventInstance> Instance;
			uint64_t AttemptRevision = UINT64_MAX;
			bool Started = false;
		};
		struct Zone
		{
			Voice Ambience;
			float Weight = 0.0f;
			std::string SnapshotReference;
			std::string SnapshotKey;
			bool Seen = false;
		};
		struct Snapshot
		{
			Voice Event;
			float Weight = 0.0f;
			bool Seen = false;
		};
		bool Prepare(Voice& voice, const std::string& reference, bool snapshot);
		static void Play(Voice& voice, float weight, bool paused);
		std::unordered_map<UUID, Zone> m_Zones;
		std::unordered_map<std::string, Snapshot> m_Snapshots;
		std::unordered_set<UUID> m_InvalidZones, m_InvalidPortals;
		float m_RaytracedReverbGain = 1.0f;
	};
}
