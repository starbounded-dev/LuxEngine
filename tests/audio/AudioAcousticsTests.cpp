// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2025-2026 starbounded-dev
#include "lpch.h"
#include "Lux/Audio/AudioAcoustics.h"
#include "Lux/Core/JobSystem.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <limits>
#include <semaphore>
#include <thread>

namespace
{
	using namespace Lux;
	using namespace std::chrono_literals;
	struct Gate : RefCounted
	{
		std::binary_semaphore Started{0}, Release{0}, Finished{0};
	};
	void Wall(AudioAcoustics::GeometryInput& input)
	{
		constexpr int indices[] = {0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,4,6, 0,6,2,
			1,3,7, 1,7,5, 0,1,5, 0,5,4, 2,6,7, 2,7,3};
		const auto point = [](int i) { return Soundwave::Position{i & 1 ? 0.1f : -0.1f,i & 2 ? 2.0f : -2.0f,i & 4 ? 2.0f : -2.0f}; };
		for (int i = 0; i < 36; i += 3)
			input.Triangles.push_back({point(indices[i]),point(indices[i+1]),point(indices[i+2]),10,0,0,false});
	}
	void Drain(AudioAcoustics& audio)
	{
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		do
		{
			audio.PollGeometry();
			if (!audio.GetDiagnostics().PendingLayers)
				return;
			std::this_thread::yield();
		} while (std::chrono::steady_clock::now() < deadline);
		assert(false && "Soundwave worker did not finish");
	}
}

void RunAudioAcousticsTests()
{
	using namespace Lux;
	using namespace std::chrono_literals;
	AudioOcclusionSettings settings;
	settings.Source = AudioOcclusionSource::Raytraced; // legacy selector does not choose CPU reference output
	settings.ReflectionDepth = 0;
	settings.OpeningGridSize = 1;
	settings.MaxBounces = 1;
	settings.CastBudget = 1;
	AcousticMaterialSettings materials;
	materials.Overrides[0].Enabled = true;
	materials.Overrides[0].Properties = {0.1f,0.5f,0.1f,0.1f,0.05f,0.9f,0.99f};
	const auto empty = [](auto&, auto&) { return true; };
	const std::array<uint64_t, 1> live{10};
	std::array<AudioAcoustics::Source, 3> sources{{{100,{2,0,0}},{101,{2,0,0}},{102,{2,0,0}}}};
	const Soundwave::Position listener{-2,0,0};
	JobSystem::Init(1);
	{
		AudioAcoustics audio;
		assert(audio.Configure(materials, settings));
		assert(audio.SetActiveInstances(live));
		std::atomic<int> fixedBuilds{0}, skippedBuilds{0}, latestBuilds{0};
		auto gate = Ref<Gate>::Create();
		assert(audio.RequestGeometry(1, [gate,&fixedBuilds](auto& input, auto&) mutable
		{
			++fixedBuilds;
			gate->Started.release();
			gate->Release.acquire();
			Wall(input);
			return true;
		}, empty));
		audio.PollGeometry();
		assert(gate->Started.try_acquire_for(5s));
		// Worker is blocked and cannot finish. Poll/Update must return without joining it.
		audio.PollGeometry();
		assert(audio.Update(0.01f, listener, sources) && !audio.GetDiagnostics().Ready && !audio.GetResult(100));
		assert(audio.RequestGeometry(2, {}, [&](auto&, auto&) { ++skippedBuilds; return true; }));
		assert(audio.RequestGeometry(3, {}, [&](auto&, auto&) { ++latestBuilds; return true; }));
		gate->Release.release();
		Drain(audio);
		assert(audio.GetDiagnostics().Ready && fixedBuilds == 1 && skippedBuilds == 0 && latestBuilds == 1);
		assert(audio.GetDiagnostics().StaticTriangles == 12 && audio.GetDiagnostics().MovingTriangles == 0);
		assert(audio.Update(0.01f, listener, sources));
		assert(audio.GetResult(100) && !audio.GetResult(101) && !audio.GetResult(102));
		assert(audio.GetGainLF(100) <= 0.001001f);
		assert(audio.Update(0, listener, sources) && audio.GetResult(101) && !audio.GetResult(102));
		assert(audio.Update(0, listener, sources) && audio.GetResult(102));
		const auto evaluations = audio.GetDiagnostics().Evaluations;
		assert(audio.Update(0.01f, listener, sources) && audio.GetDiagnostics().Evaluations == evaluations);
		assert(audio.SetActiveInstances({}));
		assert(audio.Update(0, listener, sources) && audio.GetResult(100)->OcclusionGainLF == 1 && audio.GetGainLF(100) < 0.01f);
		assert(audio.Update(0.05f, listener, sources) && audio.GetGainLF(100) > 0.001f && audio.GetGainLF(100) < 1);
		assert(audio.SetActiveInstances(live));
		// Migration is one publication. Same UUID in old static and new moving storage
		// must never be queried together or charged twice.
		assert(audio.RequestGeometry(4, empty, [](auto& input, auto&) { Wall(input); return true; }));
		Drain(audio);
		assert(audio.GetDiagnostics().StaticTriangles == 0 && audio.GetDiagnostics().MovingTriangles == 12);
		assert(audio.Update(0.1f, listener, std::span(sources).first(1)) && audio.GetResult(100)->Direct.Crossings.size() == 1);
		assert(!audio.GetResult(101) && audio.GetGainLF(102) == 1);
		std::atomic<int> failures{0};
		assert(audio.RequestGeometry(5, [&](auto&, auto& error) { ++failures; error = "intentional test failure"; return false; }, {}));
		Drain(audio);
		assert(audio.GetDiagnostics().Ready && audio.GetDiagnostics().GeometryFailed && failures == 1);
		for (int i = 0; i < 10; ++i)
			audio.PollGeometry();
		assert(failures == 1 && audio.GetDiagnostics().MovingTriangles == 12);
		// Unrelated movement must not keep rebuilding/logging the same invalid room.
		assert(audio.RequestGeometry(6, {}, empty));
		Drain(audio);
		assert(audio.GetDiagnostics().GeometryFailed && failures == 1 && audio.GetDiagnostics().MovingTriangles == 12);
		assert(audio.RequestGeometry(7, empty, {}));
		Drain(audio);
		assert(!audio.GetDiagnostics().GeometryFailed && audio.GetDiagnostics().MovingTriangles == 0);
		assert(audio.Update(0.1f, listener, std::span(sources).first(1)));
		const auto savedDistance = audio.GetResult(100)->Direct.Distance;
		sources[0].Position = {std::numeric_limits<float>::max(),0,0};
		assert(audio.Update(0.1f, listener, std::span(sources).first(1)) && audio.GetDiagnostics().SolveFailures == 1 &&
			audio.GetResult(100)->Direct.Distance == savedDistance);
		sources[0].Position = {2,0,0};
		assert(audio.Update(0.1f, listener, std::span(sources).first(1)));
		auto invalid = settings;
		invalid.CandidateBudget = 0;
		assert(!audio.Configure(materials, invalid));
		assert(!audio.Update(-1, listener, sources));
		sources[1].Entity = 100;
		assert(!audio.Update(0.1f, listener, sources));
		const std::array<uint64_t, 2> duplicate{10,10};
		assert(!audio.SetActiveInstances(duplicate));
		assert(!audio.RequestGeometry(6, empty, empty));
		const std::array<AudioAcoustics::Source, 1> invalidSource{{{103,{std::numeric_limits<float>::max(),0,0}}}};
		assert(audio.Update(0.1f, listener, invalidSource));
		const auto invalidAttempts = audio.GetDiagnostics().Evaluations;
		assert(!audio.GetResult(103) && audio.Update(0, listener, invalidSource) && audio.GetDiagnostics().Evaluations == invalidAttempts);

		const auto wall = [](auto& input, auto&) { Wall(input); return true; };
		assert(audio.RequestGeometry(8, wall, wall));
		Drain(audio);
		assert(audio.GetDiagnostics().GeometryFailed && audio.GetDiagnostics().StaticTriangles == 0 && audio.GetDiagnostics().MovingTriangles == 0);
		// Fixing either side of a cross-layer identity conflict can unblock publication.
		assert(audio.RequestGeometry(9, {}, empty));
		Drain(audio);
		assert(!audio.GetDiagnostics().GeometryFailed && audio.GetDiagnostics().StaticTriangles == 12 && audio.GetDiagnostics().MovingTriangles == 0);
	}
	{
		AudioAcoustics audio;
		auto gate = Ref<Gate>::Create();
		assert(audio.RequestGeometry(1, [gate](auto& input, auto&) mutable
		{
			gate->Started.release();
			gate->Release.acquire();
			Wall(input);
			return true;
		}, empty));
		audio.PollGeometry();
		assert(gate->Started.try_acquire_for(5s));
		audio.Clear(); // Must return while the captured worker is still blocked.
		assert(!audio.GetDiagnostics().Ready && !audio.GetDiagnostics().PendingLayers);
		gate->Release.release();
		JobSystem::Submit([gate]() mutable { gate->Finished.release(); });
		assert(gate->Finished.try_acquire_for(5s));
		audio.PollGeometry();
		assert(!audio.GetDiagnostics().Ready); // detached old job cannot publish into a new scene
		assert(audio.RequestGeometry(1, empty, empty));
		Drain(audio);
		assert(audio.GetDiagnostics().Ready && audio.GetDiagnostics().StaticTriangles == 0);
	}
	JobSystem::Shutdown();
	JobSystem::Init(0);
	{
		AudioAcoustics audio;
		assert(audio.Configure(materials, settings));
		assert(audio.RequestGeometry(1, [](auto& input, auto&) { Wall(input); return true; }, empty));
		Drain(audio);
		assert(audio.GetDiagnostics().Ready && audio.Update(0.1f, listener, std::span(sources).first(1)));
		assert(audio.GetGainLF(100) <= 0.001001f);
	}
	JobSystem::Shutdown();
	std::cout << "PASS: Soundwave worker ownership, nonblocking publication/clear, coalescing, layer migration, retries, source cadence/budget/smoothing and last-valid results under both job policies\n";
}
