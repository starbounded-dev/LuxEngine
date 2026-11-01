// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#include "lpch.h"
#include "AudioAcoustics.h"
#include "Lux/Core/JobSystem.h"
#include "Lux/Debug/Profiler.h"
#include <algorithm>
#include <cmath>

namespace Lux
{
	namespace
	{
		constexpr float kSmoothingSeconds = 0.1f;
		bool Finite(Soundwave::Position point)
		{
			return std::isfinite(point.X) && std::isfinite(point.Y) && std::isfinite(point.Z);
		}
	}

	AudioAcoustics::AudioAcoustics()
		: m_Materials(Soundwave::DefaultMaterials()), m_KernelSettings(m_Settings.GetKernelSettings())
	{
	}

	bool AudioAcoustics::Configure(const AcousticMaterialSettings& materials, const AudioOcclusionSettings& settings)
	{
		if (!materials.Validate() || !settings.Validate())
		{
			LUX_CORE_ERROR_TAG("Audio", "Cannot configure Soundwave: invalid materials or kernel settings");
			return false;
		}
		static_assert(AcousticMaterialCount == Soundwave::kMaterialTagCount);
		auto table = Soundwave::DefaultMaterials();
		for (size_t i = 0; i < table.size(); ++i)
		{
			const auto& entry = materials.Overrides[i];
			if (entry.Enabled)
			{
				const auto& p = entry.Properties;
				table[i] = {p.AbsorptionLF,p.AbsorptionHF,p.Scattering,p.TransmissionLF,p.TransmissionHF,p.FlatTransmissionLF,p.FlatTransmissionHF};
			}
		}
		m_Materials = table;
		m_Settings = settings;
		m_KernelSettings = settings.GetKernelSettings();
		++m_SettingsGeneration;
		return true;
	}

	bool AudioAcoustics::RequestGeometry(uint64_t revision, Builder fixed, Builder moving)
	{
		if (!revision || revision < m_RequestedRevision || (!fixed && !moving) || (!m_RequestedRevision && (!fixed || !moving)))
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid Soundwave geometry request (revision {})", revision);
			return false;
		}
		if (revision == m_RequestedRevision)
			return true;
		m_RequestedRevision = revision;
		if (fixed)
		{
			m_Builders[0] = std::move(fixed);
			m_LayerRevisions[0] = revision;
			m_DirtyLayers |= 1;
		}
		if (moving)
		{
			m_Builders[1] = std::move(moving);
			m_LayerRevisions[1] = revision;
			m_DirtyLayers |= 2;
		}
		return true;
	}

	bool AudioAcoustics::CanPrepare() const
	{
		if (!m_DirtyLayers || m_LastAttemptedRevision == m_RequestedRevision)
			return false;
		if (!m_FailedLayers)
			return true;
		for (uint32_t i = 0; i < 2; ++i)
			if ((m_FailedLayers & (1u << i)) && m_LayerRevisions[i] != m_FailureLayerRevisions[i])
				return true;
		return false; // unrelated door movement must not rebuild/log the same invalid room
	}

	void AudioAcoustics::PollGeometry()
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (m_Pending && m_Pending->Done.load(std::memory_order_acquire))
		{
			if (m_Pending->Result[0] && m_Pending->Result[1])
			{
				// Completed movement still publishes when a newer position is queued;
				// discarding every old result would starve a continuously moving door.
				m_Snapshots = m_Pending->Result;
				m_FailedRevision = 0;
				m_FailedLayers = 0;
				for (uint32_t i = 0; i < 2; ++i)
					if ((m_Pending->ChangedLayers & (1u << i)) && m_Pending->LayerRevisions[i] == m_LayerRevisions[i])
						m_DirtyLayers &= ~(1u << i);
				++m_GeometryGeneration;
				RefreshGeometryView();
			}
			else
			{
				m_FailedRevision = m_Pending->Revision;
				m_FailedLayers = m_Pending->FailedLayers;
				m_FailureLayerRevisions = m_Pending->LayerRevisions;
				LUX_CORE_ERROR_TAG("Audio", "Soundwave geometry revision {} failed: {}", m_FailedRevision, m_Pending->Error);
			}
			m_Pending = nullptr;
		}
		if (m_Pending || !CanPrepare())
			return;
		m_Pending = Ref<Job>::Create();
		m_Pending->Revision = m_RequestedRevision;
		m_Pending->Result = m_Snapshots;
		m_Pending->ChangedLayers = m_DirtyLayers;
		m_Pending->LayerRevisions = m_LayerRevisions;
		m_LastAttemptedRevision = m_RequestedRevision;
		Ref<Job> job = m_Pending;
		std::array<Builder, 2> builders;
		for (uint32_t i = 0; i < 2; ++i)
			if (m_DirtyLayers & (1u << i))
				builders[i] = m_Builders[i];
		JobSystem::Submit([job, builders = std::move(builders)]() mutable
		{
			LUX_PROFILE_SCOPE("Soundwave::PrepareGeometry");
			try
			{
				for (uint32_t i = 0; i < 2; ++i)
				{
					if (!builders[i])
						continue;
					job->FailedLayers = 1u << i;
					GeometryInput input;
					if (!builders[i](input, job->Error))
					{
						if (job->Error.empty())
							job->Error = "geometry capture rejected input";
						break;
					}
					Ref<Snapshot> snapshot = Ref<Snapshot>::Create();
					const auto error = snapshot->Grid.Build(input.Triangles);
					if (error != Soundwave::GeometryError::None)
					{
						job->Error = std::format("invalid triangle snapshot (error {})", static_cast<uint32_t>(error));
						break;
					}
					snapshot->Portals = std::move(input.Portals);
					Soundwave::DirectScratch scratch;
					Soundwave::Path path;
					if (Soundwave::EvaluateDirect(snapshot->Grid, snapshot->Portals, Soundwave::DefaultMaterials(), {}, {}, {}, 0,
						scratch, path) != Soundwave::EvaluationError::None)
					{
						job->Error = "invalid portal snapshot";
						break;
					}
					for (const auto& triangle : snapshot->Grid.GetTriangles())
						snapshot->Instances.push_back(triangle.Instance);
					for (const auto& portal : snapshot->Portals)
						snapshot->Instances.push_back(portal.Instance);
					std::sort(snapshot->Instances.begin(), snapshot->Instances.end());
					snapshot->Instances.erase(std::unique(snapshot->Instances.begin(), snapshot->Instances.end()), snapshot->Instances.end());
					job->Result[i] = snapshot;
				}
				if (job->Error.empty() && job->Result[0] && job->Result[1])
				{
					job->FailedLayers = 3;
					const auto& a = job->Result[0]->Instances;
					const auto& b = job->Result[1]->Instances;
					for (const auto id : a)
						if (std::binary_search(b.begin(), b.end(), id))
						{
							job->Error = std::format("instance {} appears in both geometry layers", id);
							break;
						}
				}
			}
			catch (const std::exception& error)
			{
				job->Error = error.what();
			}
			if (!job->Error.empty())
				job->Result = {};
			job->Done.store(true, std::memory_order_release);
		});
	}

	bool AudioAcoustics::SetActiveInstances(std::span<const uint64_t> instances)
	{
		if (!std::is_sorted(instances.begin(), instances.end()) ||
			std::adjacent_find(instances.begin(), instances.end()) != instances.end() || (!instances.empty() && !instances.front()))
		{
			LUX_CORE_ERROR_TAG("Audio", "Soundwave live instance list must contain sorted unique nonzero UUIDs");
			return false;
		}
		if (!m_HasActiveInstances || !std::equal(instances.begin(), instances.end(), m_ActiveInstances.begin(), m_ActiveInstances.end()))
		{
			m_ActiveInstances.assign(instances.begin(), instances.end());
			m_HasActiveInstances = true;
			++m_GeometryGeneration;
			RefreshGeometryView();
		}
		return true;
	}

	void AudioAcoustics::RefreshGeometryView()
	{
		m_Excluded.clear();
		m_Portals.clear();
		for (const auto& snapshot : m_Snapshots)
		{
			if (!snapshot)
				continue;
			if (m_HasActiveInstances)
				for (const auto instance : snapshot->Instances)
					if (!std::binary_search(m_ActiveInstances.begin(), m_ActiveInstances.end(), instance))
						m_Excluded.push_back(instance);
			m_Portals.insert(m_Portals.end(), snapshot->Portals.begin(), snapshot->Portals.end());
		}
		std::sort(m_Excluded.begin(), m_Excluded.end());
		m_Excluded.erase(std::unique(m_Excluded.begin(), m_Excluded.end()), m_Excluded.end());
		std::sort(m_Portals.begin(), m_Portals.end(), [](const auto& a, const auto& b) { return a.Instance < b.Instance; });
	}

	bool AudioAcoustics::Update(float timestep, Soundwave::Position listener, std::span<const Source> sources)
	{
		LUX_PROFILE_FUNCTION_AUTO;
		if (!std::isfinite(timestep) || timestep < 0 || !Finite(listener))
		{
			LUX_CORE_ERROR_TAG("Audio", "Invalid Soundwave source update: timestep or listener is not finite");
			return false;
		}
		m_SourceIDs.clear();
		for (const auto& source : sources)
		{
			if (source.Entity == 0 || !Finite(source.Position))
			{
				LUX_CORE_ERROR_TAG("Audio", "Invalid Soundwave source UUID or position ({})", static_cast<uint64_t>(source.Entity));
				return false;
			}
			m_SourceIDs.push_back(source.Entity);
		}
		std::sort(m_SourceIDs.begin(), m_SourceIDs.end());
		if (std::adjacent_find(m_SourceIDs.begin(), m_SourceIDs.end()) != m_SourceIDs.end())
		{
			LUX_CORE_ERROR_TAG("Audio", "Duplicate source UUID in Soundwave update");
			return false;
		}
		++m_UpdateIndex;
		for (const auto& source : sources)
		{
			auto& state = m_Sources[source.Entity];
			state.SeenUpdate = m_UpdateIndex;
			state.SinceEvaluation += timestep;
		}
		const float interval = 1.0f / m_Settings.UpdateRateHz;
		const float smoothing = 1.0f - std::exp(-timestep / kSmoothingSeconds);
		const bool ready = m_Snapshots[0] && m_Snapshots[1];
		const auto& fixed = m_Snapshots[0] ? m_Snapshots[0]->Grid : m_Empty;
		const auto* moving = m_Snapshots[1] ? &m_Snapshots[1]->Grid : nullptr;
		const Soundwave::GeometryView geometry(fixed, moving, m_Excluded);
		size_t evaluations = 0;
		const size_t first = sources.empty() ? 0 : m_NextSource % sources.size();
		for (size_t i = 0; ready && i < sources.size() && evaluations < m_Settings.CastBudget; ++i)
		{
			const auto& source = sources[(first + i) % sources.size()];
			auto& state = m_Sources[source.Entity];
			if (state.Attempted && state.SinceEvaluation < interval && state.GeometryGeneration == m_GeometryGeneration &&
				state.SettingsGeneration == m_SettingsGeneration)
				continue;
			const auto error = Soundwave::Solve(geometry, m_Portals, m_Materials, m_KernelSettings, listener, source.Position,
				source.Entity, m_Scratch, state.Attempt);
			if (error == Soundwave::EvaluationError::None)
			{
				std::swap(state.Result, state.Attempt);
				state.TargetLF = state.Result.OcclusionGainLF;
				state.TargetHF = state.Result.OcclusionGainHF;
				if (!state.Evaluated)
				{
					state.GainLF = state.TargetLF;
					state.GainHF = state.TargetHF;
				}
				state.Evaluated = true;
			}
			else
			{
				++m_SolveFailures;
				if (state.LastError != error)
					LUX_CORE_ERROR_TAG("Audio", "Soundwave source {} solve failed (error {}); retaining its last valid result",
						static_cast<uint64_t>(source.Entity), static_cast<uint32_t>(error));
			}
			state.LastError = error;
			state.Attempted = true;
			state.GeometryGeneration = m_GeometryGeneration;
			state.SettingsGeneration = m_SettingsGeneration;
			state.SinceEvaluation = 0;
			++evaluations;
			++m_Evaluations;
			m_NextSource = (first + i + 1) % sources.size();
		}
		for (auto it = m_Sources.begin(); it != m_Sources.end();)
		{
			if (it->second.SeenUpdate != m_UpdateIndex)
			{
				it = m_Sources.erase(it);
				continue;
			}
			it->second.GainLF += (it->second.TargetLF - it->second.GainLF) * smoothing;
			it->second.GainHF += (it->second.TargetHF - it->second.GainHF) * smoothing;
			++it;
		}
		return true;
	}

	float AudioAcoustics::GetGainLF(UUID source) const
	{
		const auto found = m_Sources.find(source);
		return found != m_Sources.end() ? found->second.GainLF : 1;
	}
	float AudioAcoustics::GetGainHF(UUID source) const
	{
		const auto found = m_Sources.find(source);
		return found != m_Sources.end() ? found->second.GainHF : 1;
	}
	const Soundwave::SolveResult* AudioAcoustics::GetResult(UUID source) const
	{
		const auto found = m_Sources.find(source);
		return found != m_Sources.end() && found->second.Evaluated ? &found->second.Result : nullptr;
	}
	AudioAcoustics::Diagnostics AudioAcoustics::GetDiagnostics() const
	{
		Diagnostics result;
		result.GeometryGeneration = m_GeometryGeneration;
		result.Evaluations = m_Evaluations;
		result.SolveFailures = m_SolveFailures;
		result.StaticTriangles = m_Snapshots[0] ? static_cast<uint32_t>(m_Snapshots[0]->Grid.GetTriangles().size()) : 0;
		result.MovingTriangles = m_Snapshots[1] ? static_cast<uint32_t>(m_Snapshots[1]->Grid.GetTriangles().size()) : 0;
		result.ExcludedInstances = static_cast<uint32_t>(m_Excluded.size());
		result.Ready = m_Snapshots[0] && m_Snapshots[1];
		for (uint32_t i = 0; i < 2; ++i)
			result.PendingLayers += ((m_Pending && (m_Pending->ChangedLayers & (1u << i))) ||
				(CanPrepare() && (m_DirtyLayers & (1u << i)))) ? 1 : 0;
		result.GeometryFailed = m_FailedRevision != 0;
		return result;
	}
	void AudioAcoustics::Clear()
	{
		m_Snapshots = {};
		m_Builders = {};
		m_LayerRevisions = {};
		m_DirtyLayers = m_FailedLayers = 0;
		m_FailureLayerRevisions = {};
		m_Pending = nullptr;
		m_RequestedRevision = m_FailedRevision = m_LastAttemptedRevision = 0;
		m_ActiveInstances.clear();
		m_Excluded.clear();
		m_Portals.clear();
		m_SourceIDs.clear();
		m_Sources.clear();
		m_HasActiveInstances = false;
		m_NextSource = 0;
		m_UpdateIndex = m_GeometryGeneration = m_Evaluations = m_SolveFailures = 0;
	}
}
