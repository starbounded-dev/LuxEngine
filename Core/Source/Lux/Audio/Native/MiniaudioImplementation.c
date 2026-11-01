// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// The one translation unit that compiles the vendored miniaudio and its Vorbis decoder for the
// native audio backend. miniaudio enables Vorbis when stb_vorbis's declarations precede its
// implementation, so the header-only pass comes first and the stb_vorbis implementation follows.
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "Miniaudio.h"
#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
