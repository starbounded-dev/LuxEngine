#pragma once

#include "Thread.h"

#include <atomic>
#include <string_view>

namespace Lux {

	struct RenderThreadData;

	enum class ThreadingPolicy
	{
		// MultiThreaded will create a Render Thread
		None = 0, SingleThreaded, MultiThreaded
	};

	// Canonical string forms used when persisting the policy in settings files.
	inline const char* ThreadingPolicyToString(ThreadingPolicy policy)
	{
		switch (policy)
		{
			case ThreadingPolicy::SingleThreaded: return "Single";
			case ThreadingPolicy::MultiThreaded:  return "Multi";
			default:                              return "Multi";
		}
	}

	// Parses a persisted policy string. Anything that isn't explicitly single-threaded
	// (including legacy/empty values) resolves to MultiThreaded.
	inline ThreadingPolicy ThreadingPolicyFromString(std::string_view str)
	{
		if (str == "Single" || str == "single" || str == "SingleThreaded")
			return ThreadingPolicy::SingleThreaded;
		return ThreadingPolicy::MultiThreaded;
	}

	class RenderThread
	{
	public:
		enum class State
		{
			Idle = 0,
			Busy,
			Kick
		};
	public:
		RenderThread(ThreadingPolicy coreThreadingPolicy);
		~RenderThread();

		void Run();
		bool IsRunning() const { return m_IsRunning; }
		void Terminate();

		void Wait(State waitForState);
		void WaitAndSet(State waitForState, State setToState);
		void Set(State setToState);

		void NextFrame();
		void BlockUntilRenderComplete();
		void Kick();

		void Pump();

		static bool IsCurrentThreadRT();
	private:
		RenderThreadData* m_Data;
		ThreadingPolicy m_ThreadingPolicy;

		Thread m_RenderThread;

		bool m_IsRunning = false;

		std::atomic<uint32_t> m_AppThreadFrame = 0;
	};

}
