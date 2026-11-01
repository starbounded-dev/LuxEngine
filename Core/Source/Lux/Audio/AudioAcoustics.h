// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#pragma once

#include "AcousticMaterial.h"
#include "AudioOcclusionSettings.h"
#include "Lux/Core/Ref.h"
#include "Lux/Core/UUID.h"
#include <soundwave/Solve.h>
#include <array>
#include <atomic>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Lux
{
	// Scene-owned CPU reference adapter. Main owns scheduling and publication; builders
	// run through JobSystem with captured values/Refs only, never Scene/ECS pointers.
	class AudioAcoustics
	{
	public:
		struct GeometryInput
		{
			std::vector<Soundwave::Triangle> Triangles;
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
			uint64_t GeometryGeneration = 0, Evaluations = 0, SolveFailures = 0;
			uint32_t StaticTriangles = 0, MovingTriangles = 0, ExcludedInstances = 0, PendingLayers = 0;
			bool Ready = false, GeometryFailed = false;
		};

		AudioAcoustics();
		bool Configure(const AcousticMaterialSettings& materials, const AudioOcclusionSettings& settings);
		// Revisions increase globally. Empty builders reuse that layer; the first request
		// must supply both. Both snapshots publish together so an instance changing layers
		// cannot exist twice. Coalescing retains the latest builder for each dirty layer.
		[[nodiscard]] bool RequestGeometry(uint64_t revision, Builder fixed, Builder moving);
		void PollGeometry();
		// Current live UUIDs mask deleted/disabled geometry even before a rebuild finishes.
		[[nodiscard]] bool SetActiveInstances(std::span<const uint64_t> instances);
		[[nodiscard]] bool Update(float timestep, Soundwave::Position listener, std::span<const Source> sources);
		float GetGainLF(UUID source) const;
		float GetGainHF(UUID source) const;
		const Soundwave::SolveResult* GetResult(UUID source) const;
		Diagnostics GetDiagnostics() const;
		bool IsRaycastComparisonEnabled() const { return m_Settings.CompareRaycast; }
		void Clear();

	private:
		struct Snapshot : RefCounted
		{
			Soundwave::Grid Grid;
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
			Soundwave::SolveResult Result, Attempt;
			float GainLF = 1, GainHF = 1, TargetLF = 1, TargetHF = 1, SinceEvaluation = 0;
			uint64_t SeenUpdate = 0, GeometryGeneration = 0, SettingsGeneration = 0;
			Soundwave::EvaluationError LastError = Soundwave::EvaluationError::None;
			bool Evaluated = false, Attempted = false;
		};
		void RefreshGeometryView();
		bool CanPrepare() const;
		std::array<Ref<Snapshot>, 2> m_Snapshots;
		std::array<Builder, 2> m_Builders;
		std::array<uint64_t, 2> m_LayerRevisions{};
		uint32_t m_DirtyLayers = 0, m_FailedLayers = 0;
		std::array<uint64_t, 2> m_FailureLayerRevisions{};
		Ref<Job> m_Pending;
		uint64_t m_RequestedRevision = 0, m_FailedRevision = 0, m_LastAttemptedRevision = 0;
		Soundwave::Grid m_Empty;
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
		bool m_HasActiveInstances = false;
	};
}
