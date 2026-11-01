// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "soundwave/Solve.h"
#include "SolveInternal.h"
#include "GeometryMath.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Lux::Soundwave
{
	namespace
	{
		constexpr float kSurfaceMargin = 0.001f;
		constexpr float kOpeningClearance = 0.004f;
		constexpr float kReferenceDistance = 1.0f;
		constexpr float kSpeedOfSound = 343.0f;
		constexpr float kFrequencyLF = 250.0f, kFrequencyHF = 4000.0f;
		constexpr std::uint32_t kPreparationNodesPerWork = 32;
		constexpr std::array<std::array<float, 2>, 8> kCircle{{ {1,0}, {0.70710678f,0.70710678f}, {0,1}, {-0.70710678f,0.70710678f},
			{-1,0}, {-0.70710678f,-0.70710678f}, {0,-1}, {0.70710678f,-0.70710678f} }};

		float Distance(Position a, Position b) { return Math::Length(Math::Subtract(a, b)); }
		Position Normal(const Triangle& triangle)
		{
			return Math::Unit(Math::Cross(Math::Subtract(triangle.B, triangle.A), Math::Subtract(triangle.C, triangle.A)));
		}
		Position Mirror(Position point, const Triangle& triangle)
		{
			const auto normal = Normal(triangle);
			return Math::Subtract(point, Math::Scale(normal, 2.0f * Math::Dot(Math::Subtract(point, triangle.A), normal)));
		}
		bool OnTriangle(Position point, const Triangle& triangle)
		{
			const auto normal = Normal(triangle);
			for (const auto& side : { std::pair{triangle.A,triangle.B}, std::pair{triangle.B,triangle.C}, std::pair{triangle.C,triangle.A} })
			{
				const auto edge = Math::Subtract(side.second, side.first);
				if (Math::Dot(Math::Cross(edge, Math::Subtract(point, side.first)), normal) < -kSurfaceMargin * Math::Length(edge))
					return false;
			}
			return true;
		}

		struct Context
		{
			const GeometryView& World;
			std::span<const PortalBox> Portals;
			const MaterialTable& Materials;
			const Settings& Config;
			Position Listener, Source;
			std::uint64_t SourceInstance;
			SolveScratch& Scratch;
			SolveResult& Result;
			std::uint32_t Limit;
			bool Invalid = false;

			bool Spend()
			{
				if (Result.Diagnostics.Work >= Limit)
				{
					Result.Diagnostics.WorkLimited = true;
					return false;
				}
				++Result.Diagnostics.Work;
				return true;
			}
			bool Visible(Position from, Position to, float margin)
			{
				if (!Spend())
					return false;
				++Result.Diagnostics.VisibilityRays;
				const auto visibility = Detail::SegmentVisible(World, Portals, from, to, SourceInstance, Scratch.Hits, margin);
				Invalid |= visibility == Detail::Visibility::Invalid;
				return visibility == Detail::Visibility::Clear;
			}
			void Accept(const PropagationPath& path)
			{
				if (!std::isfinite(path.Distance) || !std::isfinite(path.GainLF) || !std::isfinite(path.GainHF))
				{
					Invalid = true;
					return;
				}
				if (path.Kind == PathKind::Opening)
				{
					if (path.GainLF > Result.OcclusionGainLF)
					{
						Result.AudibleOpening = path;
						Result.HasAudibleOpening = true;
					}
					Result.OcclusionGainLF = std::max(Result.OcclusionGainLF, path.GainLF);
					Result.OcclusionGainHF = std::max(Result.OcclusionGainHF, path.GainHF);
				}
				for (const auto& existing : Result.Indirect)
				{
					if (existing.Kind != path.Kind || existing.PointCount != path.PointCount)
						continue;
					bool duplicate = true;
					for (std::uint32_t i = 1; i + 1 < path.PointCount; ++i)
						duplicate &= Distance(existing.Points[i], path.Points[i]) <= kSurfaceMargin;
					if (duplicate)
						return;
				}
				if (Result.Indirect.size() >= Config.PathBudget)
				{
					Result.Diagnostics.PathsLimited = true;
					return;
				}
				Result.Indirect.push_back(path);
				if (path.Kind == PathKind::Opening)
					++Result.Diagnostics.OpeningPaths;
				else
					++Result.Diagnostics.ReflectionPaths;
			}
			float Spread(float distance) const
			{
				// Relative excess-path spreading only; the direct signal keeps FMOD's fade.
				return std::min(1.0f, std::max(Result.Direct.Distance, kReferenceDistance) / std::max(distance, kReferenceDistance));
			}
		};

		double BoxDistance(const Bounds& box, Position point)
		{
			double squared = 0;
			for (std::uint32_t axis = 0; axis < 3; ++axis)
			{
				const double p = Math::Component(point, axis), low = Math::Component(box.Min, axis), high = Math::Component(box.Max, axis);
				const double delta = p < low ? low - p : (p > high ? p - high : 0);
				squared += delta * delta;
			}
			return std::sqrt(squared);
		}
		void Prepare(Context& context)
		{
			auto& scratch = context.Scratch;
			scratch.NodeHeap.clear();
			scratch.Triangles.clear();
			if (context.World.GetNodeCount() == 0)
				return;
			const auto compare = [](const auto& a, const auto& b) { return a.Score != b.Score ? a.Score > b.Score : a.Node > b.Node; };
			const auto push = [&](std::uint32_t node)
			{
				const auto& box = context.World.GetNode(node).Box;
				scratch.NodeHeap.push_back({ node, BoxDistance(box, context.Listener) + BoxDistance(box, context.Source) });
				std::push_heap(scratch.NodeHeap.begin(), scratch.NodeHeap.end(), compare);
			};
			for (auto root : context.World.GetRoots())
				if (root != kInvalidGeometryIndex)
					push(root);
			const auto maximum = std::max(4u, context.Config.CandidateBudget / 8);
			while (!scratch.NodeHeap.empty() && scratch.Triangles.size() < maximum &&
				context.Result.Diagnostics.PreparationNodes < context.Config.CandidateBudget * kPreparationNodesPerWork)
			{
				++context.Result.Diagnostics.PreparationNodes;
				std::pop_heap(scratch.NodeHeap.begin(), scratch.NodeHeap.end(), compare);
				const auto index = scratch.NodeHeap.back().Node;
				scratch.NodeHeap.pop_back();
				const auto& node = context.World.GetNode(index);
				if (!node.Count)
				{
					push(index + 1);
					push(node.Right);
					continue;
				}
				for (std::uint32_t i = node.First; i < node.First + node.Count && scratch.Triangles.size() < maximum; ++i)
				{
					const auto triangle = context.World.TriangleAtOrder(i);
					if (context.World.GetTriangle(triangle).Instance != context.SourceInstance &&
						context.World.IsInstanceActive(context.World.GetTriangle(triangle).Instance))
						scratch.Triangles.push_back(triangle);
				}
			}
			context.Result.Diagnostics.WorkLimited |= !scratch.NodeHeap.empty() ||
				(scratch.Triangles.size() == maximum && context.World.GetTriangleCount() > maximum);
		}

		void Reflections(Context& context)
		{
			const auto& pool = context.Scratch.Triangles;
			if (pool.empty())
				return;
			std::array<std::uint32_t, kMaximumInteractions> sequence{};
			std::array<Position, kMaximumInteractions + 1> images{};
			for (std::uint32_t depth = 1; depth <= context.Config.ReflectionDepth; ++depth)
			{
				sequence.fill(0);
				bool done = false;
				while (!done)
				{
					if (!context.Spend() || context.Invalid)
						return;
					PropagationPath path;
					path.Reflections = depth;
					path.PointCount = depth + 2;
					path.Points[0] = context.Listener;
					path.Points[depth + 1] = context.Source;
					images[0] = context.Source;
					bool valid = true;
					for (std::uint32_t i = 0; i < depth; ++i)
					{
						if (i && sequence[i] == sequence[i - 1])
							valid = false;
						images[i + 1] = Mirror(images[i], context.World.GetTriangle(pool[sequence[i]]));
						if (!Math::Finite(images[i + 1]))
							context.Invalid = true;
					}
					for (std::uint32_t i = 0; valid && i < depth; ++i)
					{
						const auto& triangle = context.World.GetTriangle(pool[sequence[depth - i - 1]]);
						const auto normal = Normal(triangle);
						const Position current = path.Points[i], delta = Math::Subtract(images[depth - i], current);
						const float denominator = Math::Dot(delta, normal);
						const float t = denominator == 0 ? -1 : Math::Dot(Math::Subtract(triangle.A, current), normal) / denominator;
						valid = t > 0 && t < 1;
						if (valid)
						{
							path.Points[i + 1] = Math::Add(current, Math::Scale(delta, t));
							valid = OnTriangle(path.Points[i + 1], triangle);
						}
					}
					float energyLF = 1, energyHF = 1;
					for (std::uint32_t i = 1; valid && i <= depth; ++i)
					{
						const auto& triangle = context.World.GetTriangle(pool[sequence[depth - i]]);
						const auto normal = Normal(triangle);
						const float before = Math::Dot(Math::Subtract(path.Points[i - 1], triangle.A), normal);
						const float after = Math::Dot(Math::Subtract(path.Points[i + 1], triangle.A), normal);
						valid = before * after > 0;
						const auto& material = context.Materials[triangle.Material];
						energyLF *= (1 - context.Config.Absorption * material.AbsorptionLF) * (1 - material.Scattering);
						energyHF *= (1 - context.Config.Absorption * material.AbsorptionHF) * (1 - material.Scattering);
					}
					for (std::uint32_t i = 1; valid && i < path.PointCount; ++i)
					{
						path.Distance += Distance(path.Points[i - 1], path.Points[i]);
						valid = context.Visible(path.Points[i - 1], path.Points[i], kSurfaceMargin);
					}
					if (valid)
					{
						path.GainLF = std::sqrt(energyLF) * context.Spread(path.Distance);
						path.GainHF = std::sqrt(energyHF) * context.Spread(path.Distance);
						context.Accept(path);
					}
					for (std::uint32_t digit = depth; digit > 0; --digit)
					{
						if (++sequence[digit - 1] < pool.size())
							break;
						sequence[digit - 1] = 0;
						if (digit == 1)
							done = true;
					}
				}
			}
		}

		float ApexT(const Edge& edge, Position listener, Position source)
		{
			const auto delta = Math::Subtract(edge.B, edge.A);
			const auto score = [&](float t)
			{
				const auto point = Math::Add(edge.A, Math::Scale(delta, t));
				return Distance(listener, point) + Distance(point, source);
			};
			float low = 0, high = 1;
			for (int i = 0; i < 24; ++i)
			{
				const float a = low + (high - low) / 3, b = high - (high - low) / 3;
				if (score(a) < score(b))
					high = b;
				else
					low = a;
			}
			return (low + high) * 0.5f;
		}
		void OpeningSamples(Context& context)
		{
			auto& scratch = context.Scratch;
			scratch.EdgeIndices.clear();
			scratch.Edges.clear();
			scratch.Openings.clear();
			for (auto triangle : scratch.Triangles)
				for (auto edge : context.World.GetTriangleEdges(triangle))
					if (edge != kInvalidGeometryIndex)
						scratch.EdgeIndices.push_back(edge);
			std::sort(scratch.EdgeIndices.begin(), scratch.EdgeIndices.end());
			scratch.EdgeIndices.erase(std::unique(scratch.EdgeIndices.begin(), scratch.EdgeIndices.end()), scratch.EdgeIndices.end());
			for (auto index : scratch.EdgeIndices)
			{
				const auto& edge = context.World.GetEdge(index);
				const float t = ApexT(edge, context.Listener, context.Source);
				const auto point = Math::Add(edge.A, Math::Scale(Math::Subtract(edge.B, edge.A), t));
				scratch.Edges.push_back({ index,t,Distance(context.Listener, point) + Distance(point, context.Source) });
			}
			std::sort(scratch.Edges.begin(), scratch.Edges.end(), [](const auto& a, const auto& b)
				{ return a.Score != b.Score ? a.Score < b.Score : a.Index < b.Index; });
			for (std::uint32_t sample = 0; sample <= context.Config.OpeningGridSize; ++sample)
				for (const auto& candidate : scratch.Edges)
				{
					const auto& edge = context.World.GetEdge(candidate.Index);
					const auto axis = Math::Unit(Math::Subtract(edge.B, edge.A));
					const auto helper = std::abs(axis.Y) < 0.9f ? Position{0,1,0} : Position{1,0,0};
					const auto u = Math::Unit(Math::Cross(axis, helper)), v = Math::Cross(axis, u);
					const float t = sample ? static_cast<float>(sample - 1) / static_cast<float>(context.Config.OpeningGridSize - 1) : candidate.T;
					const auto apex = Math::Add(edge.A, Math::Scale(Math::Subtract(edge.B, edge.A), t));
					for (const auto& direction : kCircle)
					{
						if (scratch.Openings.size() >= context.Config.OpeningNodeBudget || !context.Spend())
						{
							context.Result.Diagnostics.WorkLimited = true;
							return;
						}
						const auto offset = Math::Scale(Math::Add(Math::Scale(u, direction[0]), Math::Scale(v, direction[1])), kOpeningClearance);
						const auto point = Math::Add(apex, offset);
						bool clear = false;
						if (!context.World.HasSurfaceClearance(point, kSurfaceMargin, context.SourceInstance, clear))
						{
							context.Invalid = true;
							return;
						}
						if (clear)
							scratch.Openings.push_back({ point,candidate.Index });
					}
				}
		}

		float EdgeLoss(float excess, float frequency)
		{
			// Fresnel knife-edge approximation, P.526-6 eq. 17, with acoustic wavelength.
			// v=2*sqrt(excess/wavelength) is the small-angle excess-path relation. This
			// is a geometric shadow approximation, not a full finite-wedge wave solver.
			const float v = 2 * std::sqrt(std::max(excess, 0.0f) * frequency / kSpeedOfSound);
			const float a = v - 0.1f;
			return 6.9f + 20 * std::log10(std::sqrt(a * a + 1) + a);
		}
		void OpeningPath(Context& context, std::uint32_t stateIndex)
		{
			const auto& scratch = context.Scratch;
			const auto& state = scratch.States[stateIndex];
			PropagationPath path;
			path.Kind = PathKind::Opening;
			path.Diffractions = state.Depth;
			path.PointCount = state.Depth + 2;
			path.Points[0] = context.Listener;
			path.Points[state.Depth + 1] = context.Source;
			std::uint32_t current = stateIndex;
			for (std::uint32_t i = state.Depth; i > 0; --i)
			{
				path.Points[i] = scratch.Openings[scratch.States[current].Node].Point;
				current = scratch.States[current].Parent;
			}
			float lossLF = 0, lossHF = 0;
			for (std::uint32_t i = 1; i + 1 < path.PointCount; ++i)
			{
				// An unnecessary bend in clear space is not a diffraction event.
				if (context.Visible(path.Points[i - 1], path.Points[i + 1], 0) || context.Invalid || context.Result.Diagnostics.Work >= context.Limit)
					return;
				const float excess = Distance(path.Points[i - 1], path.Points[i]) + Distance(path.Points[i], path.Points[i + 1]) -
					Distance(path.Points[i - 1], path.Points[i + 1]);
				lossLF += EdgeLoss(excess, kFrequencyLF);
				lossHF += EdgeLoss(excess, kFrequencyHF);
			}
			for (std::uint32_t i = 1; i < path.PointCount; ++i)
				path.Distance += Distance(path.Points[i - 1], path.Points[i]);
			path.GainLF = GainFromLossDb(lossLF) * context.Spread(path.Distance);
			path.GainHF = GainFromLossDb(lossHF) * context.Spread(path.Distance);
			context.Accept(path);
		}
		void Openings(Context& context)
		{
			OpeningSamples(context);
			auto& scratch = context.Scratch;
			scratch.States.clear();
			scratch.SearchHeap.clear();
			const auto count = static_cast<std::uint32_t>(scratch.Openings.size());
			scratch.BestDistance.assign(static_cast<std::size_t>(count) * (context.Config.MaxBounces + 1), std::numeric_limits<float>::infinity());
			const auto compare = [&](std::uint32_t a, std::uint32_t b)
			{
				const auto& x = scratch.States[a];
				const auto& y = scratch.States[b];
				return x.Score != y.Score ? x.Score > y.Score : a > b;
			};
			const auto push = [&](std::uint32_t node, std::uint32_t depth, std::uint32_t parent, float distance)
			{
				auto& best = scratch.BestDistance[static_cast<std::size_t>(depth) * count + node];
				if (distance >= best)
					return;
				best = distance;
				scratch.SearchHeap.push_back(static_cast<std::uint32_t>(scratch.States.size()));
				scratch.States.push_back({ node,depth,parent,distance,distance + Distance(scratch.Openings[node].Point, context.Source) });
				std::push_heap(scratch.SearchHeap.begin(), scratch.SearchHeap.end(), compare);
			};
			for (std::uint32_t i = 0; i < count && context.Result.Diagnostics.Work < context.Limit; ++i)
				if (context.Visible(context.Listener, scratch.Openings[i].Point, 0))
					push(i, 1, kInvalidGeometryIndex, Distance(context.Listener, scratch.Openings[i].Point));
			while (!scratch.SearchHeap.empty() && context.Result.Diagnostics.Work < context.Limit && !context.Invalid)
			{
				std::pop_heap(scratch.SearchHeap.begin(), scratch.SearchHeap.end(), compare);
				const auto index = scratch.SearchHeap.back();
				scratch.SearchHeap.pop_back();
				const auto state = scratch.States[index]; // pushes below may reallocate States
				const auto& node = scratch.Openings[state.Node];
				if (context.Visible(node.Point, context.Source, 0))
					OpeningPath(context, index);
				else if (state.Depth < context.Config.MaxBounces)
					for (std::uint32_t next = 0; next < count && context.Result.Diagnostics.Work < context.Limit; ++next)
					{
						bool repeated = false;
						for (auto prior = index; prior != kInvalidGeometryIndex; prior = scratch.States[prior].Parent)
							repeated |= scratch.Openings[scratch.States[prior].Node].EdgeIndex == scratch.Openings[next].EdgeIndex;
						if (!repeated && context.Visible(node.Point, scratch.Openings[next].Point, 0))
							push(next, state.Depth + 1, index, state.Distance + Distance(node.Point, scratch.Openings[next].Point));
					}
			}
			context.Result.Diagnostics.WorkLimited |= !scratch.SearchHeap.empty();
		}
	}

	EvaluationError Solve(const Grid& grid, std::span<const PortalBox> portals,
		const MaterialTable& materials, const Settings& settings, Position listener,
		Position source, std::uint64_t sourceInstance, SolveScratch& scratch, SolveResult& output)
	{
		return Solve(GeometryView(grid), portals, materials, settings, listener, source, sourceInstance, scratch, output);
	}

	EvaluationError Solve(const GeometryView& grid, std::span<const PortalBox> portals,
		const MaterialTable& materials, const Settings& settings, Position listener,
		Position source, std::uint64_t sourceInstance, SolveScratch& scratch, SolveResult& output)
	{
		output.Indirect.clear();
		output.AudibleOpening = {};
		output.HasAudibleOpening = false;
		output.Diagnostics = {};
		output.OcclusionGainLF = output.OcclusionGainHF = 1;
		const auto error = EvaluateDirect(grid, portals, materials, settings, listener, source, sourceInstance, scratch.Direct, output.Direct);
		if (error != EvaluationError::None)
			return error;
		output.OcclusionGainLF = output.Direct.GainLF();
		output.OcclusionGainHF = output.Direct.GainHF();
		if ((!settings.ReflectionDepth && settings.OpeningGridSize == 1) || output.Direct.Distance <= kSurfaceMargin)
			return EvaluationError::None;
		Context context{ grid,portals,materials,settings,listener,source,sourceInstance,scratch,output,settings.CandidateBudget };
		Prepare(context);
		if (settings.ReflectionDepth)
		{
			if (settings.OpeningGridSize > 1)
				context.Limit = std::max(1u, settings.CandidateBudget / 2);
			Reflections(context);
		}
		context.Limit = settings.CandidateBudget;
		if (settings.OpeningGridSize > 1 && (output.OcclusionGainLF < 1 || output.OcclusionGainHF < 1) && !context.Invalid)
			Openings(context);
		output.Diagnostics.WorkLimited |= output.Diagnostics.Work >= settings.CandidateBudget;
		if (context.Invalid)
		{
			output.Indirect.clear();
			output.HasAudibleOpening = false;
			output.OcclusionGainLF = output.Direct.GainLF();
			output.OcclusionGainHF = output.Direct.GainHF();
			return EvaluationError::NumericalFailure;
		}
		return EvaluationError::None;
	}
}
