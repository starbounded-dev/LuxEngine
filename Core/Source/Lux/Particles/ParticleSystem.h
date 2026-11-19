// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev

#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

// Particles (docs/PARTICLES_PLAN.md). The simulation is pure code over plain data: no registry, no GPU, any thread, so it is
// checked headless. A scene owns one State per particle component and steps it with fixed 1/60 s steps from a seeded random
// generator, so the same settings, seed and elapsed time always give the same particles.
namespace Lux::Particles
{
	// A value over a particle's life (t from 0 to 1) as up to eight keys joined by straight lines.
	struct Curve
	{
		static constexpr uint32_t MaxKeys = 8;
		struct Key { float T = 0.0f; float Value = 1.0f; };

		std::array<Key, MaxKeys> Keys{};
		uint32_t Count = 1;

		Curve() = default;
		explicit Curve(float constant) { Keys[0] = { 0.0f, constant }; }
		static Curve Linear(float from, float to) { Curve c; c.Keys[0] = { 0.0f, from }; c.Keys[1] = { 1.0f, to }; c.Count = 2; return c; }

		float Evaluate(float t) const;
		// Adds a key in time order; false when full. Keys at the same time replace each other.
		bool Add(float t, float value);
		// Removes a key; false for the only key.
		bool Remove(uint32_t index);
		bool operator==(const Curve& other) const;
	};

	// A color (with alpha) over a particle's life.
	struct Gradient
	{
		static constexpr uint32_t MaxKeys = 8;
		struct Key { float T = 0.0f; glm::vec4 Color{ 1.0f }; };

		std::array<Key, MaxKeys> Keys{};
		uint32_t Count = 1;

		Gradient() = default;
		explicit Gradient(const glm::vec4& constant) { Keys[0] = { 0.0f, constant }; }
		static Gradient Fade(const glm::vec4& color) { Gradient g; g.Keys[0] = { 0.0f, color }; g.Keys[1] = { 1.0f, { color.r, color.g, color.b, 0.0f } }; g.Count = 2; return g; }

		glm::vec4 Evaluate(float t) const;
		bool Add(float t, const glm::vec4& color);
		bool Remove(uint32_t index);
		bool operator==(const Gradient& other) const;
	};

	struct Range
	{
		float Min = 1.0f;
		float Max = 1.0f;
		bool operator==(const Range&) const = default;
	};

	enum class Shape : uint8_t { Cone = 0, Sphere, Box };
	enum class Blend : uint8_t { Alpha = 0, Additive, Premultiplied };
	enum class RenderMode : uint8_t { Billboard3D = 0, Sprite2D };
	enum class FrameMode : uint8_t { OverLife = 0, Random };

	// Particles released at one moment, repeated Cycles times Interval seconds apart.
	struct Burst
	{
		float Time = 0.0f;
		uint16_t Count = 10;
		uint16_t Cycles = 1;
		float Interval = 0.1f;
		bool operator==(const Burst&) const = default;
	};

	struct Settings
	{
		static constexpr uint32_t MaxBursts = 4;
		static constexpr uint32_t HardMaxParticles = 20000;

		bool Looping = true;
		float Duration = 5.0f;
		bool Prewarm = false;

		float Rate = 20.0f; // particles per second
		std::array<Burst, MaxBursts> Bursts{};
		uint32_t BurstCount = 0;
		uint32_t MaxParticles = 1000;

		Range Lifetime{ 1.0f, 2.0f };   // seconds
		Range Speed{ 2.0f, 4.0f };      // units per second
		Range Size{ 0.2f, 0.3f };       // units
		Range Rotation{ 0.0f, 0.0f };   // degrees at birth
		Range AngularVelocity{ 0.0f, 0.0f }; // degrees per second

		Shape EmitShape = Shape::Cone;
		float ConeAngle = 25.0f;        // degrees from +Y (180 is every direction)
		float Radius = 0.0f;            // cone base radius, sphere radius
		glm::vec3 BoxSize{ 1.0f };

		glm::vec3 Gravity{ 0.0f, -9.81f, 0.0f };
		float Drag = 0.0f;              // 1/s
		bool WorldSpace = true;         // particles stay where they were released when the emitter moves

		glm::vec4 StartColor{ 1.0f };
		Gradient ColorOverLife = Gradient::Fade(glm::vec4(1.0f));
		Curve SizeOverLife = Curve(1.0f);
		Curve SpeedOverLife = Curve(1.0f);

		// A texture sheet of TilesX by TilesY frames, frame 0 at the top left.
		uint32_t TilesX = 1;
		uint32_t TilesY = 1;
		FrameMode Frames = FrameMode::OverLife;
		float FrameCycles = 1.0f;       // OverLife: times the sheet plays during a life

		Blend BlendMode = Blend::Alpha;
		float SoftDistance = 0.5f;      // 3D: units over which a particle fades out against geometry
		RenderMode Mode = RenderMode::Billboard3D;

		bool operator==(const Settings&) const;
	};

	// What the renderer draws for one particle: 48 bytes, the vertex-stream layout of the 3D pass.
	struct Instance
	{
		glm::vec3 Position{ 0.0f }; // world space
		float Rotation = 0.0f;      // radians
		glm::vec2 Size{ 0.0f };
		uint32_t ColorRG = 0;       // half floats: red, green
		uint32_t ColorBA = 0;       // half floats: blue, alpha
		glm::vec4 UVRect{ 0.0f, 0.0f, 1.0f, 1.0f }; // u0 v0 u1 v1, v up

		glm::vec4 GetColor() const;
		void SetColor(const glm::vec4& color);
	};
	static_assert(sizeof(Instance) == 48, "Instance is a vertex stream: keep it 48 bytes");

	struct State
	{
		// One entry per live particle (structure of arrays; a dead one is replaced by the last).
		std::vector<glm::vec3> Position;
		std::vector<glm::vec3> Velocity;
		std::vector<float> Age;
		std::vector<float> Lifetime;
		std::vector<float> Rotation;        // radians
		std::vector<float> AngularVelocity; // radians per second
		std::vector<float> StartSize;
		std::vector<uint32_t> FrameSeed;    // picks the frame in Random mode

		uint64_t Rng = 0;
		float Time = 0.0f;           // seconds into the emitter's duration
		float StepRemainder = 0.0f;  // time not yet covered by a fixed step
		float EmitAccumulator = 0.0f;
		std::array<uint16_t, Settings::MaxBursts> BurstCyclesDone{};
		bool Playing = true;         // false: paused, nothing moves or ages
		bool Emitting = true;        // false: nothing is released by rate or bursts (the particles in the air live out their life)
		bool Started = false;        // the first step has run (prewarm happens once)
		uint32_t Dropped = 0;        // releases refused by the particle limit since the last Clear

		uint32_t Count() const { return static_cast<uint32_t>(Age.size()); }
	};

	constexpr float FixedStep = 1.0f / 60.0f;

	// A new state whose random generator starts from the seed.
	State MakeState(uint32_t seed);

	// Removes every particle and rewinds the emitter. The seed is kept as the generator's current value.
	void Clear(State& state);

	// Advances by dt seconds in fixed steps (a long frame is cut to 0.25 s). `emitter` is the emitter's world matrix.
	void Step(State& state, const Settings& settings, const glm::mat4& emitter, float dt);

	// Starts releasing again, from the start of the emitter's duration when it had stopped or a one-shot had ended, and
	// resumes a paused system.
	void Play(State& state, const Settings& settings);

	// Stops releasing; the particles in the air live out their life.
	void Stop(State& state);

	// Releases count particles now, whatever the rate says (a script's Emit).
	void Emit(State& state, const Settings& settings, const glm::mat4& emitter, uint32_t count);

	// Runs a looping system through one duration so it starts full. Called once, from the first step.
	void Prewarm(State& state, const Settings& settings, const glm::mat4& emitter);

	// The particles to draw. A 3D alpha system sorts back to front from viewPosition; a 2D system draws older particles
	// first; an additive system is not sorted. Appends to `out`.
	void Fill(const State& state, const Settings& settings, const glm::mat4& emitter, const glm::vec3& viewPosition, std::vector<Instance>& out);

	// The texture rectangle of frame `frame` of a TilesX by TilesY sheet, frame 0 at the top left, v up.
	glm::vec4 FrameUV(uint32_t tilesX, uint32_t tilesY, uint32_t frame);
}
