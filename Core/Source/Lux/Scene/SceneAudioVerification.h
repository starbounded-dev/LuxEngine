// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once
#include <string>

namespace Lux
{
	class Scene;
	enum class SceneAudioVerificationStatus
	{
		Pending,
		Passed,
		Failed
	};

	// Main thread only. Reads the running Scene and its live audio sources without
	// waiting for preparation or GPU work. The caller owns the resulting YAML/error.
	SceneAudioVerificationStatus CaptureSceneAudioVerification(Scene& scene, std::string& report, std::string& error);
}
