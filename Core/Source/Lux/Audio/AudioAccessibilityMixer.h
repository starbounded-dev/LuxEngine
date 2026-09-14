#pragma once

#include "AudioAccessibilitySettings.h"
#include "Lux/Core/Base.h"

namespace Lux
{
	// Owns extra FMOD DSPs, leaving authored bus volumes and gameplay bus controls intact.
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
