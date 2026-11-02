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
		constexpr float kReverbSmoothingSeconds = 0.5f;
		constexpr float kReverbMoveMetres = 1.0f; // listener travel that triggers a new measurement
		// A GPU measurement normally returns in FramesInFlight frames; this many updates without it
		// means frames are not rendering (minimized, paused) and is reported, not waited on.
		constexpr uint32_t kReverbStallUpdates = 30;
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
					const auto error = snapshot->Raw.Build(input.Triangles);
					if (error != Soundwave::GeometryError::None)
					{
						job->Error = std::format("invalid triangle snapshot (error {})", static_cast<uint32_t>(error));
						break;
					}
					std::vector<Soundwave::GeometryInstance> instances;
					instances.reserve(input.Meshes.size() + 1);
					if (!input.Triangles.empty())
					{
						Soundwave::GeometryInstance raw;
						raw.Geometry = &snapshot->Raw;
						instances.push_back(std::move(raw));
					}
					std::vector<const Soundwave::Grid*> stored;
					for (auto& mesh : input.Meshes)
					{
						if (!mesh.Geometry)
						{
							job->Error = "missing shared mesh owner";
							break;
						}
						mesh.Description.Geometry = &mesh.Geometry->Grid;
						stored.push_back(mesh.Description.Geometry);
						if (mesh.Description.Instance)
							snapshot->Instances.push_back(mesh.Description.Instance);
						else
							for (const auto& triangle : mesh.Geometry->Grid.GetTriangles())
								snapshot->Instances.push_back(triangle.Instance);
						instances.push_back(std::move(mesh.Description));
						snapshot->Meshes.push_back(std::move(mesh.Geometry));
					}
					if (!job->Error.empty())
						break;
					const auto instanceError = snapshot->Grid.Build(instances);
					if (instanceError != Soundwave::InstanceError::None)
					{
						job->Error = std::format("invalid shared-mesh snapshot (error {})", static_cast<uint32_t>(instanceError));
						break;
					}
					std::sort(stored.begin(), stored.end(), std::less<const Soundwave::Grid*>{});
					stored.erase(std::unique(stored.begin(), stored.end()), stored.end());
					snapshot->StoredTriangles = static_cast<uint32_t>(input.Triangles.size());
					for (const auto* grid : stored)
						snapshot->StoredTriangles += static_cast<uint32_t>(grid->GetTriangles().size());
					snapshot->Portals = std::move(input.Portals);
					Soundwave::DirectScratch scratch;
					Soundwave::Path path;
					if (Soundwave::EvaluateDirect(Soundwave::GeometryView(snapshot->Grid), snapshot->Portals,
						Soundwave::DefaultMaterials(), {}, {}, {}, 0, scratch, path) != Soundwave::EvaluationError::None)
					{
						job->Error = "invalid portal snapshot";
						break;
					}
					for (const auto& triangle : snapshot->Raw.GetTriangles())
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
					if (!Soundwave::GeometryView(job->Result[0]->Grid, &job->Result[1]->Grid).IsValid())
						job->Error = "combined shared-mesh snapshot exceeds its index space";
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
		PollSolves();
		std::vector<SolveJob::Item> due;
		const size_t first = sources.empty() ? 0 : m_NextSource % sources.size();
		// One batch in flight; until it publishes, sources keep their last results and cadence.
		for (size_t i = 0; ready && !m_SolvePending && i < sources.size() && due.size() < m_Settings.CastBudget; ++i)
		{
			const auto& source = sources[(first + i) % sources.size()];
			auto& state = m_Sources[source.Entity];
			if (state.Attempted && state.SinceEvaluation < interval && state.GeometryGeneration == m_GeometryGeneration &&
				state.SettingsGeneration == m_SettingsGeneration)
				continue;
			due.push_back({ source.Entity, source.Position });
			state.Attempted = true;
			state.GeometryGeneration = m_GeometryGeneration;
			state.SettingsGeneration = m_SettingsGeneration;
			state.SinceEvaluation = 0;
			++m_Evaluations;
			m_NextSource = (first + i + 1) % sources.size();
		}
		if (!due.empty())
		{
			m_SolvePending = Ref<SolveJob>::Create();
			m_SolvePending->Items = std::move(due);
			m_SolvePending->Scratch = std::move(m_Scratch);
			// shortcut: one job solves the batch serially on one worker; split it across workers when a
			// batch's solve time exceeds the acoustic update interval.
			JobSystem::Submit([job = m_SolvePending, snapshots = m_Snapshots, excluded = m_Excluded, portals = m_Portals,
				materials = m_Materials, settings = m_KernelSettings, listener]() mutable
			{
				LUX_PROFILE_SCOPE("Soundwave::SolveSources");
				const Soundwave::GeometryView geometry(snapshots[0]->Grid, &snapshots[1]->Grid, excluded);
				for (auto& item : job->Items)
				{
					try
					{
						item.Error = Soundwave::Solve(geometry, portals, materials, settings, listener, item.Position,
							item.Entity, job->Scratch, item.Result);
					}
					catch (const std::exception&)
					{
						item.Error = Soundwave::EvaluationError::NumericalFailure;
					}
				}
				job->Done.store(true, std::memory_order_release);
			});
			PollSolves(); // the inline job policy has already finished it
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
		if (ready)
			UpdateReverb(timestep, listener);
		return true;
	}

	void AudioAcoustics::PollSolves()
	{
		if (!m_SolvePending || !m_SolvePending->Done.load(std::memory_order_acquire))
			return;
		for (auto& item : m_SolvePending->Items)
		{
			const auto found = m_Sources.find(item.Entity);
			if (found == m_Sources.end())
				continue; // the source left while it was being solved
			auto& state = found->second;
			if (item.Error == Soundwave::EvaluationError::None)
			{
				std::swap(state.Result, item.Result);
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
				if (state.LastError != item.Error)
					LUX_CORE_ERROR_TAG("Audio", "Soundwave source {} solve failed (error {}); retaining its last valid result",
						static_cast<uint64_t>(item.Entity), static_cast<uint32_t>(item.Error));
			}
			state.LastError = item.Error;
		}
		m_Scratch = std::move(m_SolvePending->Scratch);
		m_SolvePending = nullptr;
	}

	void AudioAcoustics::PublishReverb(Soundwave::EvaluationError error, const Soundwave::ReverbResult& result)
	{
		if (error == Soundwave::EvaluationError::None)
		{
			m_Reverb = result;
			m_ReverbTarget = m_Reverb.Send;
			if (!m_ReverbValid)
				m_ReverbSend = m_ReverbTarget;
			m_ReverbValid = true;
			++m_ReverbMeasurements;
		}
		else if (m_ReverbError != error)
			LUX_CORE_ERROR_TAG("Audio", "Soundwave reverb measurement failed (error {}); retaining the last valid send",
				static_cast<uint32_t>(error));
		m_ReverbError = error;
	}

	void AudioAcoustics::UpdateReverb(float timestep, Soundwave::Position listener)
	{
		if (m_ReverbPending && m_ReverbPending->Done.load(std::memory_order_acquire))
		{
			PublishReverb(m_ReverbPending->Error, m_ReverbPending->Result);
			m_ReverbPending = nullptr;
		}
		if (m_FlattenPending && m_FlattenPending->Done.load(std::memory_order_acquire))
		{
			if (m_FlattenPending->Result)
				m_Flat = m_FlattenPending->Result;
			else
				LUX_CORE_ERROR_TAG("Audio", "Soundwave could not flatten acoustic geometry for the GPU; retaining the last upload");
			m_FlattenPending = nullptr;
		}
		if (m_ReverbOnGpu)
		{
			if (m_ReverbBackend->Poll(m_ReverbRays))
			{
				m_ReverbOnGpu = m_ReverbStalled = false;
				m_ReverbWaitUpdates = 0;
				Soundwave::ReverbResult result;
				PublishReverb(Soundwave::FinishReverb(m_ReverbRays, m_ReverbRequestSettings, result), result);
			}
			else if (++m_ReverbWaitUpdates == kReverbStallUpdates)
			{
				m_ReverbStalled = true;
				LUX_CORE_WARN_TAG("Audio", "Soundwave GPU reverb has not returned for {} updates; frames are probably not rendering", kReverbStallUpdates);
			}
		}
		m_ReverbSend += (m_ReverbTarget - m_ReverbSend) * (1.0f - std::exp(-timestep / kReverbSmoothingSeconds));
		m_SinceReverb += timestep;
		const auto moved = Soundwave::Position{ listener.X - m_ReverbListener.X, listener.Y - m_ReverbListener.Y, listener.Z - m_ReverbListener.Z };
		const bool stale = !m_ReverbAttempted || m_ReverbGeometry != m_GeometryGeneration || m_ReverbSettings != m_SettingsGeneration ||
			moved.X * moved.X + moved.Y * moved.Y + moved.Z * moved.Z > kReverbMoveMetres * kReverbMoveMetres;
		// Same cadence as source solves, so a swinging door cannot start a measurement every frame.
		if (m_ReverbPending || m_ReverbOnGpu || !stale || (m_ReverbAttempted && m_SinceReverb < 1.0f / m_Settings.UpdateRateHz))
			return;
		if (m_ReverbBackend && (!m_Flat || m_Flat->Generation != m_GeometryGeneration))
		{
			// Measure once this geometry generation is flattened; the previous send stays meanwhile.
			if (!m_FlattenPending)
			{
				m_FlattenPending = Ref<FlattenJob>::Create();
				JobSystem::Submit([job = m_FlattenPending, snapshots = m_Snapshots, excluded = m_Excluded, generation = m_GeometryGeneration]() mutable
				{
					LUX_PROFILE_SCOPE("Soundwave::FlattenGeometry");
					try
					{
						auto geometry = Ref<ReverbBackend::Geometry>::Create();
						geometry->Generation = generation;
						if (Soundwave::Flatten(Soundwave::GeometryView(snapshots[0]->Grid, &snapshots[1]->Grid, excluded), geometry->Flat))
							job->Result = geometry;
					}
					catch (const std::exception&)
					{
						job->Result = nullptr;
					}
					job->Done.store(true, std::memory_order_release);
				});
			}
			return;
		}
		m_SinceReverb = 0;
		m_ReverbAttempted = true;
		m_ReverbGeometry = m_GeometryGeneration;
		m_ReverbSettings = m_SettingsGeneration;
		m_ReverbListener = listener;
		if (m_ReverbBackend)
		{
			m_ReverbRequestSettings = m_KernelSettings;
			if (m_ReverbBackend->Dispatch(m_Flat, listener, m_KernelSettings, m_Materials))
			{
				m_ReverbOnGpu = true;
				return;
			}
			LUX_CORE_WARN_TAG("Audio", "Soundwave GPU reverb could not dispatch; measuring on the CPU for the rest of this session");
			m_ReverbBackend = nullptr;
		}
		m_ReverbPending = Ref<ReverbJob>::Create();
		// Only immutable snapshot Refs and copied values cross to the worker.
		JobSystem::Submit([job = m_ReverbPending, snapshots = m_Snapshots, excluded = m_Excluded, materials = m_Materials,
			settings = m_KernelSettings, listener]() mutable
		{
			LUX_PROFILE_SCOPE("Soundwave::MeasureReverb");
			try
			{
				std::vector<Soundwave::RayHit> hits;
				job->Error = Soundwave::EvaluateReverb(Soundwave::GeometryView(snapshots[0]->Grid, &snapshots[1]->Grid, excluded),
					materials, settings, listener, hits, job->Result);
			}
			catch (const std::exception&)
			{
				job->Error = Soundwave::EvaluationError::NumericalFailure;
			}
			job->Done.store(true, std::memory_order_release);
		});
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
		result.ReverbMeasurements = m_ReverbMeasurements;
		result.ReverbOnGpu = m_ReverbBackend != nullptr;
		result.ReverbStalled = m_ReverbStalled;
		result.SolvesPending = m_SolvePending != nullptr;
		result.StaticTriangles = m_Snapshots[0] ? m_Snapshots[0]->Grid.GetTriangleCount() : 0;
		result.MovingTriangles = m_Snapshots[1] ? m_Snapshots[1]->Grid.GetTriangleCount() : 0;
		result.StaticInertTriangles = m_Snapshots[0] ? m_Snapshots[0]->Grid.GetInertTriangleCount() : 0;
		result.MovingInertTriangles = m_Snapshots[1] ? m_Snapshots[1]->Grid.GetInertTriangleCount() : 0;
		result.StaticStoredTriangles = m_Snapshots[0] ? m_Snapshots[0]->StoredTriangles : 0;
		result.MovingStoredTriangles = m_Snapshots[1] ? m_Snapshots[1]->StoredTriangles : 0;
		result.StaticMeshInstances = m_Snapshots[0] ? m_Snapshots[0]->Grid.GetInstanceCount() : 0;
		result.MovingMeshInstances = m_Snapshots[1] ? m_Snapshots[1]->Grid.GetInstanceCount() : 0;
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
		m_ReverbPending = nullptr;
		m_Reverb = {};
		m_ReverbListener = {};
		m_ReverbError = Soundwave::EvaluationError::None;
		m_ReverbGeometry = m_ReverbSettings = m_ReverbMeasurements = 0;
		m_ReverbSend = m_ReverbTarget = m_SinceReverb = 0;
		m_ReverbValid = m_ReverbAttempted = false;
		m_FlattenPending = nullptr;
		m_Flat = nullptr;
		m_SolvePending = nullptr;
		m_ReverbOnGpu = m_ReverbStalled = false;
		m_ReverbWaitUpdates = 0;
		m_UpdateIndex = m_GeometryGeneration = m_Evaluations = m_SolveFailures = 0;
	}
}
