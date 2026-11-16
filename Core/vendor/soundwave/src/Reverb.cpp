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
		constexpr double kNepersPerDecibel = 0.23025851; // ln(10) / 10, energy

		// Cosine-weighted direction about a unit normal from a fixed hash of (ray, bounce), so the
		// scattered share of a reflection spreads the same way on every run.
		Position Diffuse(Position normal, std::uint32_t ray, std::uint32_t bounce)
		{
			std::uint32_t h = ray * 0x9E3779B1u ^ (bounce + 1) * 0x85EBCA77u;
			h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
			const float u = static_cast<float>(h & 0xFFFFu) / 65536.0f, v = static_cast<float>(h >> 16) / 65536.0f;
			const Position helper = std::abs(normal.X) < 0.9f ? Position{ 1, 0, 0 } : Position{ 0, 1, 0 };
			const Position tangent = Math::Unit(Math::Cross(helper, normal)), bitangent = Math::Cross(normal, tangent);
			const float radius = std::sqrt(u), angle = 6.28318531f * v;
			return Math::Add(Math::Add(Math::Scale(tangent, radius * std::cos(angle)), Math::Scale(bitangent, radius * std::sin(angle))),
				Math::Scale(normal, std::sqrt(std::max(0.0f, 1.0f - u))));
		}

		EvaluationError ValidateInputs(const GeometryView& geometry, const MaterialTable& materials, const Settings& settings, Position listener)
		{
			if (!settings.Validate())
				return EvaluationError::InvalidSettings;
			for (const auto& material : materials)
				if (!IsValidMaterialProperties(material))
					return EvaluationError::InvalidMaterials;
			if (!geometry.IsValid())
				return EvaluationError::InvalidGeometry;
			if (!Math::Finite(listener))
				return EvaluationError::InvalidRay;
			return EvaluationError::None;
		}

		EvaluationError TraceRay(const GeometryView& geometry, const MaterialTable& materials, const Settings& settings,
			Position listener, std::uint32_t ray, std::vector<RayHit>& hits, ReverbRay& output)
		{
			output = {};
			// Fibonacci sphere: evenly spread, fixed order.
			const std::uint32_t rays = settings.ReverbRays;
			const float z = 1.0f - 2.0f * (static_cast<float>(ray) + 0.5f) / static_cast<float>(rays);
			const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
			const float angle = kGoldenAngle * static_cast<float>(ray);
			Position direction{ radius * std::cos(angle), z, radius * std::sin(angle) };
			Position origin = listener;
			for (std::uint32_t bounce = 0; bounce < settings.ReverbBounces; ++bounce)
			{
				++output.Traces;
				const Position end = Math::Add(origin, Math::Scale(direction, kEscapeDistance));
				if (!geometry.Trace(origin, end, 0, hits))
					return EvaluationError::InvalidGeometry;
				if (hits.empty())
				{
					output.Escaped = 1;
					break;
				}
				const RayHit& hit = hits.front();
				const Triangle triangle = geometry.GetTriangle(hit.TriangleIndex);
				const Position normal = Queries::Normal(triangle);
				if (triangle.Material >= materials.size() || !Math::Finite(normal) || Math::Dot(normal, normal) == 0)
					return EvaluationError::NumericalFailure;
				const auto& material = materials[triangle.Material];
				output.Absorbed += settings.Absorption * 0.5f * (material.AbsorptionLF + material.AbsorptionHF);
				output.Travelled += hit.Distance;
				++output.Hits;
				// Restart just off the surface on the incoming side. An endpoint margin alone lets a
				// corner bounce skip the adjacent face and leak out through the seam.
				const Position outward = Math::Dot(direction, normal) > 0 ? Math::Scale(normal, -1.0f) : normal;
				origin = Math::Add(Math::Add(origin, Math::Scale(direction, hit.Distance)), Math::Scale(outward, kSurfaceMargin));
				const Position specular = Math::Subtract(direction, Math::Scale(normal, 2.0f * Math::Dot(direction, normal)));
				// The surface's Scattering share leaves diffusely instead of as a mirror.
				direction = Math::Unit(Math::Add(Math::Scale(specular, 1.0f - material.Scattering),
					Math::Scale(Diffuse(outward, ray, bounce), material.Scattering)));
				if (Math::Dot(direction, direction) == 0)
					direction = specular;
			}
			return EvaluationError::None;
		}
	}

	EvaluationError TraceReverbRay(const GeometryView& geometry, const MaterialTable& materials,
		const Settings& settings, Position listener, std::uint32_t ray, std::vector<RayHit>& hits, ReverbRay& output)
	{
		output = {};
		if (const auto error = ValidateInputs(geometry, materials, settings, listener); error != EvaluationError::None)
			return error;
		if (ray >= settings.ReverbRays)
			return EvaluationError::InvalidRay;
		return TraceRay(geometry, materials, settings, listener, ray, hits, output);
	}

	EvaluationError FinishReverb(std::span<const ReverbRay> rays, const Settings& settings, ReverbResult& output)
	{
		output = {};
		if (!settings.Validate())
			return EvaluationError::InvalidSettings;
		if (rays.size() != settings.ReverbRays)
			return EvaluationError::InvalidRay;
		// Sabine's estimator over the ray field: losses per metre travelled, summed in ray order.
		double absorbed = 0, travelled = 0;
		std::uint32_t escaped = 0, hitCount = 0;
		for (const auto& ray : rays)
		{
			if (!std::isfinite(ray.Absorbed) || !std::isfinite(ray.Travelled) || ray.Escaped > 1)
				return EvaluationError::NumericalFailure;
			absorbed += ray.Absorbed;
			travelled += ray.Travelled;
			escaped += ray.Escaped;
			hitCount += ray.Hits;
			output.Traces += ray.Traces;
		}
		output.EscapedFraction = static_cast<float>(escaped) / static_cast<float>(rays.size());
		output.MeanFreePath = hitCount ? static_cast<float>(travelled / hitCount) : 0.0f;
		const double air = 0.005 * (static_cast<double>(settings.AirAbsorptionLF) + settings.AirAbsorptionHF) * kNepersPerDecibel; // per metre
		const double lossPerMetre = travelled > 0 ? (absorbed + escaped) / travelled + air : 0.0;
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

	EvaluationError EvaluateReverb(const GeometryView& geometry, const MaterialTable& materials,
		const Settings& settings, Position listener, std::vector<RayHit>& hits, ReverbResult& output)
	{
		output = {};
		if (const auto error = ValidateInputs(geometry, materials, settings, listener); error != EvaluationError::None)
			return error;
		std::vector<ReverbRay> rays(settings.ReverbRays);
		for (std::uint32_t ray = 0; ray < settings.ReverbRays; ++ray)
			if (const auto error = TraceRay(geometry, materials, settings, listener, ray, hits, rays[ray]); error != EvaluationError::None)
				return error;
		return FinishReverb(rays, settings, output);
	}
}
