#include "lpch.h"
#include "JobSystem.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Lux {

	namespace {

		struct JobSystemData
		{
			std::vector<std::thread> Workers;
			std::queue<std::function<void()>> Tasks;
			std::mutex QueueMutex;
			std::condition_variable Condition;
			bool Running = true;
		};

		JobSystemData* s_Data = nullptr;
		uint32_t s_WorkerCount = 0;

		void WorkerLoop()
		{
			for (;;)
			{
				std::function<void()> task;
				{
					std::unique_lock<std::mutex> lock(s_Data->QueueMutex);
					s_Data->Condition.wait(lock, [] { return !s_Data->Tasks.empty() || !s_Data->Running; });

					if (!s_Data->Running && s_Data->Tasks.empty())
						return;

					task = std::move(s_Data->Tasks.front());
					s_Data->Tasks.pop();
				}
				task();
			}
		}

	}

	void JobSystem::Init(uint32_t workerCount)
	{
		LUX_CORE_ASSERT(s_Data == nullptr, "JobSystem already initialized");

		s_WorkerCount = workerCount;
		if (workerCount == 0)
			return; // Inline mode (single-threaded policy): no pool, all work runs on the caller.

		s_Data = new JobSystemData();
		s_Data->Workers.reserve(workerCount);
		for (uint32_t i = 0; i < workerCount; i++)
			s_Data->Workers.emplace_back([]() { WorkerLoop(); });
	}

	void JobSystem::Shutdown()
	{
		if (!s_Data)
		{
			s_WorkerCount = 0;
			return;
		}

		{
			std::unique_lock<std::mutex> lock(s_Data->QueueMutex);
			s_Data->Running = false;
		}
		s_Data->Condition.notify_all();

		for (std::thread& worker : s_Data->Workers)
		{
			if (worker.joinable())
				worker.join();
		}

		delete s_Data;
		s_Data = nullptr;
		s_WorkerCount = 0;
	}

	bool JobSystem::IsMultithreaded()
	{
		return s_Data != nullptr;
	}

	uint32_t JobSystem::GetWorkerCount()
	{
		return s_WorkerCount;
	}

	void JobSystem::Submit(std::function<void()> job)
	{
		if (!s_Data)
		{
			job();
			return;
		}

		{
			std::unique_lock<std::mutex> lock(s_Data->QueueMutex);
			s_Data->Tasks.push(std::move(job));
		}
		s_Data->Condition.notify_one();
	}

	void JobSystem::ParallelFor(size_t count, const std::function<void(size_t)>& fn, size_t minChunk)
	{
		if (count == 0)
			return;

		minChunk = std::max<size_t>(1, minChunk);

		// Inline path: single-threaded policy, or a range too small to be worth splitting.
		if (!s_Data || count <= minChunk)
		{
			for (size_t i = 0; i < count; i++)
				fn(i);
			return;
		}

		// Split across the pool plus the calling thread (which also processes a chunk).
		const size_t maxChunks = s_WorkerCount + 1;
		const size_t chunk = std::max(minChunk, (count + maxChunks - 1) / maxChunks);
		const size_t chunkCount = (count + chunk - 1) / chunk;

		std::atomic<size_t> remaining{ chunkCount };
		std::mutex doneMutex;
		std::condition_variable doneCv;

		auto runChunk = [&](size_t begin, size_t end)
		{
			for (size_t i = begin; i < end; i++)
				fn(i);

			if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
			{
				std::unique_lock<std::mutex> lock(doneMutex);
				doneCv.notify_one();
			}
		};

		// Dispatch chunks [1, chunkCount) to the pool; the calling thread runs chunk 0.
		for (size_t c = 1; c < chunkCount; c++)
		{
			const size_t begin = c * chunk;
			const size_t end = std::min(count, begin + chunk);
			Submit([&runChunk, begin, end]() { runChunk(begin, end); });
		}

		runChunk(0, std::min(count, chunk));

		std::unique_lock<std::mutex> lock(doneMutex);
		doneCv.wait(lock, [&] { return remaining.load(std::memory_order_acquire) == 0; });
	}

}
