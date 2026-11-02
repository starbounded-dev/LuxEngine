// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "NativeInternal.h"

#include "Lux/Project/Project.h"

namespace Lux
{
	// The default output device, and the active project's budgets and focus option. The native tests
	// define their own (no device), so the engine itself never depends on a project.
	NativeInternal::EngineConfig NativeInternal::GetEngineConfig()
	{
		EngineConfig config;
		if (const auto project = Project::GetActive())
		{
			config.Performance = project->GetAudioPerformance();
			config.Mixer = project->GetConfig().Audio.Mixer;
		}
		return config;
	}
}
