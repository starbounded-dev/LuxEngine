// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "AudioAccessibilitySettings.h"
#include "Lux/Core/Base.h"

namespace Lux
{
	// Owns the accessibility processing (category gains, mono fold, compression), leaving the mixer's
	// bus volumes and gameplay bus controls intact.
	class AudioAccessibilityMixer
	{
	public:
		AudioAccessibilityMixer();
		~AudioAccessibilityMixer();
		AudioAccessibilityMixer(const AudioAccessibilityMixer&) = delete;
		AudioAccessibilityMixer& operator=(const AudioAccessibilityMixer&) = delete;
		bool Configure(const AudioAccessibilityConfig& config);
		bool Apply(const AudioAccessibilityPreferences& preferences, bool describing);
		void Reset(); // Must run before bank/system teardown.
		bool HasBus(AudioCategory category) const;
	private:
		struct Impl;
		Scope<Impl> m_Impl;
	};
}
