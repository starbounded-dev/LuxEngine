// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#pragma once
#include "Lux/Audio/AudioAcoustics.h"
#include "Lux/Core/Base.h"

namespace Lux
{
	class Scene;
	// Main-thread scene capture. Only immutable mesh references and value descriptors
	// cross into AudioAcoustics' preparation jobs; the cache never leaves main.
	class SceneAudioAcoustics
	{
	public:
		SceneAudioAcoustics();
		~SceneAudioAcoustics();
		bool Start(const AcousticMaterialSettings& materials, const AudioOcclusionSettings& settings);
		void Sync(Scene& scene);
		void Update(Scene& scene, float timestep, Soundwave::Position listener);
		void Clear();
		const AudioAcoustics& GetKernel() const { return m_Kernel; }
	private:
		struct Data;
		Scope<Data> m_Data;
		AudioAcoustics m_Kernel;
		std::vector<AudioAcoustics::Source> m_Sources;
		bool m_Configured = false;
	};
}
