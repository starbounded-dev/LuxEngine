// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include "soundwave/AcousticPath.h"
#include <algorithm>
#include <cmath>

namespace Lux::Soundwave::Math
{
	inline Position Add(Position a, Position b) { return { a.X + b.X, a.Y + b.Y, a.Z + b.Z }; }
	inline Position Subtract(Position a, Position b) { return { a.X - b.X, a.Y - b.Y, a.Z - b.Z }; }
	inline Position Scale(Position a, float value) { return { a.X * value, a.Y * value, a.Z * value }; }
	inline float Dot(Position a, Position b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
	inline Position Cross(Position a, Position b)
	{
		return { a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X };
	}
	inline float Length(Position a) { return std::sqrt(Dot(a, a)); }
	inline Position Unit(Position a)
	{
		const double length = std::sqrt(static_cast<double>(a.X) * a.X + static_cast<double>(a.Y) * a.Y + static_cast<double>(a.Z) * a.Z);
		if (length == 0 || !std::isfinite(length))
			return {};
		return { static_cast<float>(a.X / length), static_cast<float>(a.Y / length), static_cast<float>(a.Z / length) };
	}
	inline bool Finite(Position a) { return std::isfinite(a.X) && std::isfinite(a.Y) && std::isfinite(a.Z); }
	inline float Component(Position a, std::uint32_t axis) { return axis == 0 ? a.X : (axis == 1 ? a.Y : a.Z); }
	inline Position Min(Position a, Position b)
	{
		return { std::min(a.X, b.X), std::min(a.Y, b.Y), std::min(a.Z, b.Z) };
	}
	inline Position Max(Position a, Position b)
	{
		return { std::max(a.X, b.X), std::max(a.Y, b.Y), std::max(a.Z, b.Z) };
	}
}
