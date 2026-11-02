// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#pragma once

#include "AcousticMaterial.h"
#include "AudioOcclusionSettings.h"
#include "Lux/Core/Ref.h"
#include "Lux/Core/UUID.h"
#include <soundwave/Flatten.h>
#include <soundwave/Reverb.h>
#include <array>
#include <atomic>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Lux
{
	// Scene-owned acoustics adapter. Main owns scheduling and publication; geometry builds, source
	// solves and reverb run through JobSystem (or the GPU) with captured values/Refs only, never
	// Scene/ECS pointers, and main never waits for them.
	class AudioAcoustics
	{
	public:
		// Built before publication, then immutable. Scene mesh templates derive from this
		// owner so a published snapshot retains the shared local Grid through every read.
		struct MeshGeometry : RefCounted
		{
			Soundwave::Grid Grid;
		};
		struct MeshInstance
		{
			Ref<MeshGeometry> Geometry;
			// Geometry is rebound to the retained owner's Grid by the preparation job.
			Soundwave::GeometryInstance Description;
		};
		struct GeometryInput
		{
			std::vector<Soundwave::Triangle> Triangles; // compatibility/reference world triangles
			std::vector<MeshInstance> Meshes;
			std::vector<Soundwave::PortalBox> Portals;
		};
		using Builder = std::function<bool(GeometryInput&, std::string& error)>;
		struct Source
		{
			UUID Entity = 0;
			Soundwave::Position Position;
		};
		struct Diagnostics
		{
			uint64_t GeometryGeneration = 0, Evaluations = 0, SolveFailures = 0, ReverbMeasurements = 0;
			uint32_t StaticTriangles = 0, MovingTriangles = 0, ExcludedInstances = 0, PendingLayers = 0;
			uint32_t StaticInertTriangles = 0, MovingInertTriangles = 0;
			uint32_t StaticStoredTriangles = 0, MovingStoredTriangles = 0, StaticMeshInstances = 0, MovingMeshInstances = 0;
			bool Ready = false, GeometryFailed = false;
			bool ReverbOnGpu = false, ReverbStalled = false, SolvesPending = false;
		};
		// Where the listener reverb's rays run. Without one (headless tests, no GPU) a JobSystem
		// job measures on the CPU; with one, FinishReverb reduces the returned rays on main.
		struct ReverbBackend : RefCounted
		{
			struct Geometry : RefCounted
			{
				Soundwave::FlatGeometry Flat;
				uint64_t Generation = 0;
			};
			// Main thread. Records one measurement over geometry; false when it cannot.
			virtual bool Dispatch(const Ref<Geometry>& geometry, Soundwave::Position listener,
				const Soundwave::Settings& settings, const Soundwave::MaterialTable& materials) = 0;
			// Main thread, never waits: true once that measurement's rays reached the CPU.
			virtual bool Poll(std::vector<Soundwave::ReverbRay>& rays) = 0;
		};
		// Kept across Clear(); the owner replaces or drops it.
		void SetReverbBackend(Ref<ReverbBackend> backend) { m_ReverbBackend = std::move(backend); }

		AudioAcoustics();
		bool Configure(const AcousticMaterialSettings& materials, const AudioOcclusionSettings& settings);
		// Revisions increase globally. Empty builders reuse that layer; the first request
		// must supply both. Both snapshots publish together so an instance changing layers
		// cannot exist twice. Coalescing retains the latest builder for each dirty layer.
		[[nodiscard]] bool RequestGeometry(uint64_t revision, Builder fixed, Builder moving);
		void PollGeometry();
		// Current live UUIDs mask deleted/disabled geometry even before a rebuild finishes.
		[[nodiscard]] bool SetActiveInstances(std::span<const uint64_t> instances);
		// Selects due sources and solves them on a worker. Results publish in PollSolves, which this
		// calls; with the inline job policy that is the same update, otherwise a later one.
		[[nodiscard]] bool Update(float timestep, Soundwave::Position listener, std::span<const Source> sources);
		// Main thread: publishes a finished solve batch, if any. Never waits.
		void PollSolves();
		float GetGainLF(UUID source) const;
		float GetGainHF(UUID source) const;
		const Soundwave::SolveResult* GetResult(UUID source) const;
		// Smoothed listener reverb send, 0 until the first measurement completes.
		float GetReverbSend() const { return m_ReverbSend; }
		// The last completed measurement; nullptr before one exists.
		const Soundwave::ReverbResult* GetReverb() const { return m_ReverbValid ? &m_Reverb : nullptr; }
		// Validation builds (not Dist): the CPU reference re-measures each GPU result on a worker.
		// Null until one has; first = GPU, second = CPU, same listener and geometry.
		const std::pair<Soundwave::ReverbResult, Soundwave::ReverbResult>* GetReverbCheck() const { return m_ReverbCheckValid ? &m_ReverbCheck : nullptr; }
		Diagnostics GetDiagnostics() const;
		bool IsRaycastComparisonEnabled() const { return m_Settings.CompareRaycast; }
		void Clear();

	private:
		struct Snapshot : RefCounted
		{
			std::vector<Ref<MeshGeometry>> Meshes;
			Soundwave::Grid Raw;
			Soundwave::InstancedGrid Grid;
			uint32_t StoredTriangles = 0;
			std::vector<Soundwave::PortalBox> Portals;
			std::vector<uint64_t> Instances;
		};
		struct Job : RefCounted
		{
			std::atomic<bool> Done{ false };
			std::array<Ref<Snapshot>, 2> Result;
			std::string Error;
			uint64_t Revision = 0;
			uint32_t ChangedLayers = 0, FailedLayers = 0;
			std::array<uint64_t, 2> LayerRevisions{};
		};
		struct SourceState
		{
			Soundwave::SolveResult Result;
			float GainLF = 1, GainHF = 1, TargetLF = 1, TargetHF = 1, SinceEvaluation = 0;
			uint64_t SeenUpdate = 0, GeometryGeneration = 0, SettingsGeneration = 0;
			Soundwave::EvaluationError LastError = Soundwave::EvaluationError::None;
			bool Evaluated = false, Attempted = false;
		};
		struct ReverbJob : RefCounted
		{
			std::atomic<bool> Done{ false };
			Soundwave::ReverbResult Result;
			Soundwave::EvaluationError Error = Soundwave::EvaluationError::None;
		};
		struct SolveJob : RefCounted
		{
			struct Item
			{
				UUID Entity = 0;
				Soundwave::Position Position;
				Soundwave::SolveResult Result;
				Soundwave::EvaluationError Error = Soundwave::EvaluationError::None;
			};
			std::atomic<bool> Done{ false };
			std::vector<Item> Items;
			Soundwave::SolveScratch Scratch;
		};
		struct FlattenJob : RefCounted
		{
			std::atomic<bool> Done{ false };
			Ref<ReverbBackend::Geometry> Result;
		};
		void UpdateReverb(float timestep, Soundwave::Position listener);
		void PublishReverb(Soundwave::EvaluationError error, const Soundwave::ReverbResult& result);
		void RefreshGeometryView();
		bool CanPrepare() const;
		std::array<Ref<Snapshot>, 2> m_Snapshots;
		std::array<Builder, 2> m_Builders;
		std::array<uint64_t, 2> m_LayerRevisions{};
		uint32_t m_DirtyLayers = 0, m_FailedLayers = 0;
		std::array<uint64_t, 2> m_FailureLayerRevisions{};
		Ref<Job> m_Pending;
		uint64_t m_RequestedRevision = 0, m_FailedRevision = 0, m_LastAttemptedRevision = 0;
		Soundwave::MaterialTable m_Materials;
		AudioOcclusionSettings m_Settings;
		Soundwave::Settings m_KernelSettings;
		Soundwave::SolveScratch m_Scratch;
		std::vector<uint64_t> m_ActiveInstances, m_Excluded, m_SourceIDs;
		std::vector<Soundwave::PortalBox> m_Portals;
		std::unordered_map<UUID, SourceState> m_Sources;
		uint64_t m_UpdateIndex = 0, m_GeometryGeneration = 0, m_SettingsGeneration = 0;
		uint64_t m_Evaluations = 0, m_SolveFailures = 0;
		size_t m_NextSource = 0;
		Ref<SolveJob> m_SolvePending;
		Ref<ReverbJob> m_ReverbPending;
		Ref<ReverbBackend> m_ReverbBackend;
		Ref<ReverbJob> m_ReverbCheckPending;
		std::pair<Soundwave::ReverbResult, Soundwave::ReverbResult> m_ReverbCheck;
		Soundwave::ReverbResult m_ReverbCheckGpu;
		uint64_t m_ReverbCheckGeometry = 0;
		bool m_ReverbCheckValid = false;
		Ref<FlattenJob> m_FlattenPending;
		Ref<ReverbBackend::Geometry> m_Flat;
		std::vector<Soundwave::ReverbRay> m_ReverbRays;
		Soundwave::Settings m_ReverbRequestSettings;
		uint32_t m_ReverbWaitUpdates = 0;
		bool m_ReverbOnGpu = false, m_ReverbStalled = false;
		Soundwave::ReverbResult m_Reverb;
		Soundwave::Position m_ReverbListener;
		Soundwave::EvaluationError m_ReverbError = Soundwave::EvaluationError::None;
		uint64_t m_ReverbGeometry = 0, m_ReverbSettings = 0, m_ReverbMeasurements = 0;
		float m_ReverbSend = 0, m_ReverbTarget = 0, m_SinceReverb = 0;
		bool m_ReverbValid = false, m_ReverbAttempted = false;
		bool m_HasActiveInstances = false;
	};
}
