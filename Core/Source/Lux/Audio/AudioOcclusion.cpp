// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "AudioOcclusion.h"
#include "Lux/Debug/Profiler.h"
#include <algorithm>
#include <cmath>

namespace Lux
{
	namespace
	{
		// VA defines a material's transmission distance as the thickness that leaves 0.1% of the
		// energy (vaMaterialConvertMetersToTransmission), i.e. a 30 dB loss.
		constexpr float k_TransmissionLossDb = 30.0f;
		constexpr float k_MinTransmissionMetres = 0.001f;
		constexpr float k_MaxLossDb = 60.0f;
		// Coplanar or shared-edge triangles report the same crossing more than once.
		constexpr float k_HitMergeDistance = 0.001f;
		// Parameter smoothing so a wall edge sliding across the path does not click the filter.
		constexpr float k_SmoothingSeconds = 0.1f;

		float FlatLossDb(float energyFraction)
		{
			return -10.0f * std::log10(std::max(1.0f - energyFraction, 1e-6f));
		}

		float GainFromLossDb(float lossDb)
		{
			return std::pow(10.0f, -lossDb / 20.0f);
		}

		// Segment against the unit box [-1, 1]^3 in box space. Returns the entry/exit fractions.
		bool IntersectUnitBox(const glm::vec3& from, const glm::vec3& to, float& outEnter, float& outExit)
		{
			const glm::vec3 delta = to - from;
			float enter = 0.0f;
			float exit = 1.0f;
			for (int axis = 0; axis < 3; ++axis)
			{
				if (std::abs(delta[axis]) < 1e-8f)
				{
					if (from[axis] < -1.0f || from[axis] > 1.0f)
						return false;
					continue;
				}
				float t0 = (-1.0f - from[axis]) / delta[axis];
				float t1 = (1.0f - from[axis]) / delta[axis];
				if (t0 > t1)
					std::swap(t0, t1);
				enter = std::max(enter, t0);
				exit = std::min(exit, t1);
				if (enter > exit)
					return false;
			}
			outEnter = enter;
			outExit = exit;
			return exit > enter;
		}
	}

	void AudioOcclusion::Configure(const AcousticMaterialSettings& materials, const AudioOcclusionSettings& settings)
	{
		m_Settings = settings;
		if (!m_Settings.Validate())
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid occlusion settings; using defaults");
			m_Settings = {};
		}
		for (size_t i = 0; i < AcousticMaterialCount; ++i)
		{
			const AcousticMaterialOverride& entry = materials.Overrides[i];
			m_Properties[i] = entry.Enabled ? entry.Properties : GetDefaultAcousticMaterialProperties(static_cast<AcousticMaterial>(i));
		}
	}

	void AudioOcclusion::SetGeometry(std::span<const AudioGeometryInput> inputs, const std::function<float(UUID)>& portalOpen)
	{
		m_Materials.clear();
		m_Portals.clear();
		for (const AudioGeometryInput& input : inputs)
		{
			if (input.Mode == AcousticGeometryMode::Disabled)
				continue;
			if (!input.Portal)
			{
				m_Materials.emplace_back(input.Entity, input.Material);
				continue;
			}

			AudioGeometryInput applied = input;
			applied.Open = portalOpen ? portalOpen(input.Entity) : input.Open;
			if (!AudioGeometrySystem::Validate(applied) || applied.Open >= 1.0f)
				continue;
			m_Portals.push_back({ input.Entity, input.Material, glm::inverse(AudioGeometrySystem::PortalTransform(applied)) });
		}
		std::sort(m_Materials.begin(), m_Materials.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	}

	const AcousticMaterial* AudioOcclusion::FindMaterial(UUID entity) const
	{
		const auto found = std::lower_bound(m_Materials.begin(), m_Materials.end(), entity, [](const auto& entry, UUID value) { return entry.first < value; });
		return found != m_Materials.end() && found->first == entity ? &found->second : nullptr;
	}

	void AudioOcclusion::AddWall(AudioOcclusionPath& path, UUID entity, AcousticMaterial material, float start, float end, bool flat, bool portal) const
	{
		const AcousticMaterialProperties& properties = m_Properties[IsValidAcousticMaterial(material) ? static_cast<size_t>(material) : 0];
		AudioOcclusionWall& wall = path.Walls.emplace_back();
		wall.Entity = entity;
		wall.Material = material;
		wall.Start = start;
		wall.End = end;
		wall.Flat = flat;
		wall.Portal = portal;
		if (flat)
		{
			wall.LossLF = m_Settings.Strength * FlatLossDb(properties.FlatTransmissionLF);
			wall.LossHF = m_Settings.Strength * FlatLossDb(properties.FlatTransmissionHF);
		}
		else
		{
			const float thickness = end - start;
			const float lossPerMetre = m_Settings.Strength * k_TransmissionLossDb;
			wall.LossLF = lossPerMetre * thickness / std::max(properties.TransmissionLF, k_MinTransmissionMetres);
			wall.LossHF = lossPerMetre * thickness / std::max(properties.TransmissionHF, k_MinTransmissionMetres);
		}
	}

	void AudioOcclusion::Evaluate(const glm::vec3& from, const glm::vec3& to, std::vector<AudioOcclusionHit>& hits, AudioOcclusionPath& outPath) const
	{
		outPath.From = from;
		outPath.To = to;
		outPath.LossLF = 0.0f;
		outPath.LossHF = 0.0f;
		outPath.Walls.clear();

		const float length = glm::distance(from, to);
		if (length <= k_HitMergeDistance)
			return;

		std::sort(hits.begin(), hits.end(), [](const AudioOcclusionHit& a, const AudioOcclusionHit& b) { return a.Distance < b.Distance; });

		// Pair each entry with the same entity's next exit to measure a solid's thickness. A crossing
		// that never pairs is a one-sided or non-watertight surface: VA charges those a flat loss.
		m_OpenSolids.clear();
		for (const AudioOcclusionHit& hit : hits)
		{
			if (hit.Distance <= 0.0f || hit.Distance >= length)
				continue;
			const AcousticMaterial* material = FindMaterial(hit.Entity);
			if (!material)
				continue;

			auto open = std::find_if(m_OpenSolids.begin(), m_OpenSolids.end(), [&](const auto& entry) { return entry.first == hit.Entity; });
			if (!hit.Exit)
			{
				if (open == m_OpenSolids.end())
				{
					m_OpenSolids.emplace_back(hit.Entity, hit.Distance);
				}
				else if (hit.Distance - open->second > k_HitMergeDistance)
				{
					AddWall(outPath, hit.Entity, *material, open->second, open->second, true, false);
					open->second = hit.Distance;
				}
				continue;
			}

			if (open != m_OpenSolids.end())
			{
				AddWall(outPath, hit.Entity, *material, open->second, hit.Distance, false, false);
				m_OpenSolids.erase(open);
				continue;
			}

			const bool duplicateExit = !outPath.Walls.empty() && outPath.Walls.back().Entity == hit.Entity
				&& hit.Distance - outPath.Walls.back().End <= k_HitMergeDistance;
			if (!duplicateExit)
				AddWall(outPath, hit.Entity, *material, hit.Distance, hit.Distance, true, false);
		}
		for (const auto& [entity, start] : m_OpenSolids)
			AddWall(outPath, entity, *FindMaterial(entity), start, start, true, false);

		for (const Portal& portal : m_Portals)
		{
			const glm::vec3 boxFrom = glm::vec3(portal.WorldToBox * glm::vec4(from, 1.0f));
			const glm::vec3 boxTo = glm::vec3(portal.WorldToBox * glm::vec4(to, 1.0f));
			float enter = 0.0f;
			float exit = 0.0f;
			if (IntersectUnitBox(boxFrom, boxTo, enter, exit))
				AddWall(outPath, portal.Entity, portal.Material, enter * length, exit * length, false, true);
		}

		std::sort(outPath.Walls.begin(), outPath.Walls.end(), [](const AudioOcclusionWall& a, const AudioOcclusionWall& b) { return a.Start < b.Start; });
		for (const AudioOcclusionWall& wall : outPath.Walls)
		{
			outPath.LossLF += wall.LossLF;
			outPath.LossHF += wall.LossHF;
		}
		outPath.LossLF = std::min(outPath.LossLF, k_MaxLossDb);
		outPath.LossHF = std::min(outPath.LossHF, k_MaxLossDb);
	}

	void AudioOcclusion::Update(float timestep, const glm::vec3& listener, std::span<const Source> sources, const RayCaster& cast)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		++m_UpdateIndex;
		const float interval = 1.0f / m_Settings.UpdateRateHz;
		const float smoothing = 1.0f - std::exp(-std::max(timestep, 0.0f) / k_SmoothingSeconds);

		for (const Source& source : sources)
		{
			SourceState& state = m_Sources[source.Entity];
			state.SeenUpdate = m_UpdateIndex;
			state.SinceEvaluation += timestep;
		}

		// Round-robin from where the last update stopped, so a budget cut never starves the tail.
		size_t evaluations = 0;
		const size_t count = sources.size();
		const size_t first = count ? m_NextSource % count : 0;
		for (size_t i = 0; i < count && evaluations < m_Settings.CastBudget; ++i)
		{
			const Source& source = sources[(first + i) % count];
			SourceState& state = m_Sources[source.Entity];
			if (state.Evaluated && state.SinceEvaluation < interval)
				continue;

			m_Hits.clear();
			if (cast)
				cast(source.Entity, listener, source.Position, m_Hits);
			Evaluate(listener, source.Position, m_Hits, state.Path);
			state.TargetGainLF = GainFromLossDb(state.Path.LossLF);
			if (!state.Evaluated)
				state.GainLF = state.TargetGainLF;
			state.Evaluated = true;
			state.SinceEvaluation = 0.0f;
			++evaluations;
			m_NextSource = (first + i + 1) % count;
		}

		for (auto it = m_Sources.begin(); it != m_Sources.end();)
		{
			if (it->second.SeenUpdate != m_UpdateIndex)
			{
				it = m_Sources.erase(it);
				continue;
			}
			it->second.GainLF += (it->second.TargetGainLF - it->second.GainLF) * smoothing;
			++it;
		}
	}

	float AudioOcclusion::GetGainLF(UUID source) const
	{
		const auto found = m_Sources.find(source);
		return found != m_Sources.end() && found->second.Evaluated ? found->second.GainLF : 1.0f;
	}

	const AudioOcclusionPath* AudioOcclusion::GetPath(UUID source) const
	{
		const auto found = m_Sources.find(source);
		return found != m_Sources.end() && found->second.Evaluated ? &found->second.Path : nullptr;
	}

	void AudioOcclusion::Clear()
	{
		m_Materials.clear();
		m_Portals.clear();
		m_Sources.clear();
		m_Hits.clear();
		m_NextSource = 0;
	}
}
