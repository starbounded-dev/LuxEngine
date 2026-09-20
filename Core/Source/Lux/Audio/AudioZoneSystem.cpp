// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioZoneSystem.h"
#include "AudioEngine.h"
#include "Lux/Scene/Components.h"
#include "Lux/Utilities/StringUtils.h"

#include <algorithm>
#include <cmath>

namespace Lux {

	namespace
	{
		bool Finite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}
	}

	bool AudioZoneSystem::Validate(const AudioPortalComponent& component)
	{
		return Finite(component.HalfExtents) && glm::all(glm::greaterThan(component.HalfExtents, glm::vec3(0))) &&
			std::isfinite(component.Open) && component.Open >= 0 && component.Open <= 1 &&
			std::isfinite(component.BlendDistance) && component.BlendDistance > 0 && IsValidAcousticMaterial(component.Material);
	}

	bool AudioZoneSystem::ValidatePortal(const AudioPortalInput& portal)
	{
		if (!portal.ID || !portal.ZoneA || !portal.ZoneB || portal.ZoneA == portal.ZoneB || !Finite(portal.HalfExtents) ||
			glm::any(glm::lessThanEqual(portal.HalfExtents, glm::vec3(0))) || !std::isfinite(portal.Open) || portal.Open < 0 || portal.Open > 1 ||
			!std::isfinite(portal.BlendDistance) || portal.BlendDistance <= 0)
			return false;
		for (int c = 0; c < 4; ++c)
			if (!Finite(glm::vec3(portal.Transform[c])) || !std::isfinite(portal.Transform[c].w))
				return false;
		const double determinant = glm::determinant(glm::dmat3(portal.Transform));
		return std::isfinite(determinant) && determinant != 0 && portal.Transform[0].w == 0 &&
			portal.Transform[1].w == 0 && portal.Transform[2].w == 0 && portal.Transform[3].w == 1;
	}

	bool AudioZoneSystem::Validate(const AudioZoneComponent& component)
	{
		return component.Shape <= AudioZoneShape::Collider && Finite(component.Offset) && Finite(component.HalfExtents)
			&& glm::all(glm::greaterThan(component.HalfExtents, glm::vec3(0.0f)))
			&& std::isfinite(component.Radius) && component.Radius > 0.0f && std::isfinite(component.Priority)
			&& std::isfinite(component.BlendDistance) && component.BlendDistance >= 0.0f
			&& std::isfinite(component.FadeTime) && component.FadeTime >= 0.0f
			&& std::isfinite(component.Volume) && component.Volume >= 0.0f;
	}

	float AudioZoneSystem::Evaluate(const AudioZoneVolume& volume, const glm::vec3& position, float blendDistance)
	{
		if (!volume.Valid || !Finite(position) || !std::isfinite(blendDistance) || blendDistance < 0.0f)
			return 0.0f;
		for (int column = 0; column < 4; ++column)
		{
			if (!Finite(glm::vec3(volume.Transform[column])) || !std::isfinite(volume.Transform[column].w))
				return 0.0f;
		}
		const float determinant = glm::determinant(glm::mat3(volume.Transform));
		if (!std::isfinite(determinant) || determinant == 0.0f)
			return 0.0f;
		float depth = 0.0f;
		if (volume.Type == AudioZoneVolume::Shape::Box)
		{
			const glm::mat4 inverse = glm::inverse(volume.Transform);
			const glm::vec3 remaining = volume.HalfExtents - glm::abs(glm::vec3(inverse * glm::vec4(position, 1.0f)));
			depth = std::numeric_limits<float>::max();
			for (int axis = 0; axis < 3; ++axis)
				depth = std::min(depth, remaining[axis] / glm::length(glm::vec3(inverse[0][axis], inverse[1][axis], inverse[2][axis])));
		}
		else
		{
			const glm::vec3 scale(glm::length(glm::vec3(volume.Transform[0])), glm::length(glm::vec3(volume.Transform[1])), glm::length(glm::vec3(volume.Transform[2])));
			glm::vec3 relative = position - glm::vec3(volume.Transform[3]);
			float radius = volume.Radius * std::max({ scale.x, scale.y, scale.z });
			if (volume.Type == AudioZoneVolume::Shape::Capsule)
			{
				const glm::vec3 axis = glm::vec3(volume.Transform[1]) / scale.y;
				const float height = volume.HalfHeight * scale.y;
				relative -= axis * std::clamp(glm::dot(relative, axis), -height, height);
				radius = volume.Radius * std::max(scale.x, scale.z);
			}
			depth = radius - glm::length(relative);
		}
		if (!std::isfinite(depth) || depth < 0.0f)
			return 0.0f;
		return blendDistance == 0.0f ? 1.0f : std::clamp(depth / blendDistance, 0.0f, 1.0f);
	}

	void AudioZoneSystem::Blend(std::span<AudioZoneInput> zones, const AudioListener::States& listeners, std::span<const AudioPortalInput> portals)
	{
		for (auto& zone : zones)
			zone.Target = 0.0f;
		std::sort(zones.begin(), zones.end(), [](const auto& a, const auto& b)
		{
			const float aPriority = std::isfinite(a.Component->Priority) ? a.Component->Priority : 0.0f;
			const float bPriority = std::isfinite(b.Component->Priority) ? b.Component->Priority : 0.0f;
			return aPriority == bPriority ? static_cast<uint64_t>(a.ID) < static_cast<uint64_t>(b.ID) : aPriority > bPriority;
		});
		float totalWeight = 0.0f;
		for (const auto& listener : listeners)
			if (std::isfinite(listener.Weight) && listener.Weight > 0.0f)
				totalWeight += listener.Weight;
		if (totalWeight <= 0.0f)
			return;
		for (const auto& listener : listeners)
		{
			if (!std::isfinite(listener.Weight) || listener.Weight <= 0.0f)
				continue;
			for (auto& zone : zones)
			{
				zone.ListenerTarget = zone.PortalDelta = zone.PortalOutgoing = 0.0f;
			}
			float remaining = listener.Weight / totalWeight;
			const glm::vec3 position = listener.UseAttenuationPosition ? listener.AttenuationPosition : listener.Position;
			for (size_t begin = 0; begin < zones.size();)
			{
				size_t end = begin + 1;
				while (end < zones.size() && zones[end].Component->Priority == zones[begin].Component->Priority)
					++end;
				float sum = 0.0f;
				for (size_t i = begin; i < end; ++i)
					if (zones[i].Component->Enabled)
						sum += Evaluate(zones[i].Volume, position, zones[i].Component->BlendDistance);
				const float coverage = std::min(sum, 1.0f);
				if (sum > 0.0f)
				{
					for (size_t i = begin; i < end; ++i)
						if (zones[i].Component->Enabled)
							zones[i].ListenerTarget += remaining * coverage * Evaluate(zones[i].Volume, position, zones[i].Component->BlendDistance) / sum;
				}
				remaining *= 1.0f - coverage;
				begin = end;
			}
			const auto visit = [&](auto transfer)
			{
				for (const auto& portal : portals)
				{
					if (!portal.Enabled || !ValidatePortal(portal) || portal.Open <= 0)
						continue;
					auto a = std::find_if(zones.begin(), zones.end(), [&](const auto& zone) { return zone.ID == portal.ZoneA; });
					auto b = std::find_if(zones.begin(), zones.end(), [&](const auto& zone) { return zone.ID == portal.ZoneB; });
					if (a == zones.end() || b == zones.end() || !a->Component->Enabled || !b->Component->Enabled || !a->Volume.Valid || !b->Volume.Valid)
						continue;
					const glm::vec3 local(glm::inverse(portal.Transform) * glm::vec4(position, 1));
					const glm::vec3 closest(portal.Transform * glm::vec4(glm::clamp(local, -portal.HalfExtents, portal.HalfExtents), 1));
					if (!Finite(local) || !Finite(closest))
						continue;
					const float proximity = std::clamp(1.0f - glm::distance(position, closest) / portal.BlendDistance, 0.0f, 1.0f);
					const float share = 0.5f * portal.Open * proximity;
					transfer(*a, *b, share);
					transfer(*b, *a, share);
				}
			};
			visit([](auto& from, auto&, float share) { from.PortalOutgoing += share; });
			visit([](auto& from, auto& to, float share)
			{
				const float amount = from.ListenerTarget * share / std::max(1.0f, from.PortalOutgoing);
				from.PortalDelta -= amount;
				to.PortalDelta += amount;
			});
			for (auto& zone : zones)
				zone.Target += std::max(0.0f, zone.ListenerTarget + zone.PortalDelta);
		}
	}

	bool AudioZoneSystem::Prepare(Voice& voice, const std::string& reference, bool snapshot)
	{
		if (voice.Reference != reference)
		{
			voice = {};
			voice.Reference = reference;
		}
		if (voice.Instance && !voice.Instance->IsValid())
		{
			voice.Instance = nullptr;
			voice.AttemptRevision = UINT64_MAX;
			voice.Started = false;
		}
		if (!voice.Instance && !reference.empty() && voice.AttemptRevision != AudioEngine::GetBankRevision())
		{
			voice.AttemptRevision = AudioEngine::GetBankRevision();
			voice.Instance = AudioEventInstance::Create(reference);
			if (voice.Instance && (snapshot ? !voice.Instance->SetSnapshotIntensity(0.0f) : voice.Instance->IsSnapshot() || voice.Instance->IsOneShot()))
			{
				if (!snapshot)
					LUX_CORE_ERROR_TAG("Audio", "Zone ambience {0} must be a looping event, not a one-shot or snapshot", reference);
				voice.Instance = nullptr;
			}
		}
		return static_cast<bool>(voice.Instance);
	}

	void AudioZoneSystem::Play(Voice& voice, float weight, bool paused)
	{
		if (!voice.Instance)
			return;
		voice.Instance->SetScenePaused(paused);
		if (weight > 0.0f && !voice.Started)
		{
			voice.Instance->Start();
			voice.Started = true;
		}
		else if (weight <= 0.0f && voice.Started)
		{
			voice.Instance->Stop(true);
			voice.Started = false;
		}
	}

	void AudioZoneSystem::Update(std::span<AudioZoneInput> zones, const AudioListener::States& listeners, float timestep,
		bool paused, AudioZoneReverbMode mode, bool raytracedReverbValid, std::span<const AudioPortalInput> portals)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		for (auto& [id, zone] : m_Zones)
			zone.Seen = false;
		for (auto& [reference, snapshot] : m_Snapshots)
		{
			snapshot.Weight = 0.0f;
			snapshot.Seen = false;
		}
		for (auto& input : zones)
			input.Volume.Valid = input.Volume.Valid && Validate(*input.Component);
		for (const auto& portal : portals)
		{
			const bool linked = portal.ZoneA != 0 || portal.ZoneB != 0;
			const auto exists = [&](UUID id) { return std::any_of(zones.begin(), zones.end(), [&](const auto& zone) { return zone.ID == id; }); };
			if (portal.Enabled && linked && (!ValidatePortal(portal) || !exists(portal.ZoneA) || !exists(portal.ZoneB)))
			{
				if (m_InvalidPortals.insert(portal.ID).second)
					LUX_CORE_ERROR_TAG("Audio", "Invalid audio portal {}: link two different audio zones and use finite positive dimensions/range", static_cast<uint64_t>(portal.ID));
			}
			else
				m_InvalidPortals.erase(portal.ID);
		}
		std::erase_if(m_InvalidPortals, [&](UUID id) { return std::none_of(portals.begin(), portals.end(), [&](const auto& portal) { return portal.ID == id; }); });
		Blend(zones, listeners, portals);
		const float dt = paused || !std::isfinite(timestep) ? 0.0f : std::max(0.0f, timestep);
		for (auto& input : zones)
		{
			const auto& component = *input.Component;
			auto& zone = m_Zones[input.ID];
			zone.Seen = true;
			if (!Validate(component) || !input.Volume.Valid)
			{
				if (m_InvalidZones.insert(input.ID).second)
					LUX_CORE_ERROR_TAG("Audio", "Invalid audio zone {0}: check dimensions, transform, and exactly one supported primitive collider", static_cast<uint64_t>(input.ID));
				input.Target = 0.0f;
			}
			else
				m_InvalidZones.erase(input.ID);
			if (!paused)
			{
				const float step = std::isfinite(component.FadeTime) && component.FadeTime > 0.0f ? dt / component.FadeTime : 1.0f;
				zone.Weight += std::clamp(input.Target - zone.Weight, -step, step);
			}
			if (zone.Ambience.Reference != component.AmbienceEvent.Guid)
				zone.Ambience = {};
			if (zone.Weight > 0.0f)
				Prepare(zone.Ambience, component.AmbienceEvent.Guid, false);
			if (zone.Ambience.Instance)
			{
				zone.Ambience.Instance->Set3DAttributes(glm::vec3(input.Volume.Transform[3]), glm::vec3(0.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
				zone.Ambience.Instance->SetVolume(std::isfinite(component.Volume) ? zone.Weight * std::max(0.0f, component.Volume) : 0.0f);
				Play(zone.Ambience, zone.Weight, paused);
			}
			if (zone.SnapshotReference != component.Snapshot.Guid)
			{
				zone.SnapshotReference = component.Snapshot.Guid;
				zone.SnapshotKey = Utils::String::ToLowerCopy(component.Snapshot.Guid);
			}
			if (!zone.SnapshotKey.empty())
			{
				// A single instance per GUID: FMOD averages multiple instances of the same snapshot.
				auto& snapshot = m_Snapshots[zone.SnapshotKey];
				snapshot.Seen = true;
				snapshot.Weight += zone.Weight;
			}
		}
		std::erase_if(m_Zones, [this](const auto& pair)
		{
			if (!pair.second.Seen)
				m_InvalidZones.erase(pair.first);
			return !pair.second.Seen;
		});
		float snapshotCoverage = 0.0f;
		for (auto& [reference, snapshot] : m_Snapshots)
		{
			const float weight = mode == AudioZoneReverbMode::PreferRaytraced && raytracedReverbValid ? 0.0f : std::min(snapshot.Weight, 1.0f);
			if (weight > 0.0f)
				Prepare(snapshot.Event, reference, true);
			if (snapshot.Event.Instance && snapshot.Event.Instance->SetSnapshotIntensity(weight))
			{
				Play(snapshot.Event, weight, paused);
				snapshotCoverage += weight;
			}
		}
		std::erase_if(m_Snapshots, [](const auto& entry)
		{
			return !entry.second.Seen && (!entry.second.Event.Instance || !entry.second.Event.Instance->IsPlaying());
		});
		m_RaytracedReverbGain = mode == AudioZoneReverbMode::PreferZones ? 1.0f - std::min(snapshotCoverage, 1.0f) : 1.0f;
	}

	void AudioZoneSystem::Remove(UUID id)
	{
		m_Zones.erase(id);
		m_InvalidZones.erase(id);
	}

	void AudioZoneSystem::Clear()
	{
		m_Zones.clear();
		m_Snapshots.clear();
		m_InvalidZones.clear();
		m_InvalidPortals.clear();
		m_RaytracedReverbGain = 1.0f;
	}

	float AudioZoneSystem::GetWeight(UUID id) const
	{
		const auto it = m_Zones.find(id);
		return it == m_Zones.end() ? 0.0f : it->second.Weight;
	}
}
