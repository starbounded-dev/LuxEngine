#pragma once

#include "Thread.h"

#include <condition_variable>
#include <functional>
#include <mutex>

namespace Lux {

	// Drives game-state simulation on its own thread so it can run a frame ahead of (in parallel with)
	// the main thread's render submission + ImGui. Mirrors the RenderThread Kick/Idle handshake.
	//
	// The simulation thread is the sole writer of the scene ECS while it runs; the main thread must only
	// read render state from the captured FrameRenderPacket between BlockUntilComplete() and Kick().
	class SimulationThread
	{
	public:
		SimulationThread() = default;
		~SimulationThread();

		// Spawn the worker thread (idle until first Kick).
		void Run();
		bool IsRunning() const { return m_Running; }

		// Hand the per-frame simulation work to the thread and start it. Returns immediately; the work
		// runs concurrently with the caller until BlockUntilComplete().
		void Kick(std::function<void()> work);

		// Block the calling (main) thread until the simulation work from the last Kick has finished.
		void BlockUntilComplete();

		// Stop and join the worker thread.
		void Terminate();

	private:
		void ThreadFunc();

		enum class State { Idle, Kicked, Running };

		Thread m_Thread{ "Simulation Thread" };
		std::mutex m_Mutex;
		std::condition_variable m_CV;
		std::function<void()> m_Work;
		State m_State = State::Idle;
		bool m_Running = false;
	};

}
