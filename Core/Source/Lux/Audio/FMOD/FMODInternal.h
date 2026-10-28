// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

// Private to the FMOD backend (Audio/FMOD/). Engine code outside this folder talks to AudioEngine and
// AudioEventInstance, whose headers name no FMOD type, so the backend can be swapped behind them.

#include <fmod_common.h>

namespace FMOD {
	class System;
	namespace Studio { class System; class Bus; }
}

namespace Lux::FMODInternal {

	// The Studio system, which owns banks, events and buses, and the Core system it created for
	// metadata, statistics and accessibility DSP processing. Both are null before AudioEngine::Init
	// succeeds and after Shutdown.
	FMOD::Studio::System* GetStudioSystem();
	FMOD::System* GetCoreSystem();

	// Reference-counted Studio::Bus::lockChannelGroup. Studio locks are not counted, so two
	// systems locking the same bus (the accessibility mixer and the performance monitor both
	// use "bus:/") would fail with FMOD_ERR_ALREADY_LOCKED, and the first unlock would pull the
	// group out from under the other. Every bus lock goes through these. Main thread only;
	// AudioEngine::UnloadAllBanks releases its holders first and then forgets any remaining counts.
	FMOD_RESULT LockBusChannelGroup(FMOD::Studio::Bus* bus);
	FMOD_RESULT UnlockBusChannelGroup(FMOD::Studio::Bus* bus);

}
