// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

// Stand-in precompiled header for building Lux::Yaml outside the engine (tests/yaml/run.py): just
// the standard library and the one engine macro its sources use.
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define LUX_CORE_ASSERT(condition, ...) do { if (!(condition)) { std::fprintf(stderr, "assert: %s\n", #condition); std::abort(); } } while (0)
