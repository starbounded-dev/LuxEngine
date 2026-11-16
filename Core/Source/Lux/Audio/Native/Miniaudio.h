// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

// Private to the native backend (Audio/Native/). Every unit that uses miniaudio, and its single
// implementation unit, includes it through this header so they agree on the configuration macros:
// miniaudio's structs change size with them. Plain C, because the implementation unit is C.

// One miniaudio listener per engine listener slot (AudioListener::MaxListeners).
#if defined(MA_ENGINE_MAX_LISTENERS) && MA_ENGINE_MAX_LISTENERS != 8
#error "MA_ENGINE_MAX_LISTENERS must match AudioListener::MaxListeners (8)"
#endif
#define MA_ENGINE_MAX_LISTENERS 8

#include <miniaudio.h>
