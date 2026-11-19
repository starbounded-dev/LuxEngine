// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#include "lpch.h"
#include "ParticleSystem.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Lux::Particles
{
	namespace
	{
		// splitmix64 to seed, xorshift64* to draw: small, fast, and the same on every platform.
		uint64_t SplitMix(uint64_t x)
		{
			x += 0x9E3779B97F4A7C15ull;
			x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
			x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
			return x ^ (x >> 31);
		}

		uint64_t NextBits(State& state)
		{
			uint64_t x = state.Rng;
			x ^= x >> 12;
			x ^= x << 25;
			x ^= x >> 27;
			state.Rng = x;
			return x * 0x2545F4914F6CDD1Dull;
		}

		float Next(State& state) { return static_cast<float>(NextBits(state) >> 40) * (1.0f / 16777216.0f); }
		float Between(State& state, const Range& range) { return range.Min + (range.Max - range.Min) * Next(state); }

		float SafeRatio(float age, float lifetime) { return lifetime > 0.0f ? std::clamp(age / lifetime, 0.0f, 1.0f) : 1.0f; }

		// The emitter's rotation without its scale.
		glm::mat3 RotationOf(const glm::mat4& m)
		{
			const auto axis = [](glm::vec3 v, const glm::vec3& fallback) { const float l = glm::length(v); return l > 1e-6f ? v / l : fallback; };
			return glm::mat3(axis(glm::vec3(m[0]), { 1, 0, 0 }), axis(glm::vec3(m[1]), { 0, 1, 0 }), axis(glm::vec3(m[2]), { 0, 0, 1 }));
		}

		void RemoveAt(State& s, size_t i)
		{
			const size_t last = s.Age.size() - 1;
			if (i != last)
			{
				s.Position[i] = s.Position[last];
				s.Velocity[i] = s.Velocity[last];
				s.Age[i] = s.Age[last];
				s.Lifetime[i] = s.Lifetime[last];
				s.Rotation[i] = s.Rotation[last];
				s.AngularVelocity[i] = s.AngularVelocity[last];
				s.StartSize[i] = s.StartSize[last];
				s.FrameSeed[i] = s.FrameSeed[last];
			}
			s.Position.pop_back();
			s.Velocity.pop_back();
			s.Age.pop_back();
			s.Lifetime.pop_back();
			s.Rotation.pop_back();
			s.AngularVelocity.pop_back();
			s.StartSize.pop_back();
			s.FrameSeed.pop_back();
		}

		void Spawn(State& state, const Settings& s, const glm::mat4& emitter, uint32_t count)
		{
			const uint32_t limit = std::min(s.MaxParticles, Settings::HardMaxParticles);
			const bool planar = s.Mode == RenderMode::Sprite2D;
			const glm::mat3 rotation = RotationOf(emitter);
			for (uint32_t n = 0; n < count; n++)
			{
				if (state.Count() >= limit)
				{
					state.Dropped += count - n;
					return;
				}

				glm::vec3 position{ 0.0f };
				glm::vec3 direction{ 0.0f, 1.0f, 0.0f };
				switch (s.EmitShape)
				{
					case Shape::Cone:
					{
						const float angle = glm::radians(std::clamp(s.ConeAngle, 0.0f, 180.0f));
						if (planar)
						{
							const float theta = (Next(state) * 2.0f - 1.0f) * angle;
							direction = { std::sin(theta), std::cos(theta), 0.0f };
							position.x = (Next(state) * 2.0f - 1.0f) * s.Radius;
						}
						else
						{
							const float cosTheta = 1.0f - Next(state) * (1.0f - std::cos(angle));
							const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
							const float phi = Next(state) * glm::two_pi<float>();
							direction = { sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi) };
							const float r = s.Radius * std::sqrt(Next(state));
							const float a = Next(state) * glm::two_pi<float>();
							position = { r * std::cos(a), 0.0f, r * std::sin(a) };
						}
						break;
					}
					case Shape::Sphere:
					{
						if (planar)
						{
							const float a = Next(state) * glm::two_pi<float>();
							direction = { std::cos(a), std::sin(a), 0.0f };
						}
						else
						{
							const float y = Next(state) * 2.0f - 1.0f;
							const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
							const float a = Next(state) * glm::two_pi<float>();
							direction = { r * std::cos(a), y, r * std::sin(a) };
						}
						position = direction * s.Radius;
						break;
					}
					case Shape::Box:
					{
						position = { (Next(state) - 0.5f) * s.BoxSize.x, (Next(state) - 0.5f) * s.BoxSize.y, planar ? 0.0f : (Next(state) - 0.5f) * s.BoxSize.z };
						break;
					}
				}

				const float speed = Between(state, s.Speed);
				glm::vec3 velocity = direction * speed;
				if (s.WorldSpace)
				{
					position = glm::vec3(emitter * glm::vec4(position, 1.0f));
					velocity = rotation * velocity;
				}

				state.Position.push_back(position);
				state.Velocity.push_back(velocity);
				state.Age.push_back(0.0f);
				state.Lifetime.push_back(std::max(0.01f, Between(state, s.Lifetime)));
				state.Rotation.push_back(glm::radians(Between(state, s.Rotation)));
				state.AngularVelocity.push_back(glm::radians(Between(state, s.AngularVelocity)));
				state.StartSize.push_back(std::max(0.0f, Between(state, s.Size)));
				state.FrameSeed.push_back(static_cast<uint32_t>(NextBits(state) >> 32));
			}
		}

		void StepOnce(State& state, const Settings& s, const glm::mat4& emitter)
		{
			constexpr float dt = FixedStep;

			// Age and retire.
			for (size_t i = 0; i < state.Age.size();)
			{
				state.Age[i] += dt;
				if (state.Age[i] >= state.Lifetime[i])
					RemoveAt(state, i);
				else
					i++;
			}

			// Move.
			const float dragScale = std::max(0.0f, 1.0f - s.Drag * dt);
			for (size_t i = 0; i < state.Age.size(); i++)
			{
				const float t = SafeRatio(state.Age[i], state.Lifetime[i]);
				state.Velocity[i] = state.Velocity[i] * dragScale + s.Gravity * dt;
				state.Position[i] += state.Velocity[i] * (s.SpeedOverLife.Evaluate(t) * dt);
				state.Rotation[i] += state.AngularVelocity[i] * dt;
			}

			// Release.
			const float before = state.Time;
			state.Time += dt;
			const bool emitting = s.Looping || before < s.Duration;
			if (emitting)
			{
				state.EmitAccumulator += std::max(0.0f, s.Rate) * dt;
				const uint32_t whole = static_cast<uint32_t>(state.EmitAccumulator);
				state.EmitAccumulator -= static_cast<float>(whole);
				Spawn(state, s, emitter, whole);

				for (uint32_t b = 0; b < std::min(s.BurstCount, Settings::MaxBursts); b++)
				{
					const Burst& burst = s.Bursts[b];
					uint16_t& done = state.BurstCyclesDone[b];
					while (done < burst.Cycles && state.Time >= burst.Time + static_cast<float>(done) * burst.Interval && burst.Time + static_cast<float>(done) * burst.Interval < s.Duration + 1e-4f)
					{
						Spawn(state, s, emitter, burst.Count);
						done++;
					}
				}
			}
			if (s.Looping && s.Duration > 0.0f && state.Time >= s.Duration)
			{
				state.Time -= s.Duration;
				state.BurstCyclesDone.fill(0);
			}
		}
	}

	// ---- Curve, Gradient ----------------------------------------------------------------------------------------------

	float Curve::Evaluate(float t) const
	{
		if (Count <= 1)
			return Keys[0].Value;
		if (t <= Keys[0].T)
			return Keys[0].Value;
		for (uint32_t i = 1; i < Count; i++)
		{
			if (t <= Keys[i].T)
			{
				const float span = Keys[i].T - Keys[i - 1].T;
				const float f = span > 1e-6f ? (t - Keys[i - 1].T) / span : 1.0f;
				return Keys[i - 1].Value + (Keys[i].Value - Keys[i - 1].Value) * f;
			}
		}
		return Keys[Count - 1].Value;
	}

	bool Curve::Add(float t, float value)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		for (uint32_t i = 0; i < Count; i++)
		{
			if (std::abs(Keys[i].T - t) < 1e-5f)
			{
				Keys[i].Value = value;
				return true;
			}
		}
		if (Count >= MaxKeys)
			return false;
		uint32_t at = Count;
		while (at > 0 && Keys[at - 1].T > t)
		{
			Keys[at] = Keys[at - 1];
			at--;
		}
		Keys[at] = { t, value };
		Count++;
		return true;
	}

	bool Curve::operator==(const Curve& other) const
	{
		if (Count != other.Count)
			return false;
		for (uint32_t i = 0; i < Count; i++)
			if (Keys[i].T != other.Keys[i].T || Keys[i].Value != other.Keys[i].Value)
				return false;
		return true;
	}

	glm::vec4 Gradient::Evaluate(float t) const
	{
		if (Count <= 1 || t <= Keys[0].T)
			return Keys[0].Color;
		for (uint32_t i = 1; i < Count; i++)
		{
			if (t <= Keys[i].T)
			{
				const float span = Keys[i].T - Keys[i - 1].T;
				const float f = span > 1e-6f ? (t - Keys[i - 1].T) / span : 1.0f;
				return glm::mix(Keys[i - 1].Color, Keys[i].Color, f);
			}
		}
		return Keys[Count - 1].Color;
	}

	bool Gradient::Add(float t, const glm::vec4& color)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		for (uint32_t i = 0; i < Count; i++)
		{
			if (std::abs(Keys[i].T - t) < 1e-5f)
			{
				Keys[i].Color = color;
				return true;
			}
		}
		if (Count >= MaxKeys)
			return false;
		uint32_t at = Count;
		while (at > 0 && Keys[at - 1].T > t)
		{
			Keys[at] = Keys[at - 1];
			at--;
		}
		Keys[at] = { t, color };
		Count++;
		return true;
	}

	bool Gradient::operator==(const Gradient& other) const
	{
		if (Count != other.Count)
			return false;
		for (uint32_t i = 0; i < Count; i++)
			if (Keys[i].T != other.Keys[i].T || Keys[i].Color != other.Keys[i].Color)
				return false;
		return true;
	}

	bool Settings::operator==(const Settings& o) const
	{
		if (BurstCount != o.BurstCount)
			return false;
		for (uint32_t i = 0; i < std::min(BurstCount, MaxBursts); i++)
			if (!(Bursts[i] == o.Bursts[i]))
				return false;
		return Looping == o.Looping && Duration == o.Duration && Prewarm == o.Prewarm && Rate == o.Rate && MaxParticles == o.MaxParticles
			&& Lifetime == o.Lifetime && Speed == o.Speed && Size == o.Size && Rotation == o.Rotation && AngularVelocity == o.AngularVelocity
			&& EmitShape == o.EmitShape && ConeAngle == o.ConeAngle && Radius == o.Radius && BoxSize == o.BoxSize && Gravity == o.Gravity
			&& Drag == o.Drag && WorldSpace == o.WorldSpace && StartColor == o.StartColor && ColorOverLife == o.ColorOverLife
			&& SizeOverLife == o.SizeOverLife && SpeedOverLife == o.SpeedOverLife && TilesX == o.TilesX && TilesY == o.TilesY && Frames == o.Frames
			&& FrameCycles == o.FrameCycles && BlendMode == o.BlendMode && SoftDistance == o.SoftDistance && Mode == o.Mode;
	}

	// ---- Instance -----------------------------------------------------------------------------------------------------

	glm::vec4 Instance::GetColor() const
	{
		return { glm::unpackHalf2x16(ColorRG), glm::unpackHalf2x16(ColorBA) };
	}

	void Instance::SetColor(const glm::vec4& color)
	{
		ColorRG = glm::packHalf2x16({ color.r, color.g });
		ColorBA = glm::packHalf2x16({ color.b, color.a });
	}

	// ---- Simulation ---------------------------------------------------------------------------------------------------

	State MakeState(uint32_t seed)
	{
		State state;
		state.Rng = SplitMix(seed);
		if (state.Rng == 0)
			state.Rng = 0x9E3779B97F4A7C15ull;
		return state;
	}

	void Clear(State& state)
	{
		state.Position.clear();
		state.Velocity.clear();
		state.Age.clear();
		state.Lifetime.clear();
		state.Rotation.clear();
		state.AngularVelocity.clear();
		state.StartSize.clear();
		state.FrameSeed.clear();
		state.Time = 0.0f;
		state.StepRemainder = 0.0f;
		state.EmitAccumulator = 0.0f;
		state.BurstCyclesDone.fill(0);
		state.Dropped = 0;
		state.Started = false;
	}

	void Prewarm(State& state, const Settings& settings, const glm::mat4& emitter)
	{
		if (!settings.Looping || settings.Duration <= 0.0f)
			return;
		const uint32_t steps = std::min<uint32_t>(static_cast<uint32_t>(std::ceil(settings.Duration / FixedStep)), 1800);
		for (uint32_t i = 0; i < steps; i++)
			StepOnce(state, settings, emitter);
	}

	void Step(State& state, const Settings& settings, const glm::mat4& emitter, float dt)
	{
		if (!state.Started)
		{
			state.Started = true;
			if (settings.Prewarm)
				Prewarm(state, settings, emitter);
		}
		if (!state.Playing)
			return;

		state.StepRemainder += std::clamp(dt, 0.0f, 0.25f);
		while (state.StepRemainder >= FixedStep)
		{
			state.StepRemainder -= FixedStep;
			StepOnce(state, settings, emitter);
		}
	}

	void Emit(State& state, const Settings& settings, const glm::mat4& emitter, uint32_t count)
	{
		Spawn(state, settings, emitter, count);
	}

	glm::vec4 FrameUV(uint32_t tilesX, uint32_t tilesY, uint32_t frame)
	{
		tilesX = std::max(tilesX, 1u);
		tilesY = std::max(tilesY, 1u);
		frame %= tilesX * tilesY;
		const uint32_t column = frame % tilesX;
		const uint32_t row = frame / tilesX;
		return { static_cast<float>(column) / static_cast<float>(tilesX), 1.0f - static_cast<float>(row + 1) / static_cast<float>(tilesY),
			static_cast<float>(column + 1) / static_cast<float>(tilesX), 1.0f - static_cast<float>(row) / static_cast<float>(tilesY) };
	}

	void Fill(const State& state, const Settings& s, const glm::mat4& emitter, const glm::vec3& viewPosition, std::vector<Instance>& out)
	{
		const size_t count = state.Age.size();
		if (count == 0)
			return;

		std::vector<uint32_t> order(count);
		std::iota(order.begin(), order.end(), 0u);

		const auto worldPosition = [&](size_t i) { return s.WorldSpace ? state.Position[i] : glm::vec3(emitter * glm::vec4(state.Position[i], 1.0f)); };
		if (s.Mode == RenderMode::Sprite2D)
		{
			std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return state.Age[a] > state.Age[b]; });
		}
		else if (s.BlendMode != Blend::Additive)
		{
			std::vector<float> distance(count);
			for (size_t i = 0; i < count; i++)
				distance[i] = glm::dot(worldPosition(i) - viewPosition, worldPosition(i) - viewPosition);
			std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return distance[a] > distance[b]; });
		}

		const uint32_t frames = std::max(s.TilesX, 1u) * std::max(s.TilesY, 1u);
		out.reserve(out.size() + count);
		for (const uint32_t i : order)
		{
			const float t = SafeRatio(state.Age[i], state.Lifetime[i]);
			Instance instance;
			instance.Position = worldPosition(i);
			instance.Rotation = state.Rotation[i];
			const float size = state.StartSize[i] * std::max(0.0f, s.SizeOverLife.Evaluate(t));
			instance.Size = { size, size };
			instance.SetColor(s.StartColor * s.ColorOverLife.Evaluate(t));
			if (frames > 1)
			{
				const uint32_t frame = s.Frames == FrameMode::Random ? state.FrameSeed[i] % frames
					: static_cast<uint32_t>(std::max(0.0f, t * s.FrameCycles * static_cast<float>(frames))) % frames;
				instance.UVRect = FrameUV(s.TilesX, s.TilesY, frame);
			}
			out.push_back(instance);
		}
	}
}
