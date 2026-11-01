// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "soundwave/Reverb.h"
#include "GeometryMath.h"
#include "GeometryQueries.h"
#include <algorithm>
#include <cmath>

namespace Lux::Soundwave
{
	namespace
	{
		constexpr float kSpeedOfSound = 343.0f;
		constexpr float kSixtyDecibelsNepers = 13.815511f; // ln(10^6)
		constexpr float kSurfaceMargin = 0.001f;
		// shortcut: a ray that meets nothing within this many metres has escaped; raise it for halls larger than this.
		constexpr float kEscapeDistance = 200.0f;
		constexpr float kGoldenAngle = 2.39996323f;
		constexpr float kMaxDecaySeconds = 10.0f; // a lossless closed room rings at most this long
	}

	EvaluationError EvaluateReverb(const GeometryView& geometry, const MaterialTable& materials,
		const Settings& settings, Position listener, std::vector<RayHit>& hits, ReverbResult& output)
	{
		output = {};
		if (!settings.Validate())
			return EvaluationError::InvalidSettings;
		for (const auto& material : materials)
			if (!IsValidMaterialProperties(material))
				return EvaluationError::InvalidMaterials;
		if (!geometry.IsValid())
			return EvaluationError::InvalidGeometry;
		if (!Math::Finite(listener))
			return EvaluationError::InvalidRay;

		// Sabine's estimator over the ray field: losses per metre travelled. Doubles keep the
		// sum independent of ray count to within float noise.
		double absorbed = 0, travelled = 0;
		std::uint32_t escaped = 0, hitCount = 0, traces = 0;
		const std::uint32_t rays = settings.ReverbRays;
		for (std::uint32_t ray = 0; ray < rays; ++ray)
		{
			// Fibonacci sphere: evenly spread, fixed order.
			const float z = 1.0f - 2.0f * (static_cast<float>(ray) + 0.5f) / static_cast<float>(rays);
			const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
			const float angle = kGoldenAngle * static_cast<float>(ray);
			Position direction{ radius * std::cos(angle), z, radius * std::sin(angle) };
			Position origin = listener;
			for (std::uint32_t bounce = 0; bounce < settings.ReverbBounces; ++bounce)
			{
				++traces;
				const Position end = Math::Add(origin, Math::Scale(direction, kEscapeDistance));
				if (!geometry.Trace(origin, end, 0, hits))
					return EvaluationError::InvalidGeometry;
				if (hits.empty())
				{
					++escaped;
					break;
				}
				const RayHit& hit = hits.front();
				const Triangle triangle = geometry.GetTriangle(hit.TriangleIndex);
				const Position normal = Queries::Normal(triangle);
				if (triangle.Material >= materials.size() || !Math::Finite(normal) || Math::Dot(normal, normal) == 0)
					return EvaluationError::NumericalFailure;
				const auto& material = materials[triangle.Material];
				absorbed += settings.Absorption * 0.5 * (static_cast<double>(material.AbsorptionLF) + material.AbsorptionHF);
				travelled += hit.Distance;
				++hitCount;
				// Restart just off the surface on the incoming side. An endpoint margin alone lets a
				// corner bounce skip the adjacent face and leak out through the seam.
				const float side = Math::Dot(direction, normal) > 0 ? -kSurfaceMargin : kSurfaceMargin;
				origin = Math::Add(Math::Add(origin, Math::Scale(direction, hit.Distance)), Math::Scale(normal, side));
				direction = Math::Subtract(direction, Math::Scale(normal, 2.0f * Math::Dot(direction, normal)));
			}
		}

		output.Traces = traces;
		output.EscapedFraction = static_cast<float>(escaped) / static_cast<float>(rays);
		output.MeanFreePath = hitCount ? static_cast<float>(travelled / hitCount) : 0.0f;
		const double lossPerMetre = travelled > 0 ? (absorbed + escaped) / travelled : 0.0;
		if (travelled <= 0)
			output.DecaySeconds = 0; // nothing to reflect from
		else if (lossPerMetre <= 0)
			output.DecaySeconds = kMaxDecaySeconds; // lossless closed room
		else
			output.DecaySeconds = static_cast<float>(std::min<double>(kMaxDecaySeconds,
				kSixtyDecibelsNepers / (lossPerMetre * kSpeedOfSound)));
		// Energy that leaves the space never returns as reverb, so open ground stays dry.
		output.Send = (1.0f - output.EscapedFraction) * (1.0f - std::exp(-output.DecaySeconds / settings.ReverbReferenceSeconds));
		if (!std::isfinite(output.DecaySeconds) || !std::isfinite(output.Send))
		{
			output = {};
			return EvaluationError::NumericalFailure;
		}
		return EvaluationError::None;
	}
}
