#include "lpch.h"
#include "SimulationThread.h"

namespace Lux {

	SimulationThread::~SimulationThread()
	{
		Terminate();
	}

	void SimulationThread::Run()
	{
		if (m_Running)
			return;

		m_Running = true;
		m_State = State::Idle;
		m_Thread.Dispatch([this]() { ThreadFunc(); });
	}

	void SimulationThread::Kick(std::function<void()> work)
	{
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_Work = std::move(work);
			m_State = State::Kicked;
		}
		m_CV.notify_all();
	}

	void SimulationThread::BlockUntilComplete()
	{
		std::unique_lock<std::mutex> lock(m_Mutex);
		m_CV.wait(lock, [this]() { return m_State == State::Idle; });
	}

	void SimulationThread::Terminate()
	{
		if (!m_Running)
			return;

		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			// Wait for any in-flight work to finish, then signal shutdown.
			m_CV.wait(lock, [this]() { return m_State == State::Idle; });
			m_Running = false;
		}
		m_CV.notify_all();
		m_Thread.Join();
	}

	void SimulationThread::ThreadFunc()
	{
		for (;;)
		{
			std::function<void()> work;
			{
				std::unique_lock<std::mutex> lock(m_Mutex);
				m_CV.wait(lock, [this]() { return m_State == State::Kicked || !m_Running; });

				if (!m_Running)
					return;

				work = std::move(m_Work);
				m_Work = nullptr;
				m_State = State::Running;
			}

			if (work)
				work();

			{
				std::unique_lock<std::mutex> lock(m_Mutex);
				m_State = State::Idle;
			}
			m_CV.notify_all();
		}
	}

}
