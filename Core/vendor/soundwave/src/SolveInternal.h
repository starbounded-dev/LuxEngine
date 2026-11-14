// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#pragma once
#include "soundwave/Solve.h"
namespace Lux::Soundwave::Detail
{
	enum class Visibility { Clear, Blocked, Invalid };
	Visibility SegmentVisible(const GeometryView& grid, std::span<const PortalBox> portals,
		Position from, Position to, std::uint64_t sourceInstance, std::vector<RayHit>& hits, float margin);
}
