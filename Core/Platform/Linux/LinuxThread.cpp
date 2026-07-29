#include "lpch.h"
#include "Lux/Core/Thread.h"

#include <pthread.h>

#include <condition_variable>
#include <mutex>

namespace Lux {

	Thread::Thread(const std::string& name)
		: m_Name(name)
	{
	}

	void Thread::SetName(const std::string& name)
	{
		// pthread limits thread names to 16 bytes (including the null terminator).
		pthread_setname_np(m_Thread.native_handle(), name.substr(0, 15).c_str());
	}

	void Thread::Join()
	{
		if (m_Thread.joinable())
			m_Thread.join();
	}

	// Linux replacement for the Win32 named-event ThreadSignal. Mirrors the auto/manual-reset
	// semantics of CreateEvent/SetEvent/ResetEvent. The Windows implementation never closes its
	// handle, so this matches that one-time leak (ThreadSignals are long-lived) rather than adding
	// a destructor to the shared header, which would also need a Windows definition.
	namespace {
		struct LinuxSignalState
		{
			std::mutex Mutex;
			std::condition_variable Condition;
			bool Signaled = false;
			bool ManualReset = false;
		};
	}

	ThreadSignal::ThreadSignal(const std::string& name, bool manualReset)
	{
		auto* state = new LinuxSignalState();
		state->ManualReset = manualReset;
		m_SignalHandle = state;
	}

	void ThreadSignal::Wait()
	{
		auto* state = static_cast<LinuxSignalState*>(m_SignalHandle);
		std::unique_lock<std::mutex> lock(state->Mutex);
		state->Condition.wait(lock, [state] { return state->Signaled; });

		// Auto-reset events consume the signal on a successful wait.
		if (!state->ManualReset)
			state->Signaled = false;
	}

	void ThreadSignal::Signal()
	{
		auto* state = static_cast<LinuxSignalState*>(m_SignalHandle);
		{
			std::lock_guard<std::mutex> lock(state->Mutex);
			state->Signaled = true;
		}
		// Manual-reset releases every waiter; auto-reset releases exactly one.
		if (state->ManualReset)
			state->Condition.notify_all();
		else
			state->Condition.notify_one();
	}

	void ThreadSignal::Reset()
	{
		auto* state = static_cast<LinuxSignalState*>(m_SignalHandle);
		std::lock_guard<std::mutex> lock(state->Mutex);
		state->Signaled = false;
	}

	std::thread::id Thread::GetID() const
	{
		return m_Thread.get_id();
	}
}
