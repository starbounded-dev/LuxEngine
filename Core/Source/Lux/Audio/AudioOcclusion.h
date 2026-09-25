// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "AcousticMaterial.h"
#include "AudioGeometrySystem.h"
#include "AudioOcclusionSettings.h"
#include "Lux/Core/UUID.h"
#include <glm/glm.hpp>
#include <array>
#include <functional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Lux
{
	// One surface crossing on the listener->source segment, as a physics raycast reports it.
	// Exit is true for a back face (the ray leaving a solid).
	struct AudioOcclusionHit
	{
		UUID Entity = 0;
		float Distance = 0.0f;
		bool Exit = false;
	};

	// One obstruction on the path. Solid walls have a thickness (End - Start); flat walls are
	// one-sided or non-watertight surfaces, which VA also treats as a fixed per-touch loss.
	struct AudioOcclusionWall
	{
		UUID Entity = 0;
		AcousticMaterial Material = AcousticMaterial::Default;
		float Start = 0.0f;
		float End = 0.0f;
		bool Flat = false;
		bool Portal = false;
		float LossLF = 0.0f; // dB
		float LossHF = 0.0f; // dB
	};

	// The last evaluated path for one source, kept for the editor overlay.
	struct AudioOcclusionPath
	{
		glm::vec3 From{ 0.0f };
		glm::vec3 To{ 0.0f };
		float LossLF = 0.0f; // dB, sum over walls, capped
		float LossHF = 0.0f;
		std::vector<AudioOcclusionWall> Walls;
	};

	// Engine-side occlusion from raycasts through the acoustic geometry. It stands in for VA's
	// per-source occlusion, which does not respond to geometry (docs/vercidium-repro), and uses
	// VA's own material semantics so the two agree: TransmissionLF/HF is the thickness that costs
	// 30 dB, FlatTransmissionLF/HF the energy fraction lost on a one-sided surface.
	//
	// Main thread only. The caller supplies the raycast so this stays independent of Physics.
	class AudioOcclusion
	{
	public:
		struct Source
		{
			UUID Entity = 0;
			glm::vec3 Position{ 0.0f };
		};

		// Fills hits for the segment, in any order. Hits on non-acoustic entities are ignored.
		using RayCaster = std::function<void(UUID source, const glm::vec3& from, const glm::vec3& to, std::vector<AudioOcclusionHit>& hits)>;

		void Configure(const AcousticMaterialSettings& materials, const AudioOcclusionSettings& settings = {});

		// Acoustic geometry for this update: mesh colliders become materials by entity, portals
		// become boxes. portalOpen returns the Open that VA last applied for a portal entity.
		void SetGeometry(std::span<const AudioGeometryInput> inputs, const std::function<float(UUID)>& portalOpen);

		// Re-evaluates due sources within the cast budget and smooths every source's gain.
		void Update(float timestep, const glm::vec3& listener, std::span<const Source> sources, const RayCaster& cast);

		// Evaluates one path immediately. Public for tests.
		void Evaluate(const glm::vec3& from, const glm::vec3& to, std::vector<AudioOcclusionHit>& hits, AudioOcclusionPath& outPath) const;

		// Smoothed linear LF gain for the Studio Occlusion parameter; 1 when unknown.
		float GetGainLF(UUID source) const;
		const AudioOcclusionPath* GetPath(UUID source) const;

		void Clear();

	private:
		struct Portal
		{
			UUID Entity = 0;
			AcousticMaterial Material = AcousticMaterial::Default;
			glm::mat4 WorldToBox{ 1.0f };
		};

		struct SourceState
		{
			AudioOcclusionPath Path;
			float TargetGainLF = 1.0f;
			float GainLF = 1.0f;
			float SinceEvaluation = 0.0f;
			uint64_t SeenUpdate = 0;
			bool Evaluated = false;
		};

		void AddWall(AudioOcclusionPath& path, UUID entity, AcousticMaterial material, float start, float end, bool flat, bool portal) const;
		const AcousticMaterial* FindMaterial(UUID entity) const;

	private:
		std::array<AcousticMaterialProperties, AcousticMaterialCount> m_Properties{};
		AudioOcclusionSettings m_Settings;
		// Sorted by entity and rebuilt every update; a vector so steady-state updates never allocate.
		std::vector<std::pair<UUID, AcousticMaterial>> m_Materials;
		std::vector<Portal> m_Portals;
		std::unordered_map<UUID, SourceState> m_Sources;
		std::vector<AudioOcclusionHit> m_Hits;
		mutable std::vector<std::pair<UUID, float>> m_OpenSolids; // Evaluate scratch: entity, entry distance
		uint64_t m_UpdateIndex = 0;
		size_t m_NextSource = 0;
	};
}
