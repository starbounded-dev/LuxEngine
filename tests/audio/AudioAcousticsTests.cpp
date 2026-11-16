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
	struct MeshOwner : AudioAcoustics::MeshGeometry
	{
		std::atomic<bool>* Destroyed = nullptr;
		~MeshOwner() override
		{
			if (Destroyed)
				Destroyed->store(true);
		}
	};
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
	// Threaded policy: a solve batch publishes on a later update. Poll (never join) until it has.
	template<typename Sources>
	bool SettledUpdate(AudioAcoustics& audio, float timestep, Soundwave::Position listener, const Sources& sources)
	{
		const bool updated = audio.Update(timestep, listener, std::span<const AudioAcoustics::Source>(sources));
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (audio.GetDiagnostics().SolvesPending)
		{
			audio.PollSolves();
			assert(std::chrono::steady_clock::now() < deadline && "Soundwave solve worker did not finish");
			std::this_thread::yield();
		}
		return updated;
	}
}

void RunAudioAcousticsTests()
{
	using namespace Lux;
	using namespace std::chrono_literals;
	AudioOcclusionSettings settings;
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
		assert(SettledUpdate(audio, 0.01f, listener, sources) && !audio.GetDiagnostics().Ready && !audio.GetResult(100));
		assert(audio.RequestGeometry(2, {}, [&](auto&, auto&) { ++skippedBuilds; return true; }));
		assert(audio.RequestGeometry(3, {}, [&](auto&, auto&) { ++latestBuilds; return true; }));
		gate->Release.release();
		Drain(audio);
		assert(audio.GetDiagnostics().Ready && fixedBuilds == 1 && skippedBuilds == 0 && latestBuilds == 1);
		assert(audio.GetDiagnostics().StaticTriangles == 12 && audio.GetDiagnostics().MovingTriangles == 0);
		assert(SettledUpdate(audio, 0.01f, listener, sources));
		assert(audio.GetResult(100) && !audio.GetResult(101) && !audio.GetResult(102));
		assert(audio.GetGainLF(100) <= 0.001001f);
		assert(SettledUpdate(audio, 0, listener, sources) && audio.GetResult(101) && !audio.GetResult(102));
		assert(SettledUpdate(audio, 0, listener, sources) && audio.GetResult(102));
		const auto evaluations = audio.GetDiagnostics().Evaluations;
		assert(SettledUpdate(audio, 0.01f, listener, sources) && audio.GetDiagnostics().Evaluations == evaluations);
		assert(audio.SetActiveInstances({}));
		assert(SettledUpdate(audio, 0, listener, sources) && audio.GetResult(100)->OcclusionGainLF == 1 && audio.GetGainLF(100) < 0.01f);
		assert(SettledUpdate(audio, 0.05f, listener, sources) && audio.GetGainLF(100) > 0.001f && audio.GetGainLF(100) < 1);
		assert(audio.SetActiveInstances(live));
		// Migration is one publication. Same UUID in old static and new moving storage
		// must never be queried together or charged twice.
		assert(audio.RequestGeometry(4, empty, [](auto& input, auto&) { Wall(input); return true; }));
		Drain(audio);
		assert(audio.GetDiagnostics().StaticTriangles == 0 && audio.GetDiagnostics().MovingTriangles == 12);
		assert(SettledUpdate(audio, 0.1f, listener, std::span(sources).first(1)) && audio.GetResult(100)->Direct.Crossings.size() == 1);
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
		assert(SettledUpdate(audio, 0.1f, listener, std::span(sources).first(1)));
		const auto savedDistance = audio.GetResult(100)->Direct.Distance;
		sources[0].Position = {std::numeric_limits<float>::max(),0,0};
		assert(SettledUpdate(audio, 0.1f, listener, std::span(sources).first(1)) && audio.GetDiagnostics().SolveFailures == 1 &&
			audio.GetResult(100)->Direct.Distance == savedDistance);
		sources[0].Position = {2,0,0};
		assert(SettledUpdate(audio, 0.1f, listener, std::span(sources).first(1)));
		auto invalid = settings;
		invalid.CandidateBudget = 0;
		assert(!audio.Configure(materials, invalid));
		assert(!SettledUpdate(audio, -1, listener, sources));
		sources[1].Entity = 100;
		assert(!SettledUpdate(audio, 0.1f, listener, sources));
		const std::array<uint64_t, 2> duplicate{10,10};
		assert(!audio.SetActiveInstances(duplicate));
		assert(!audio.RequestGeometry(6, empty, empty));
		const std::array<AudioAcoustics::Source, 1> invalidSource{{{103,{std::numeric_limits<float>::max(),0,0}}}};
		assert(SettledUpdate(audio, 0.1f, listener, invalidSource));
		const auto invalidAttempts = audio.GetDiagnostics().Evaluations;
		assert(!audio.GetResult(103) && SettledUpdate(audio, 0, listener, invalidSource) && audio.GetDiagnostics().Evaluations == invalidAttempts);

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
	{
		std::atomic<bool> destroyed{false};
		AudioAcoustics audio;
		auto owner = Ref<MeshOwner>::Create();
		owner->Destroyed = &destroyed;
		AudioAcoustics::GeometryInput raw;
		Wall(raw);
		assert(owner->Grid.Build(raw.Triangles) == Soundwave::GeometryError::None);
		auto gate = Ref<Gate>::Create();
		assert(audio.RequestGeometry(1, [owner,gate](auto& input, auto&) mutable
		{
			gate->Started.release();
			gate->Release.acquire();
			AudioAcoustics::MeshInstance instance;
			instance.Geometry = owner;
			instance.Description.Instance = 10;
			input.Meshes.push_back(std::move(instance));
			return true;
		}, empty));
		audio.PollGeometry();
		assert(gate->Started.try_acquire_for(5s));
		owner = nullptr;
		audio.Clear();
		assert(!destroyed && !audio.GetDiagnostics().Ready);
		gate->Release.release();
		JobSystem::Submit([gate]() mutable { gate->Finished.release(); });
		assert(gate->Finished.try_acquire_for(5s) && destroyed);
		audio.PollGeometry();
		assert(!audio.GetDiagnostics().Ready);
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
		// One slab in open space: the reverb measurement publishes on the next update and stays mostly dry.
		assert(audio.Update(0.1f, listener, std::span(sources).first(1)) && audio.GetReverb() &&
			audio.GetDiagnostics().ReverbMeasurements == 1 && audio.GetReverbSend() < 0.2f);
		// Inside a closed room the same material rings, and the send follows.
		assert(audio.RequestGeometry(2, [](auto& input, auto&)
		{
			constexpr int indices[] = {0,2,3, 0,3,1, 4,5,7, 4,7,6, 0,4,6, 0,6,2, 1,3,7, 1,7,5, 0,1,5, 0,5,4, 2,6,7, 2,7,3};
			const auto point = [](int i) { return Soundwave::Position{i & 1 ? 5.0f : -5.0f,i & 2 ? 4.0f : -1.0f,i & 4 ? 5.0f : -5.0f}; };
			for (int i = 0; i < 36; i += 3)
				input.Triangles.push_back({point(indices[i]),point(indices[i+1]),point(indices[i+2]),10,0,0,false});
			return true;
		}, {}));
		Drain(audio);
		assert(audio.Update(0.1f, listener, {}) && audio.Update(10.0f, listener, {}) &&
			audio.GetDiagnostics().ReverbMeasurements == 2 && audio.GetReverb()->EscapedFraction == 0 && audio.GetReverbSend() > 0.2f);
		audio.Clear();
		assert(!audio.GetReverb() && audio.GetReverbSend() == 0);
	}
	{
		// A reverb backend gets the flattened current geometry and its returned rays are reduced on
		// main; one that never returns is reported as stalled instead of waited on.
		struct FakeBackend : AudioAcoustics::ReverbBackend
		{
			uint32_t Dispatches = 0, Polls = 0, Triangles = 0;
			bool Returns = true;
			bool Dispatch(const Ref<Geometry>& geometry, Soundwave::Position, const Soundwave::Settings&, const Soundwave::MaterialTable&) override
			{
				++Dispatches;
				Triangles = static_cast<uint32_t>(geometry->Flat.Triangles.size());
				Polls = 0;
				return true;
			}
			bool Poll(std::vector<Soundwave::ReverbRay>& rays) override
			{
				if (!Returns || ++Polls < 3)
					return false;
				rays.assign(256, { 0.3f, 4.0f, 1, 2, 0 }); // a closed room: no ray escapes
				return true;
			}
		};
		auto backend = Ref<FakeBackend>::Create();
		AudioAcoustics audio;
		assert(audio.Configure(materials, settings));
		audio.SetReverbBackend(backend);
		assert(audio.RequestGeometry(1, [](auto& input, auto&) { Wall(input); return true; }, empty));
		Drain(audio);
		for (int i = 0; i < 6; ++i)
			assert(audio.Update(0.1f, listener, {}));
		Soundwave::ReverbResult expected;
		const std::vector<Soundwave::ReverbRay> rays(256, { 0.3f, 4.0f, 1, 2, 0 });
		assert(Soundwave::FinishReverb(rays, settings.GetKernelSettings(), expected) == Soundwave::EvaluationError::None);
		assert(backend->Dispatches == 1 && backend->Triangles == 12 && audio.GetReverb() &&
			audio.GetReverb()->Send == expected.Send && audio.GetDiagnostics().ReverbOnGpu && !audio.GetDiagnostics().ReverbStalled);
		backend->Returns = false;
		assert(audio.RequestGeometry(2, [](auto& input, auto&) { Wall(input); return true; }, {}));
		Drain(audio);
		for (int i = 0; i < 40; ++i)
			assert(audio.Update(0.1f, listener, {}));
		assert(backend->Dispatches == 2 && audio.GetDiagnostics().ReverbStalled && audio.GetReverb()->Send == expected.Send);
	}
	{
		std::atomic<bool> destroyed{false};
		AudioAcoustics audio;
		assert(audio.Configure(materials, settings));
		auto owner = Ref<MeshOwner>::Create();
		owner->Destroyed = &destroyed;
		AudioAcoustics::GeometryInput raw;
		Wall(raw);
		assert(owner->Grid.Build(raw.Triangles) == Soundwave::GeometryError::None);
		const std::array<uint64_t,2> instances{10,20};
		assert(audio.SetActiveInstances(instances));
		assert(audio.RequestGeometry(1, [owner](auto& input, auto&)
		{
			for (uint32_t i = 0; i < 2; ++i)
			{
				AudioAcoustics::MeshInstance instance;
				instance.Geometry = owner;
				instance.Description.Instance = i ? 20 : 10;
				instance.Description.LocalToWorld[12] = static_cast<float>(i)*8;
				input.Meshes.push_back(std::move(instance));
			}
			return true;
		}, empty));
		owner = nullptr;
		Drain(audio);
		assert(!destroyed && audio.GetDiagnostics().Ready && audio.GetDiagnostics().StaticTriangles == 24 &&
			audio.GetDiagnostics().StaticStoredTriangles == 12 && audio.GetDiagnostics().StaticMeshInstances == 2);
		assert(audio.Update(0.1f,listener,std::span(sources).first(1)) && audio.GetGainLF(100) <= 0.001001f);
		// Invalid instance replacement retains the published geometry and its mesh owner.
		assert(audio.RequestGeometry(2, [](auto& input, auto&)
		{
			input.Meshes.emplace_back();
			return true;
		}, {}));
		Drain(audio);
		assert(audio.GetDiagnostics().GeometryFailed && !destroyed && audio.GetDiagnostics().StaticTriangles == 24);
		assert(audio.RequestGeometry(3, empty, {}));
		Drain(audio);
		assert(!audio.GetDiagnostics().GeometryFailed && audio.GetDiagnostics().StaticTriangles == 0 && destroyed);
	}
	JobSystem::Shutdown();
	std::cout << "PASS: Soundwave worker ownership, nonblocking publication/clear, coalescing, layer migration, retries, source cadence/budget/smoothing, last-valid results, shared-mesh storage and detached mesh ownership under both job policies\n";
}
