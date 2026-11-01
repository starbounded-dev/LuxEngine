// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#pragma once

#include "soundwave/Grid.h"
#include "GeometryMath.h"
#include <limits>

namespace Lux::Soundwave::Queries
{
	constexpr float kBarycentricEpsilon = 1e-6f;
	struct PrecisePosition { double X, Y, Z; };
	inline PrecisePosition Difference(Position a, Position b)
	{
		return { static_cast<double>(a.X) - b.X, static_cast<double>(a.Y) - b.Y, static_cast<double>(a.Z) - b.Z };
	}
	inline PrecisePosition Cross(PrecisePosition a, PrecisePosition b)
	{
		return { a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X };
	}
	inline double Dot(PrecisePosition a, PrecisePosition b)
	{
		return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
	}
	inline double SegmentDistanceSquared(Position point, Position a, Position b)
	{
		const auto edge = Difference(b, a), offset = Difference(point, a);
		const double lengthSquared = Dot(edge, edge);
		const double t = lengthSquared == 0 ? 0 : std::clamp(Dot(offset, edge) / lengthSquared, 0.0, 1.0);
		const PrecisePosition rest{ offset.X - t * edge.X, offset.Y - t * edge.Y, offset.Z - t * edge.Z };
		return Dot(rest, rest);
	}
	inline double SurfaceDistanceSquared(Position point, const Triangle& triangle)
	{
		const auto normal = Cross(Difference(triangle.B, triangle.A), Difference(triangle.C, triangle.A));
		if (Dot(Cross(Difference(triangle.B, triangle.A), Difference(point, triangle.A)), normal) >= 0 &&
			Dot(Cross(Difference(triangle.C, triangle.B), Difference(point, triangle.B)), normal) >= 0 &&
			Dot(Cross(Difference(triangle.A, triangle.C), Difference(point, triangle.C)), normal) >= 0)
		{
			const double distance = Dot(Difference(point, triangle.A), normal);
			return distance * distance / Dot(normal, normal);
		}
		return std::min({ SegmentDistanceSquared(point, triangle.A, triangle.B),
			SegmentDistanceSquared(point, triangle.B, triangle.C), SegmentDistanceSquared(point, triangle.C, triangle.A) });
	}

	inline bool Intersects(const Bounds& box, Position from, Position direction, float length)
	{
		float start = 0.0f, end = length;
		for (std::uint32_t axis = 0; axis < 3; ++axis)
		{
			const float origin = Math::Component(from, axis);
			const float delta = Math::Component(direction, axis);
			const float low = Math::Component(box.Min, axis), high = Math::Component(box.Max, axis);
			if (delta == 0.0f)
			{
				if (origin < low || origin > high)
					return false;
				continue;
			}
			const float a = (low - origin) / delta, b = (high - origin) / delta;
			start = std::max(start, std::min(a, b));
			end = std::min(end, std::max(a, b));
			if (start > end)
				return false;
		}
		return true;
	}

	inline bool IntersectTriangle(const Triangle& triangle, Position from, Position direction, float& distance, bool& exit)
	{
		// Wide intermediates prevent a valid large triangle's determinant numerator
		// overflowing into a silent miss. Geometry and returned distances stay floats.
		const PrecisePosition edge1 = Difference(triangle.B, triangle.A);
		const PrecisePosition edge2 = Difference(triangle.C, triangle.A);
		const PrecisePosition ray{ direction.X, direction.Y, direction.Z };
		const PrecisePosition p = Cross(ray, edge2);
		const double determinant = Dot(edge1, p);
		if (determinant == 0.0)
			return false;
		const double inverse = 1.0 / determinant;
		const PrecisePosition offset = Difference(from, triangle.A);
		const double u = Dot(offset, p) * inverse;
		if (u < -kBarycentricEpsilon || u > 1.0f + kBarycentricEpsilon)
			return false;
		const PrecisePosition q = Cross(offset, edge1);
		const double v = Dot(ray, q) * inverse;
		if (v < -kBarycentricEpsilon || u + v > 1.0f + kBarycentricEpsilon)
			return false;
		const double hitDistance = Dot(edge2, q) * inverse;
		if (!std::isfinite(hitDistance) || hitDistance > std::numeric_limits<float>::max() ||
			hitDistance < std::numeric_limits<float>::lowest())
			return false;
		distance = static_cast<float>(hitDistance);
		exit = determinant < 0.0f;
		return std::isfinite(distance);
	}
}
