// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// The one translation unit that compiles the vendored miniaudio, its Vorbis decoder and the reverb
// node. miniaudio enables Vorbis when stb_vorbis's declarations precede its implementation, so the
// header-only pass comes first and the stb_vorbis implementation follows.
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
#include "miniaudio.c"
#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
#include "extras/nodes/ma_reverb_node/ma_reverb_node.c"
