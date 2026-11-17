// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#include "lpch.h"
#include <bit>
#include "AudioAcoustics.h"
#include "Lux/Core/JobSystem.h"
#include "Lux/Debug/Profiler.h"
#include <algorithm>
#include <cmath>
#include <limits>

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
		constexpr float kReflattenSeconds = 1.0f;
		// Below Vulkan's guaranteed 128 MiB storage-buffer range, including all upload arrays.
		constexpr size_t kFlatUploadBudget = 64u * 1024u * 1024u;
		constexpr size_t kMaximumVisibilitySegments = 262144;
		constexpr size_t kMaximumVisibilityNodesPerSource = 256;
		const char* FlattenErrorName(Soundwave::FlatError error)
		{
			switch (error)
			{
				case Soundwave::FlatError::InvalidGeometry: return "invalid geometry";
				case Soundwave::FlatError::BudgetExceeded: return "upload budget exceeded";
				case Soundwave::FlatError::AllocationFailed: return "allocation failed";
				case Soundwave::FlatError::None: return "unexpected empty result";
			}
			return "unknown error";
		}
		bool Finite(Soundwave::Position point)
		{
			return std::isfinite(point.X) && std::isfinite(point.Y) && std::isfinite(point.Z);
		}
	}

	AudioAcoustics::AudioAcoustics()
		: m_Materials(Soundwave::DefaultMaterials()), m_KernelSettings(m_Settings.GetKernelSettings()),
		  m_EchogramSettings(m_Settings.GetEchogramSettings())
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
		m_EchogramSettings = settings.GetEchogramSettings();
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

	bool AudioAcoustics::Update(float timestep, Soundwave::Position listener, std::span<const Source> sources,
		std::span<const ZoneProbe> zones)
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
		m_ZoneIDs.clear();
		for (const auto& zone : zones)
		{
			if (zone.Entity == 0 || !Finite(zone.Position))
			{
				LUX_CORE_ERROR_TAG("Audio", "Invalid Soundwave zone UUID or probe position ({})", static_cast<uint64_t>(zone.Entity));
				return false;
			}
			m_ZoneIDs.push_back(zone.Entity);
		}
		std::sort(m_ZoneIDs.begin(), m_ZoneIDs.end());
		if (std::adjacent_find(m_ZoneIDs.begin(), m_ZoneIDs.end()) != m_ZoneIDs.end())
		{
			LUX_CORE_ERROR_TAG("Audio", "Duplicate zone UUID in Soundwave update");
			return false;
		}
		++m_UpdateIndex;
		for (const auto& zone : zones)
		{
			auto& state = m_Zones[zone.Entity];
			state.SeenUpdate = m_UpdateIndex;
			if (state.Position.X != zone.Position.X || state.Position.Y != zone.Position.Y || state.Position.Z != zone.Position.Z)
			{
				state.Valid = false;
				state.Attempted = false;
			}
			state.Position = zone.Position;
		}
		PollZone();
		for (const auto& source : sources)
		{
			auto& state = m_Sources[source.Entity];
			state.SeenUpdate = m_UpdateIndex;
			state.SinceEvaluation += timestep;
		}
		const float interval = 1.0f / m_Settings.UpdateRateHz;
		const float smoothing = 1.0f - std::exp(-timestep / kSmoothingSeconds);
		const bool ready = m_Snapshots[0] && m_Snapshots[1];
		if (ready && !m_ZonePending && !zones.empty())
		{
			const size_t firstZone = m_NextZone % zones.size();
			for (size_t i = 0; i < zones.size(); ++i)
			{
				const auto& zone = zones[(firstZone + i) % zones.size()];
				auto& state = m_Zones[zone.Entity];
				if (state.Attempted && state.GeometryGeneration == m_GeometryGeneration &&
					state.SettingsGeneration == m_SettingsGeneration)
					continue;
				m_ZonePending = Ref<ZoneJob>::Create();
				m_ZonePending->Entity = zone.Entity;
				m_ZonePending->Position = zone.Position;
				m_ZonePending->GeometryGeneration = m_GeometryGeneration;
				m_ZonePending->SettingsGeneration = m_SettingsGeneration;
				state.Attempted = true;
				state.Valid = false;
				state.GeometryGeneration = m_GeometryGeneration;
				state.SettingsGeneration = m_SettingsGeneration;
				m_NextZone = (firstZone + i + 1) % zones.size();
				JobSystem::Submit([job = m_ZonePending, snapshots = m_Snapshots, excluded = m_Excluded,
					materials = m_Materials, settings = m_KernelSettings, echogramSettings = m_EchogramSettings]() mutable
				{
					LUX_PROFILE_SCOPE("Soundwave::MeasureZone");
					try
					{
						std::vector<Soundwave::RayHit> hits;
						job->Error = Soundwave::EvaluateZoneReverb(
							Soundwave::GeometryView(snapshots[0]->Grid, &snapshots[1]->Grid, excluded),
							materials, settings, echogramSettings, job->Position, hits, job->Echogram, job->Reverb);
					}
					catch (const std::exception&)
					{
						job->Error = Soundwave::EvaluationError::NumericalFailure;
					}
					job->Done.store(true, std::memory_order_release);
				});
				PollZone(); // inline job policy
				break;
			}
		}
		std::erase_if(m_Zones, [&](const auto& entry) { return entry.second.SeenUpdate != m_UpdateIndex; });
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
			auto job = m_SolvePending;
			job->Items = std::move(due);
			job->Scratch = std::move(m_Scratch);
			job->Segments = std::move(m_VisibilitySegmentScratch);
			job->Segments.clear();
			job->Snapshots = m_Snapshots;
			job->Flat = m_Flat;
			job->Backend = m_ReverbBackend;
			job->Excluded = m_Excluded;
			job->Portals = m_Portals;
			job->Materials = m_Materials;
			job->Settings = m_KernelSettings;
			job->Listener = listener;
			const bool gpuReady = m_ReverbBackend && m_ReverbBackend->SupportsVisibility() && !m_SourceVisibilityGpuFailed &&
				m_Flat && m_Flat->Generation == m_GeometryGeneration &&
				job->Settings.OpeningNodeBudget <= kMaximumVisibilityNodesPerSource && job->Portals.size() <= 4096;
			if (gpuReady)
			{
				job->Current = SolveJob::Phase::Preparing;
				JobSystem::Submit([job]() mutable
				{
					LUX_PROFILE_SCOPE("Soundwave::PrepareVisibility");
					try
					{
						const Soundwave::GeometryView geometry(job->Snapshots[0]->Grid, &job->Snapshots[1]->Grid, job->Excluded);
						for (const auto& portal : job->Portals)
						{
							Soundwave::FlatPortal flat{};
							std::copy(portal.WorldToBox.begin(), portal.WorldToBox.end(), flat.WorldToBox);
							flat.Instance[0] = static_cast<uint32_t>(portal.Instance);
							flat.Instance[1] = static_cast<uint32_t>(portal.Instance >> 32);
							flat.Active = geometry.IsInstanceActive(portal.Instance) ? 1u : 0u;
							job->FlatPortals.push_back(flat);
						}
						std::vector<Soundwave::Position> points;
						for (auto& item : job->Items)
						{
							item.Error = Soundwave::PrepareCandidates(geometry, job->Portals, job->Materials, job->Settings,
								job->Listener, item.Position, item.Entity, job->Scratch, item.Prepared);
							if (item.Error != Soundwave::EvaluationError::None)
								continue;
							item.Error = Soundwave::PrepareReflections(geometry, job->Materials, job->Settings, item.Prepared,
								job->Scratch, item.Reflections);
							if (item.Error != Soundwave::EvaluationError::None)
								continue;

							const size_t width = item.Prepared.Openings.size() + 2;
							size_t count = width * width;
							for (const auto& reflection : item.Reflections.Paths)
								count += reflection.Path.PointCount - 1;
							if (width > kMaximumVisibilityNodesPerSource + 2 || count > kMaximumVisibilitySegments - job->Segments.size())
							{
								job->GpuSkipped = true;
								break;
							}
							item.VisibilityOffset = job->Segments.size();
							item.VisibilityCount = count;
							const auto source = static_cast<uint64_t>(item.Entity);
							const auto append = [&](Soundwave::Position from, Soundwave::Position to, float margin)
							{
								Soundwave::FlatVisibilitySegment segment{};
								segment.From[0] = from.X;
								segment.From[1] = from.Y;
								segment.From[2] = from.Z;
								segment.To[0] = to.X;
								segment.To[1] = to.Y;
								segment.To[2] = to.Z;
								segment.IgnoredInstance[0] = static_cast<uint32_t>(source);
								segment.IgnoredInstance[1] = static_cast<uint32_t>(source >> 32);
								segment.MarginBits = std::bit_cast<uint32_t>(margin);
								job->Segments.push_back(segment);
							};
							points.clear();
							points.reserve(width);
							points.push_back(job->Listener);
							for (const auto& opening : item.Prepared.Openings)
								points.push_back(opening.Point);
							points.push_back(item.Position);
							for (const auto& from : points)
								for (const auto& to : points)
									append(from, to, 0.0f);
							for (const auto& reflection : item.Reflections.Paths)
							{
								const auto& path = reflection.Path;
								for (uint32_t leg = 1; leg < path.PointCount; ++leg)
									append(path.Points[leg - 1], path.Points[leg], Soundwave::kReflectionEndpointMargin);
							}
						}
					}
					catch (const std::exception&)
					{
						job->GpuFailed = true;
					}
					job->Done.store(true, std::memory_order_release);
				});
			}
			else
			{
				m_SourceVisibilityOnGpu = false;
				SubmitCpuSolves(job);
			}
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
			it->second.FocusSend += (it->second.TargetFocusSend - it->second.FocusSend) * smoothing;
			++it;
		}
		if (ready)
			UpdateReverb(timestep, listener);
		return true;
	}

	void AudioAcoustics::PollZone()
	{
		if (!m_ZonePending || !m_ZonePending->Done.load(std::memory_order_acquire))
			return;
		const auto found = m_Zones.find(m_ZonePending->Entity);
		if (found != m_Zones.end())
		{
			auto& state = found->second;
			const auto& job = *m_ZonePending;
			if (state.Position.X == job.Position.X && state.Position.Y == job.Position.Y && state.Position.Z == job.Position.Z &&
				job.GeometryGeneration == m_GeometryGeneration && job.SettingsGeneration == m_SettingsGeneration)
			{
				if (job.Error == Soundwave::EvaluationError::None)
				{
					state.Echogram = job.Echogram;
					state.Reverb = job.Reverb;
					state.Valid = true;
					state.LastError = Soundwave::EvaluationError::None;
				}
				else
				{
					if (state.LastError != job.Error)
						LUX_CORE_ERROR_TAG("Audio", "Soundwave zone {} measurement failed (error {})",
							static_cast<uint64_t>(job.Entity), static_cast<uint32_t>(job.Error));
					state.LastError = job.Error;
				}
			}
		}
		m_ZonePending = nullptr;
	}

	void AudioAcoustics::SubmitCpuSolves(Ref<SolveJob> job)
	{
		job->Current = SolveJob::Phase::Cpu;
		job->Done.store(false, std::memory_order_relaxed);
		JobSystem::Submit([job]() mutable
		{
			LUX_PROFILE_SCOPE("Soundwave::SolveSources");
			const Soundwave::GeometryView geometry(job->Snapshots[0]->Grid, &job->Snapshots[1]->Grid, job->Excluded);
			for (auto& item : job->Items)
			{
				try
				{
					item.Error = Soundwave::Solve(geometry, job->Portals, job->Materials, job->Settings, job->Listener,
						item.Position, item.Entity, job->Scratch, item.Result);
				}
				catch (const std::exception&)
				{
					item.Error = Soundwave::EvaluationError::NumericalFailure;
				}
			}
			job->Done.store(true, std::memory_order_release);
		});
	}

	void AudioAcoustics::SubmitVisibilityReduction(Ref<SolveJob> job)
	{
		job->Current = SolveJob::Phase::Reducing;
		job->Done.store(false, std::memory_order_relaxed);
		JobSystem::Submit([job]() mutable
		{
			LUX_PROFILE_SCOPE("Soundwave::ReduceVisibility");
			const Soundwave::GeometryView geometry(job->Snapshots[0]->Grid, &job->Snapshots[1]->Grid, job->Excluded);
			for (auto& item : job->Items)
			{
				if (item.Error != Soundwave::EvaluationError::None)
					continue;
				try
				{
					const auto visibility = std::span<const uint32_t>(job->Visibility).subspan(item.VisibilityOffset, item.VisibilityCount);
					const size_t width = item.Prepared.Openings.size() + 2;
					const size_t openingResults = width * width;
					item.Error = Soundwave::ReduceOpeningVisibility(geometry, job->Portals, job->Materials, job->Settings,
						job->Listener, item.Position, item.Entity, item.Prepared, visibility.first(openingResults), job->Scratch,
						item.Result, &item.Reflections, visibility.subspan(openingResults));
					if (item.Error != Soundwave::EvaluationError::None)
					{
						job->GpuFailed = true;
						item.Error = Soundwave::Solve(geometry, job->Portals, job->Materials, job->Settings, job->Listener,
							item.Position, item.Entity, job->Scratch, item.Result);
					}
					#if defined(LUX_ENABLE_ASSERTS)
					else
					{
						Soundwave::SolveResult reference;
						const auto referenceError = Soundwave::Solve(geometry, job->Portals, job->Materials, job->Settings,
							job->Listener, item.Position, item.Entity, job->Scratch, reference);
						if (referenceError != Soundwave::EvaluationError::None)
						{
							job->GpuFailed = true;
							item.Error = referenceError;
						}
						else
						{
							const auto gap = [](float a, float b)
							{
								return a == b ? 0.0f : std::abs(20.0f * std::log10(std::max(a, 1e-6f) / std::max(b, 1e-6f)));
							};
							job->WorstGpuGapDb = std::max({ job->WorstGpuGapDb,
								gap(item.Result.OcclusionGainLF, reference.OcclusionGainLF),
								gap(item.Result.OcclusionGainHF, reference.OcclusionGainHF) });
							if (job->WorstGpuGapDb > 0.5f)
							{
								job->GpuFailed = true;
								item.Result = std::move(reference);
							}
						}
					}
					#endif
				}
				catch (const std::exception&)
				{
					job->GpuFailed = true;
					item.Error = Soundwave::EvaluationError::NumericalFailure;
				}
			}
			job->Done.store(true, std::memory_order_release);
		});
	}

	void AudioAcoustics::PollSolves()
	{
		if (!m_SolvePending)
			return;
		auto job = m_SolvePending;
		if (job->Current == SolveJob::Phase::GpuPending)
		{
			const auto poll = job->Backend->PollVisibility(job->Visibility);
			if (poll == ReverbBackend::PollResult::Pending)
			{
				if (++m_VisibilityWaitUpdates == kReverbStallUpdates)
					LUX_CORE_WARN_TAG("Audio", "Soundwave GPU source visibility has not returned for {} updates; frames are probably not rendering", kReverbStallUpdates);
				return;
			}
			m_VisibilityWaitUpdates = 0;
			if (poll == ReverbBackend::PollResult::Failed || job->Visibility.size() != job->Segments.size())
			{
				m_SourceVisibilityGpuFailed = true;
				m_SourceVisibilityOnGpu = false;
				LUX_CORE_ERROR_TAG("Audio", "Soundwave GPU visibility readback failed; source solves use the CPU for this session");
				SubmitCpuSolves(job);
				return;
			}
			SubmitVisibilityReduction(job);
			return;
		}
		if (!job->Done.load(std::memory_order_acquire))
			return;
		if (job->Current == SolveJob::Phase::Preparing)
		{
			if (job->GpuSkipped || job->Segments.empty())
			{
				m_SourceVisibilityOnGpu = false;
				if (job->GpuSkipped && !m_SourceVisibilityLimitReported)
				{
					LUX_CORE_WARN_TAG("Audio", "Soundwave GPU visibility batch exceeds the {}-segment limit; using the CPU reference", kMaximumVisibilitySegments);
					m_SourceVisibilityLimitReported = true;
				}
				SubmitCpuSolves(job);
				return;
			}
			if (job->GpuFailed || !job->Backend ||
				!job->Backend->DispatchVisibility(job->Flat, job->Segments, job->FlatPortals))
			{
				m_SourceVisibilityGpuFailed = true;
				m_SourceVisibilityOnGpu = false;
				LUX_CORE_ERROR_TAG("Audio", "Soundwave GPU visibility preparation or dispatch failed; source solves use the CPU for this session");
				SubmitCpuSolves(job);
				return;
			}
			job->Current = SolveJob::Phase::GpuPending;
			m_SourceVisibilityOnGpu = true;
			m_VisibilityWaitUpdates = 0;
			return;
		}
		if (job->GpuFailed)
		{
			m_SourceVisibilityGpuFailed = true;
			m_SourceVisibilityOnGpu = false;
			LUX_CORE_ERROR_TAG("Audio", "Soundwave GPU source reduction failed or exceeded the 0.5 dB reference tolerance (worst {:.3f} dB); source solves use the CPU for this session",
				job->WorstGpuGapDb);
		}
		else if (job->Current == SolveJob::Phase::Reducing)
			++m_SourceVisibilityBatches;
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
				const float addedEnergy = state.Result.AddedFocusEnergy;
				state.TargetFocusSend = addedEnergy / (1.0f + addedEnergy);
				if (!state.Evaluated)
				{
					state.GainLF = state.TargetLF;
					state.GainHF = state.TargetHF;
					state.FocusSend = state.TargetFocusSend;
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
		m_VisibilitySegmentScratch = std::move(m_SolvePending->Segments);
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
			{
				m_Flat = m_FlattenPending->Result;
				m_ReverbGeometry = 0; // measure again on the fresh upload
			}
			else
			{
				LUX_CORE_ERROR_TAG("Audio", "Soundwave GPU geometry flatten failed ({}); measuring reverb on the CPU for the rest of this session",
					FlattenErrorName(m_FlattenPending->Error));
				m_ReverbBackend = nullptr;
				m_ReverbOnGpu = m_ReverbStalled = false;
				m_ReverbGpuFailed = true;
				m_ReverbAttempted = false;
				m_ReverbWaitUpdates = 0;
			}
			m_FlattenPending = nullptr;
		}
		if (m_ReverbCheckPending && m_ReverbCheckPending->Done.load(std::memory_order_acquire))
		{
			if (m_ReverbCheckPending->Error == Soundwave::EvaluationError::None && m_ReverbCheckGeometry == m_GeometryGeneration)
			{
				m_ReverbCheck = { m_ReverbCheckGpu, m_ReverbCheckPending->Result };
				m_ReverbCheckValid = true;
				const auto gap = [](float a, float b) { return a == b ? 0.0f : std::abs(20.0f * std::log10(std::max(a, 1e-6f) / std::max(b, 1e-6f))); };
				const float sendGap = gap(m_ReverbCheck.first.Send, m_ReverbCheck.second.Send);
				const float decayGap = gap(m_ReverbCheck.first.DecaySeconds, m_ReverbCheck.second.DecaySeconds);
				if (sendGap > 0.5f || decayGap > 0.5f)
					LUX_CORE_ERROR_TAG("Audio", "Soundwave GPU reverb differs from the CPU reference by {:.3f} dB (send) / {:.3f} dB (decay)", sendGap, decayGap);
			}
			m_ReverbCheckPending = nullptr;
		}
		if (m_ReverbOnGpu)
		{
			const auto poll = m_ReverbBackend->Poll(m_ReverbRays);
			if (poll == ReverbBackend::PollResult::Failed)
			{
				LUX_CORE_ERROR_TAG("Audio", "Soundwave GPU reverb readback failed; measuring on the CPU for the rest of this session");
				m_ReverbBackend = nullptr;
				m_ReverbOnGpu = m_ReverbStalled = false;
				m_ReverbGpuFailed = true;
				m_ReverbAttempted = false;
				m_ReverbWaitUpdates = 0;
			}
			else if (poll == ReverbBackend::PollResult::Ready)
			{
				m_ReverbOnGpu = m_ReverbStalled = false;
				m_ReverbWaitUpdates = 0;
				Soundwave::ReverbResult result;
				const auto error = Soundwave::FinishReverb(m_ReverbRays, m_ReverbRequestSettings, result);
				PublishReverb(error, result);
#ifdef LUX_DEBUG
				// Decision 10's validation: the same measurement on the CPU reference, off main.
				if (error == Soundwave::EvaluationError::None && !m_ReverbCheckPending && m_Flat->Generation == m_GeometryGeneration)
				{
					m_ReverbCheckGpu = result;
					m_ReverbCheckGeometry = m_GeometryGeneration;
					m_ReverbCheckPending = Ref<ReverbJob>::Create();
					JobSystem::Submit([job = m_ReverbCheckPending, snapshots = m_Snapshots, excluded = m_Excluded, materials = m_Materials,
						settings = m_ReverbRequestSettings, listener = m_ReverbListener]() mutable
					{
						LUX_PROFILE_SCOPE("Soundwave::CheckReverb");
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
#endif
			}
			else if (poll == ReverbBackend::PollResult::Pending && ++m_ReverbWaitUpdates == kReverbStallUpdates)
			{
				m_ReverbStalled = true;
				LUX_CORE_WARN_TAG("Audio", "Soundwave GPU reverb has not returned for {} updates; frames are probably not rendering", kReverbStallUpdates);
			}
		}
		m_ReverbSend += (m_ReverbTarget - m_ReverbSend) * (1.0f - std::exp(-timestep / kReverbSmoothingSeconds));
		m_SinceReverb += timestep;
		m_SinceFlatten += timestep;
		// Reverb is a room average: changed geometry is re-flattened and re-uploaded at most once a
		// second, or a moving door would re-upload the whole scene at the acoustic rate. The GPU
		// measures on the last upload meanwhile.
		if (m_ReverbBackend && !m_FlattenPending &&
			(!m_Flat || (m_Flat->Generation != m_GeometryGeneration && m_SinceFlatten >= kReflattenSeconds)))
		{
			m_SinceFlatten = 0;
			m_FlattenPending = Ref<FlattenJob>::Create();
			JobSystem::Submit([job = m_FlattenPending, snapshots = m_Snapshots, excluded = m_Excluded, generation = m_GeometryGeneration]() mutable
			{
				LUX_PROFILE_SCOPE("Soundwave::FlattenGeometry");
				try
				{
					auto geometry = Ref<ReverbBackend::Geometry>::Create();
					geometry->Generation = generation;
					job->Error = Soundwave::Flatten(Soundwave::GeometryView(snapshots[0]->Grid, &snapshots[1]->Grid, excluded),
						geometry->Flat, kFlatUploadBudget);
					if (job->Error == Soundwave::FlatError::None)
						job->Result = geometry;
				}
				catch (const std::exception&)
				{
					job->Result = nullptr;
					job->Error = Soundwave::FlatError::AllocationFailed;
				}
				job->Done.store(true, std::memory_order_release);
			});
		}
		const auto moved = Soundwave::Position{ listener.X - m_ReverbListener.X, listener.Y - m_ReverbListener.Y, listener.Z - m_ReverbListener.Z };
		const bool stale = !m_ReverbAttempted || m_ReverbGeometry != m_GeometryGeneration || m_ReverbSettings != m_SettingsGeneration ||
			moved.X * moved.X + moved.Y * moved.Y + moved.Z * moved.Z > kReverbMoveMetres * kReverbMoveMetres;
		// Same cadence as source solves, so a swinging door cannot start a measurement every frame.
		if (m_ReverbPending || m_ReverbOnGpu || !stale || (m_ReverbAttempted && m_SinceReverb < 1.0f / m_Settings.UpdateRateHz))
			return;
		if (m_ReverbBackend && !m_Flat)
			return; // the first upload is still being prepared
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
			m_ReverbGpuFailed = true;
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
	float AudioAcoustics::GetFocusedReflectionSend(UUID source) const
	{
		const auto found = m_Sources.find(source);
		return found != m_Sources.end() ? found->second.FocusSend : 0;
	}
	const Soundwave::SolveResult* AudioAcoustics::GetResult(UUID source) const
	{
		const auto found = m_Sources.find(source);
		return found != m_Sources.end() && found->second.Evaluated ? &found->second.Result : nullptr;
	}

	const Soundwave::ReverbResult* AudioAcoustics::GetZoneReverb(UUID zone) const
	{
		const auto found = m_Zones.find(zone);
		return found != m_Zones.end() && found->second.Valid ? &found->second.Reverb : nullptr;
	}

	const Soundwave::Echogram* AudioAcoustics::GetZoneEchogram(UUID zone) const
	{
		const auto found = m_Zones.find(zone);
		return found != m_Zones.end() && found->second.Valid ? &found->second.Echogram : nullptr;
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
		result.ReverbGpuFailed = m_ReverbGpuFailed;
		result.SourceVisibilityOnGpu = m_SourceVisibilityOnGpu;
		result.SourceVisibilityGpuFailed = m_SourceVisibilityGpuFailed;
		result.SourceVisibilityBatches = m_SourceVisibilityBatches;
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
		m_Zones.clear();
		m_ZoneIDs.clear();
		m_ZonePending = nullptr;
		m_NextZone = 0;
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
		m_VisibilitySegmentScratch.clear();
		m_ReverbOnGpu = m_ReverbStalled = false;
		m_ReverbGpuFailed = false;
		m_SourceVisibilityOnGpu = m_SourceVisibilityGpuFailed = false;
		m_SourceVisibilityLimitReported = false;
		m_SourceVisibilityBatches = 0;
		m_VisibilityWaitUpdates = 0;
		m_ReverbWaitUpdates = 0;
		m_ReverbCheckPending = nullptr;
		m_ReverbCheckValid = false;
		m_UpdateIndex = m_GeometryGeneration = m_Evaluations = m_SolveFailures = 0;
	}
}
