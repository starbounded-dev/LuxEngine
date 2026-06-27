#pragma once

#include <cstdint>
#include <functional>

namespace Lux {

	// A small fork-join worker pool for data-parallel engine work (transform/culling/animation
	// fan-out, etc.). It is intentionally separate from the RenderThread (which owns GPU submission)
	// and from Jolt's internal physics job pool.
	//
	// The pool honors the application ThreadingPolicy: when initialized with zero workers
	// (SingleThreaded policy) every job runs inline on the calling thread, so toggling the policy
	// disables all worker parallelism rather than just the render thread.
	class JobSystem
	{
	public:
		// workerCount == 0 -> inline mode (no threads spawned; all work runs on the caller).
		static void Init(uint32_t workerCount);
		static void Shutdown();

		static bool IsMultithreaded();
		static uint32_t GetWorkerCount();

		// Fire-and-forget job. Runs inline when there are no workers.
		static void Submit(std::function<void()> job);

		// Parallel for over [0, count). Splits the range into chunks shared across the worker pool
		// and the calling thread, then blocks until every element has been processed. Runs fully
		// inline when single-threaded or when count <= minChunk. fn must be safe to run concurrently
		// across disjoint indices (no shared writes without synchronization).
		static void ParallelFor(size_t count, const std::function<void(size_t)>& fn, size_t minChunk = 1);
	};

}
