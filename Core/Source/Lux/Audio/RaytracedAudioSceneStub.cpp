// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// The build without --raytraced-audio. Asterwave is not compiled or linked; IsAvailable() is false and
// every call is a harmless no-op, so Scene and the editor keep one code path. Engine occlusion and
// reverb zones do not depend on this class.

#include "lpch.h"
#include "Lux/Audio/RaytracedAudioScene.h"

namespace Lux
{
	struct RaytracedAudioScene::Impl
	{
	};

	bool RaytracedAudioScene::IsAvailable()
	{
		return false;
	}

	bool RaytracedAudioScene::IsRunning() const
	{
		return false;
	}

	double RaytracedAudioScene::GetRaytracingTimeMilliseconds() const
	{
		return 0.0;
	}

	RaytracedAudioScene::RaytracedAudioScene(Scene* scene)
		: m_Scene(scene)
	{
	}

	RaytracedAudioScene::~RaytracedAudioScene() = default;

	void RaytracedAudioScene::Start(const AcousticMaterialSettings& materials)
	{
		(void)materials;
	}

	void RaytracedAudioScene::Stop()
	{
	}

	void RaytracedAudioScene::WaitForResults()
	{
	}

	void RaytracedAudioScene::OnUpdate(Timestep ts)
	{
		(void)ts;
	}

	bool RaytracedAudioScene::SetStaticGeometry(const std::vector<AcousticGeometry>& geometry)
	{
		(void)geometry;
		return false;
	}

	bool RaytracedAudioScene::SetGeometry(UUID id, const AcousticGeometry& geometry, const glm::mat4& transform, bool dynamic)
	{
		(void)id;
		(void)geometry;
		(void)transform;
		(void)dynamic;
		return false;
	}

	bool RaytracedAudioScene::UpdateGeometry(UUID id, const glm::mat4& transform, AcousticMaterial material, bool dynamic)
	{
		(void)id;
		(void)transform;
		(void)material;
		(void)dynamic;
		return false;
	}

	bool RaytracedAudioScene::RemoveGeometry(UUID id)
	{
		(void)id;
		return false;
	}

	bool RaytracedAudioScene::GetGeometryTransform(UUID id, glm::mat4& transform) const
	{
		(void)id;
		(void)transform;
		return false;
	}

	void RaytracedAudioScene::SetListener(const glm::vec3& position, const glm::vec3& forward)
	{
		(void)position;
		(void)forward;
	}

	void RaytracedAudioScene::CreateEmitter(UUID entityID)
	{
		(void)entityID;
	}

	void RaytracedAudioScene::DestroyEmitter(UUID entityID)
	{
		(void)entityID;
	}

	void RaytracedAudioScene::SetEmitterPosition(UUID entityID, const glm::vec3& position)
	{
		(void)entityID;
		(void)position;
	}

	void RaytracedAudioScene::SetEmitterMaxVolume(UUID entityID, float maxVolume)
	{
		(void)entityID;
		(void)maxVolume;
	}

	RaytracedAudioResult RaytracedAudioScene::GetResult(UUID entityID) const
	{
		(void)entityID;
		return {};
	}

	RaytracedAudioAmbience RaytracedAudioScene::GetAmbience() const
	{
		return {};
	}

	void RaytracedAudioScene::SetVisualisationEnabled(bool enabled, int rayCount, int bounceCount, int updateIntervalMs)
	{
		(void)enabled;
		(void)rayCount;
		(void)bounceCount;
		(void)updateIntervalMs;
	}

	void RaytracedAudioScene::GetVisualisation(RaytracedAudioVisualisation& visualisation) const
	{
		visualisation = {};
	}

	RaytracedAudioStats RaytracedAudioScene::GetStats() const
	{
		return {};
	}
}
