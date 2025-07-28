#pragma once

namespace StarEngine {
	/// TODO(Emily): Include a backtrace lib so we can get nice native
	///				 Stacktraces when something goes wrong even without a
	///				 Debugger attached.

	///
	/// Tries to catch as many fatal exit states and give the engine a chance
	/// To save unsaved data etc.
	///
	/// This is NOT for recoverable failure states -- the operating env. is in
	/// A largely undefined state after a fatal signal and it may not be safe
	/// To resume program flow. It also can make it difficult for user-provided
	/// Kill signals to actually end the program if they need to force-kill it.
	///
	/// Please try and avoid using spdlog from within fatal callback functions
	/// As it has a habit of ending up with messed up sink states during fatal
	/// Shutdown.
	///
	/// If a callback is taking too long or a nested failure state occurs --
	/// `FatalSignal` will force-shutdown the process.
	///
	class FatalSignal {
	private:
		using Proc = void();
		using ProcFn = std::function<Proc>;

		static FatalSignal s_State;

		std::vector<ProcFn> m_Callbacks;
		long m_Timeout; // Timeout in milliseconds.
		bool m_Active = false;

		void Handler(const char* what);

		static void Timeout();
		static void Terminate();

	public:
		// Kill the program immediately. Do not run any handlers or fallbacks.
		static void Die();

		static void Install(long timeout = 2000);

		static void Add(ProcFn& fn);
	};
}
